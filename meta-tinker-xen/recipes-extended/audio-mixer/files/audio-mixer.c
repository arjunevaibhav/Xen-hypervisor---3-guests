// SPDX-License-Identifier: MIT
/*
 * audio-mixer: dom0's audio pipeline. Every sound of the system goes through
 * here into the board's one speaker output (the 3.5 mm jack of the onboard
 * USB audio codec, ALSA), 48 kHz stereo:
 *
 *   chimes   vehicle warnings and the reverse beep, made here. Highest
 *            priority: the guests' sound is turned down while one plays.
 *   media    media1's video sound: S16LE stereo 48 kHz in UDP datagrams
 *            (its ffmpeg sends them, paced by -re).
 *   android  android1's sound: S16LE stereo 44.1 kHz over TCP (the guest's
 *            audio HAL writes /dev/eac, a pipe that netcat sends here).
 *            Reading only what the speaker plays makes TCP flow control
 *            pace the guest's audio at the speaker's rate.
 *
 * The network sources listen on dom0's xenbr0 address only and take data
 * only from their guest's address. A source is buffered 60 ms before it
 * plays (network jitter) and played with linear resampling to 48 kHz.
 * Without a sound card the mixer keeps running (and keeps the guests paced),
 * and tries the card again every 2 s.
 *
 *   audio-mixer [-D alsa-device] [-a listen-addr] [-m port] [-M media1-addr]
 *               [-n port] [-N android1-addr] [-g media%,android%] [-v]
 *   audio-mixer -c warn|reverse|info   ask the running mixer for a chime
 *
 * Chime requests are names, one per line, in the FIFO /run/audio-mixer/chime;
 * "audio-mixer -c" writes one without blocking when the mixer is not running.
 */

#define _GNU_SOURCE	/* accept4 */
#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RATE		48000
#define PERIOD		960		/* frames per mix step: 20 ms */
#define LATENCY_US	80000		/* ALSA buffer: 4 periods */
#define RING		32768		/* frames per source, power of two */
#define CHIME_DIR	"/run/audio-mixer"
#define CHIME_FIFO	CHIME_DIR "/chime"
#define DUCK		0.25f		/* guest sound level while a chime plays */

static int verbose;

/* ---- network sources --------------------------------------------------- */

struct source {
	const char *name;
	int tcp;		/* 0: UDP datagrams, 1: one TCP connection at a time */
	unsigned int rate;
	struct in_addr peer;	/* the guest; INADDR_ANY: anyone */
	unsigned int port;
	int fd, conn;		/* socket (TCP: listening), TCP connection */
	int16_t ring[RING * 2];
	uint32_t rd, wr;	/* frame counters, wr - rd frames buffered */
	uint32_t frac;		/* 16.16 position between frames rd and rd + 1 */
	uint8_t part[4];	/* TCP: bytes of an incomplete frame */
	unsigned int npart;
	int playing;
	float gain;
	unsigned long drops, underruns;
};

static struct source media = { .name = "media", .rate = 48000, .port = 5601, .gain = 1.0f,
			       .fd = -1, .conn = -1 };
static struct source android = { .name = "android", .tcp = 1, .rate = 44100, .port = 5602,
				 .gain = 1.0f, .fd = -1, .conn = -1 };
static struct source *sources[] = { &media, &android };
#define NSOURCES (sizeof(sources) / sizeof(sources[0]))

static struct in_addr listen_addr;

static int source_open(struct source *s)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(s->port),
				 .sin_addr = listen_addr };
	int one = 1, small = 16384;
	char addr[16];

	s->fd = socket(AF_INET, (s->tcp ? SOCK_STREAM : SOCK_DGRAM) | SOCK_NONBLOCK, 0);
	if (s->fd < 0)
		return -1;
	setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	/* TCP: a small receive window keeps the guest's audio close to live */
	if (s->tcp)
		setsockopt(s->fd, SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
	if (bind(s->fd, (struct sockaddr *)&a, sizeof(a)) || (s->tcp && listen(s->fd, 1))) {
		close(s->fd);
		s->fd = -1;
		return -1;
	}
	/* inet_ntoa() has one static buffer: copy the first address */
	snprintf(addr, sizeof(addr), "%s", inet_ntoa(listen_addr));
	printf("audio-mixer: %s: %s %s:%u, %u Hz, from %s\n", s->name, s->tcp ? "TCP" : "UDP",
	       addr, s->port, s->rate,
	       s->peer.s_addr == INADDR_ANY ? "anyone" : inet_ntoa(s->peer));
	return 0;
}

static int peer_ok(const struct source *s, const struct sockaddr_in *from)
{
	return s->peer.s_addr == INADDR_ANY || from->sin_addr.s_addr == s->peer.s_addr;
}

static void push(struct source *s, const uint8_t *p, size_t frames)
{
	for (; frames; frames--, p += 4) {
		if (s->wr - s->rd >= RING - 1) {
			s->drops++;
			continue;
		}
		s->ring[(s->wr & (RING - 1)) * 2] = (int16_t)(p[0] | p[1] << 8);
		s->ring[(s->wr & (RING - 1)) * 2 + 1] = (int16_t)(p[2] | p[3] << 8);
		s->wr++;
	}
}

static void close_conn(struct source *s, const char *why)
{
	printf("audio-mixer: %s: %s\n", s->name, why);
	close(s->conn);
	s->conn = -1;
	s->npart = 0;
}

/* take what a source has sent (never blocks) */
static void source_read(struct source *s, short revents_fd, short revents_conn)
{
	static uint8_t buf[65536];
	struct sockaddr_in from;
	socklen_t fl = sizeof(from);
	ssize_t n;

	if (!s->tcp) {
		while ((n = recvfrom(s->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl)) > 0) {
			if (peer_ok(s, &from))
				push(s, buf, (size_t)n / 4);
			fl = sizeof(from);
		}
		/* a sender running fast (or a clock drifting) must not build
		 * up delay: keep at most 300 ms, drop back to 100 ms */
		if (s->wr - s->rd > s->rate * 3 / 10) {
			s->drops += s->wr - s->rd - s->rate / 10;
			s->rd = s->wr - s->rate / 10;
		}
		return;
	}

	if (revents_fd & POLLIN) {
		int c = accept4(s->fd, (struct sockaddr *)&from, &fl, SOCK_NONBLOCK);

		if (c >= 0 && !peer_ok(s, &from)) {
			printf("audio-mixer: %s: refused %s\n", s->name, inet_ntoa(from.sin_addr));
			close(c);
		} else if (c >= 0) {
			if (s->conn >= 0)
				close_conn(s, "replaced by a new connection");
			s->conn = c;
			printf("audio-mixer: %s: connected from %s\n", s->name, inet_ntoa(from.sin_addr));
		}
	}
	if (s->conn < 0 || !(revents_conn & (POLLIN | POLLHUP | POLLERR)))
		return;
	/* only up to 100 ms ahead: the rest waits in TCP and paces the guest */
	while (s->wr - s->rd < s->rate / 10) {
		size_t want = (s->rate / 10 - (s->wr - s->rd)) * 4;

		memcpy(buf, s->part, s->npart);
		n = read(s->conn, buf + s->npart, want < sizeof(buf) - 4 ? want : sizeof(buf) - 4);
		if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
			close_conn(s, n == 0 ? "disconnected" : strerror(errno));
			return;
		}
		if (n < 0)
			return;
		n += s->npart;
		push(s, buf, (size_t)n / 4);
		s->npart = (unsigned int)n % 4;
		memcpy(s->part, buf + n - s->npart, s->npart);
	}
}

/* does a TCP source want its connection polled (room in its buffer)? */
static int conn_wanted(const struct source *s)
{
	return s->conn >= 0 && s->wr - s->rd < s->rate / 10;
}

/* next output frame of a source, resampled to RATE; 0 when it has nothing */
static void source_frame(struct source *s, float *l, float *r)
{
	uint32_t n = s->wr - s->rd;
	const int16_t *a, *b;
	float f;

	if (!s->playing) {
		if (n < s->rate * 6 / 100)	/* 60 ms buffered before playing */
			return;
		s->playing = 1;
		s->frac = 0;
		if (verbose)
			printf("audio-mixer: %s: playing\n", s->name);
	}
	if (n < 2) {
		s->playing = 0;
		s->underruns++;
		if (verbose)
			printf("audio-mixer: %s: idle\n", s->name);
		return;
	}
	a = &s->ring[(s->rd & (RING - 1)) * 2];
	b = &s->ring[((s->rd + 1) & (RING - 1)) * 2];
	f = s->frac / 65536.0f;
	*l += (a[0] + (b[0] - a[0]) * f) * s->gain;
	*r += (a[1] + (b[1] - a[1]) * f) * s->gain;
	s->frac += (uint32_t)(((uint64_t)s->rate << 16) / RATE);
	s->rd += s->frac >> 16;
	s->frac &= 0xffff;
}

/* ---- chimes ------------------------------------------------------------ */

struct tone { unsigned int hz, on_ms, off_ms; };
static const struct {
	const char *name;
	float level;
	struct tone t[4];	/* ends at hz 0 */
} chimes[] = {
	/* vehicle warning (FreeRTOS: overspeed, redline, signal lost) */
	{ "warn",    0.45f, { { 880, 160, 90 }, { 880, 160, 90 }, { 880, 160, 300 } } },
	/* reverse gear engaged */
	{ "reverse", 0.35f, { { 1250, 90, 70 }, { 1250, 90, 200 } } },
	/* information (e.g. a warning cleared) */
	{ "info",    0.25f, { { 660, 120, 30 }, { 880, 200, 200 } } },
};
#define NCHIMES (sizeof(chimes) / sizeof(chimes[0]))

static int queue[8], qlen;		/* chimes waiting, by index */
static int cur = -1;			/* chime playing */
static unsigned int tone_i, tone_pos;	/* tone of it, and sample in that tone */
static float phase;

static void chime_request(const char *name)
{
	unsigned int i;

	for (i = 0; i < NCHIMES; i++)
		if (!strcmp(chimes[i].name, name))
			break;
	if (i == NCHIMES) {
		printf("audio-mixer: unknown chime \"%s\"\n", name);
		return;
	}
	if (verbose)
		printf("audio-mixer: chime %s\n", name);
	/* the same chime already waiting is not queued twice */
	if (cur == (int)i && tone_i == 0)
		return;
	for (int q = 0; q < qlen; q++)
		if (queue[q] == (int)i)
			return;
	if (qlen < (int)(sizeof(queue) / sizeof(queue[0])))
		queue[qlen++] = i;
}

/* next chime sample, 0 when none plays; 5 ms ramps keep the tones click-free */
static float chime_sample(void)
{
	const struct tone *t;
	unsigned int on, total, ramp = RATE / 200;
	float v = 0.0f, env;

	if (cur < 0) {
		if (!qlen)
			return 0.0f;
		cur = queue[0];
		memmove(queue, queue + 1, --qlen * sizeof(queue[0]));
		tone_i = tone_pos = 0;
		phase = 0.0f;
	}
	t = &chimes[cur].t[tone_i];
	on = t->on_ms * RATE / 1000;
	total = on + t->off_ms * RATE / 1000;
	if (tone_pos < on) {
		env = tone_pos < ramp ? (float)tone_pos / ramp :
		      on - tone_pos < ramp ? (float)(on - tone_pos) / ramp : 1.0f;
		v = sinf(phase) * env * chimes[cur].level * 32767.0f;
		phase += 2.0f * (float)M_PI * t->hz / RATE;
		if (phase > 2.0f * (float)M_PI)
			phase -= 2.0f * (float)M_PI;
	}
	if (++tone_pos >= total) {
		tone_pos = 0;
		phase = 0.0f;
		if (++tone_i >= 4 || !chimes[cur].t[tone_i].hz)
			cur = -1;
	}
	return v;
}

static int chime_open(void)
{
	mkdir(CHIME_DIR, 0755);
	if (mkfifo(CHIME_FIFO, 0600) && errno != EEXIST) {
		perror(CHIME_FIFO);
		return -1;
	}
	/* read and write: the FIFO never reports end of file between writers */
	return open(CHIME_FIFO, O_RDWR | O_NONBLOCK);
}

static void chime_read(int fd)
{
	static char line[64];
	static size_t len;
	char c;

	while (read(fd, &c, 1) == 1) {
		if (c == '\n') {
			line[len] = '\0';
			if (len)
				chime_request(line);
			len = 0;
		} else if (len < sizeof(line) - 1) {
			line[len++] = c;
		}
	}
}

static int chime_client(const char *name)
{
	int fd = open(CHIME_FIFO, O_WRONLY | O_NONBLOCK);

	if (fd < 0) {
		fprintf(stderr, "audio-mixer: not running (%s)\n", strerror(errno));
		return 1;
	}
	if (write(fd, name, strlen(name)) < 0 || write(fd, "\n", 1) < 0) {
		perror(CHIME_FIFO);
		return 1;
	}
	close(fd);
	return 0;
}

/* ---- main -------------------------------------------------------------- */

/* alsa-lib's own messages: the retries every 2 s would fill the log */
static void alsa_quiet(const char *file, int line, const char *func, int err, const char *fmt, ...)
{
	(void)file; (void)line; (void)func; (void)err; (void)fmt;
}

static snd_pcm_t *pcm_open(const char *dev, int quiet)
{
	snd_pcm_t *pcm;
	int r;

	r = snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
	if (r == 0)
		r = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
				       2, RATE, 1, LATENCY_US);
	if (r < 0) {
		if (!quiet)
			fprintf(stderr, "audio-mixer: %s: %s (trying again every 2 s)\n", dev,
				snd_strerror(r));
		return NULL;
	}
	printf("audio-mixer: playing on %s, %u Hz stereo\n", dev, RATE);
	return pcm;
}

static double since(const struct timespec *t)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - t->tv_sec) + (now.tv_nsec - t->tv_nsec) / 1e9;
}

int main(int argc, char **argv)
{
	const char *dev = "default";
	struct timespec last_try = { 0 }, next;
	static int16_t out[PERIOD * 2];
	snd_pcm_t *pcm = NULL;
	float duck = 1.0f;
	int opt, chime_fd, gm = 100, ga = 100, warned = 0;
	unsigned int i;

	inet_aton("192.168.100.1", &listen_addr);
	inet_aton("192.168.100.4", &media.peer);
	inet_aton("192.168.100.3", &android.peer);
	while ((opt = getopt(argc, argv, "D:a:m:M:n:N:g:c:v")) != -1) {
		switch (opt) {
		case 'D': dev = optarg; break;
		case 'a': if (!inet_aton(optarg, &listen_addr)) return 2; break;
		case 'm': media.port = atoi(optarg); break;
		case 'M': if (!inet_aton(optarg, &media.peer)) return 2; break;
		case 'n': android.port = atoi(optarg); break;
		case 'N': if (!inet_aton(optarg, &android.peer)) return 2; break;
		case 'g': if (sscanf(optarg, "%d,%d", &gm, &ga) != 2) return 2; break;
		case 'c': return chime_client(optarg);
		case 'v': verbose = 1; break;
		default:
			fprintf(stderr, "usage: %s [-D alsa-device] [-a listen-addr] [-m port] "
				"[-M media1-addr] [-n port] [-N android1-addr] [-g media%%,android%%] [-v]\n"
				"       %s -c warn|reverse|info\n", argv[0], argv[0]);
			return 2;
		}
	}
	media.gain = gm / 100.0f;
	android.gain = ga / 100.0f;
	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGPIPE, SIG_IGN);
	snd_lib_error_set_handler(alsa_quiet);

	chime_fd = chime_open();
	if (chime_fd < 0)
		return 1;
	clock_gettime(CLOCK_MONOTONIC, &next);

	for (;;) {
		struct pollfd p[1 + 2 * NSOURCES];
		int np = 1;

		/* sockets and sound card: (again) every 2 s until they open,
		 * e.g. before xenbr0 has its address */
		if (since(&last_try) >= 2.0) {
			for (i = 0; i < NSOURCES; i++)
				if (sources[i]->fd < 0 && source_open(sources[i]) && !warned)
					fprintf(stderr, "audio-mixer: %s: cannot listen on %s:%u: %s\n",
						sources[i]->name, inet_ntoa(listen_addr),
						sources[i]->port, strerror(errno));
			if (!pcm)
				pcm = pcm_open(dev, warned);
			warned = 1;
			clock_gettime(CLOCK_MONOTONIC, &last_try);
		}

		/* inputs: take whatever has arrived, without waiting */
		p[0] = (struct pollfd){ .fd = chime_fd, .events = POLLIN };
		for (i = 0; i < NSOURCES; i++) {
			p[np++] = (struct pollfd){ .fd = sources[i]->fd, .events = POLLIN };
			p[np++] = (struct pollfd){ .fd = conn_wanted(sources[i]) ? sources[i]->conn : -1,
						   .events = POLLIN };
		}
		poll(p, np, 0);
		if (p[0].revents & POLLIN)
			chime_read(chime_fd);
		for (i = 0; i < NSOURCES; i++)
			if (sources[i]->fd >= 0)
				source_read(sources[i], p[1 + 2 * i].revents, p[2 + 2 * i].revents);

		/* one period: chimes over the guests, which are turned down
		 * while a chime plays */
		for (i = 0; i < PERIOD; i++) {
			float l = 0.0f, r = 0.0f, c = chime_sample();
			float target = (cur >= 0 || qlen) ? DUCK : 1.0f;
			unsigned int k;

			duck += (target - duck) * 0.002f;	/* about 10 ms */
			for (k = 0; k < NSOURCES; k++)
				source_frame(sources[k], &l, &r);
			l = l * duck + c;
			r = r * duck + c;
			out[2 * i] = l > 32767.0f ? 32767 : l < -32768.0f ? -32768 : (int16_t)l;
			out[2 * i + 1] = r > 32767.0f ? 32767 : r < -32768.0f ? -32768 : (int16_t)r;
		}

		if (pcm) {
			/* blocks while the card's buffer is full: this paces the loop */
			snd_pcm_sframes_t n = snd_pcm_writei(pcm, out, PERIOD);

			if (n < 0 && snd_pcm_recover(pcm, (int)n, 1) < 0) {
				fprintf(stderr, "audio-mixer: %s: %s, reopening\n", dev, snd_strerror((int)n));
				snd_pcm_close(pcm);
				pcm = NULL;
				warned = 0;
			}
			clock_gettime(CLOCK_MONOTONIC, &next);
		} else {
			/* no sound card: keep the pace with the clock */
			next.tv_nsec += PERIOD * (1000000000L / RATE);
			while (next.tv_nsec >= 1000000000L) {
				next.tv_nsec -= 1000000000L;
				next.tv_sec++;
			}
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		}
	}
}

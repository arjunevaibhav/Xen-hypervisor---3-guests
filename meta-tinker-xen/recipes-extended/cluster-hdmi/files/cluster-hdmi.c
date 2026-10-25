// SPDX-License-Identifier: MIT
/*
 * cluster-hdmi: instrument-cluster dashboard on dom0's HDMI framebuffer.
 *
 * Shows speed, rpm and gear decoded from the vehicle CAN bus (receive only),
 * and the verdicts of the FreeRTOS guest's real-time vehicle task (overspeed,
 * redline, signal loss, 10 ms task timing), read from its console log that
 * xenconsoled writes (XENCONSOLED_TRACE=guest).
 *
 *   cluster-hdmi [-f /dev/fb0] [-i vcan0] [-l rtos console log] [-S snapshot.ppm]
 *   -S: draw once after 3 s, write a 1/4 scale PPM snapshot and exit (tests).
 *   -g WxH: draw into memory instead of a framebuffer (with -S, layout tests);
 *   -D: demo values (speed, rpm and all warnings) instead of live data.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ID_SPEED	0x0C0
#define ID_ENGINE	0x0C1
#define RPM_MAX		7000
#define REDLINE_RPM	6000
#define LOG_LIMIT	(2 * 1024 * 1024)	/* guest console logs are truncated above this */

/* ---- framebuffer ------------------------------------------------------- */

static uint8_t *fb;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static int W, H, tty_fd = -1;

static void restore_console(void)
{
	if (tty_fd >= 0) {
		ioctl(tty_fd, KDSETMODE, KD_TEXT);
		close(tty_fd);
		tty_fd = -1;
	}
}

static void on_signal(int sig)
{
	(void)sig;
	restore_console();
	_exit(0);
}

static uint32_t rgb(unsigned int r, unsigned int g, unsigned int b)
{
	return (r >> (8 - var.red.length)) << var.red.offset |
	       (g >> (8 - var.green.length)) << var.green.offset |
	       (b >> (8 - var.blue.length)) << var.blue.offset;
}

static void fill(int x, int y, int w, int h, uint32_t c)
{
	int i, j;

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > W) w = W - x;
	if (y + h > H) h = H - y;
	for (j = 0; j < h; j++) {
		uint8_t *p = fb + (size_t)(y + j + var.yoffset) * fix.line_length +
			     (size_t)x * (var.bits_per_pixel / 8);
		for (i = 0; i < w; i++) {
			if (var.bits_per_pixel == 32)
				((uint32_t *)p)[i] = c;
			else
				((uint16_t *)p)[i] = c;
		}
	}
}

static uint32_t pixel(int x, int y)
{
	uint8_t *p = fb + (size_t)(y + var.yoffset) * fix.line_length +
		     (size_t)x * (var.bits_per_pixel / 8);

	return var.bits_per_pixel == 32 ? *(uint32_t *)p : *(uint16_t *)p;
}

/* ---- 5x7 font ---------------------------------------------------------- */

static const struct { char c; uint8_t r[7]; } font[] = {
	{ '0', { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e } },
	{ '1', { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e } },
	{ '2', { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f } },
	{ '3', { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e } },
	{ '4', { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 } },
	{ '5', { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e } },
	{ '6', { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e } },
	{ '7', { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
	{ '8', { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e } },
	{ '9', { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c } },
	{ 'A', { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } },
	{ 'B', { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e } },
	{ 'C', { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e } },
	{ 'D', { 0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c } },
	{ 'E', { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f } },
	{ 'F', { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 } },
	{ 'G', { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f } },
	{ 'H', { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } },
	{ 'I', { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e } },
	{ 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c } },
	{ 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
	{ 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f } },
	{ 'M', { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 } },
	{ 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
	{ 'O', { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e } },
	{ 'P', { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 } },
	{ 'Q', { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d } },
	{ 'R', { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 } },
	{ 'S', { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e } },
	{ 'T', { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
	{ 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e } },
	{ 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 } },
	{ 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a } },
	{ 'X', { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 } },
	{ 'Y', { 0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04 } },
	{ 'Z', { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f } },
	{ ':', { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 } },
	{ '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c } },
	{ '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
	{ '-', { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 } },
	{ '+', { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 } },
	{ '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
	{ '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
	{ '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
	{ ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
	{ '=', { 0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00 } },
	{ '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
	{ '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
};

static const uint8_t *glyph(char c)
{
	size_t i;

	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	for (i = 0; i < sizeof(font) / sizeof(font[0]); i++)
		if (font[i].c == c)
			return font[i].r;
	return NULL;	/* space and unknown characters: blank */
}

/* text with 5x7 glyphs scaled by s (6s per character including the gap) */
static void text(int x, int y, int s, uint32_t fg, uint32_t bg, const char *str)
{
	for (; *str; str++, x += 6 * s) {
		const uint8_t *g = glyph(*str);
		int r, c;

		fill(x, y, 6 * s, 8 * s, bg);
		if (!g)
			continue;
		for (r = 0; r < 7; r++)
			for (c = 0; c < 5; c++)
				if (g[r] & (0x10 >> c))
					fill(x + c * s, y + r * s, s, s, fg);
	}
}

/* text in a box: the largest scale that fits; align 0 left, 1 centre, 2 right */
static void text_box(int x, int y, int w, int h, int align, uint32_t fg, uint32_t bg,
		     const char *str)
{
	int n = (int)strlen(str), sc = h / 8, tw;

	if (n && w / (6 * n) < sc)
		sc = w / (6 * n);
	if (sc < 1)
		sc = 1;
	fill(x, y, w, h, bg);
	tw = 6 * sc * n;
	text(x + (align == 1 ? (w - tw) / 2 : align == 2 ? w - tw : 0), y + (h - 8 * sc) / 2,
	     sc, fg, bg, str);
}

/* ---- vehicle and RTOS state ------------------------------------------- */

static unsigned int speed100, rpm, frames_10s;
static struct timespec last_frame;
static int rtos_overspeed, rtos_redline, rtos_signal = -1, rtos_have_report;
static unsigned int rt_min, rt_max, rtos_frames;
static time_t rtos_last_report;

static double since(const struct timespec *t)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - t->tv_sec) + (now.tv_nsec - t->tv_nsec) / 1e9;
}

static unsigned int gear(double kmh)
{
	static const double top[] = { 25, 70, 95, 115, 130, 1e9 };
	unsigned int g = 0;

	if (kmh < 1.0)
		return 0;
	while (kmh > top[g])
		g++;
	return g + 1;
}

static void rtos_line(const char *l)
{
	unsigned int a, b, c;
	const char *p;

	if (strstr(l, "ALERT overspeed"))
		rtos_overspeed = 1;
	else if (strstr(l, "overspeed cleared"))
		rtos_overspeed = 0;
	else if (strstr(l, "ALERT engine over redline"))
		rtos_redline = 1;
	else if (strstr(l, "redline cleared"))
		rtos_redline = 0;
	else if (strstr(l, "SIGNAL LOST"))
		rtos_signal = 0;
	else if (strstr(l, "SIGNAL OK") || strstr(l, "first vehicle frame"))
		rtos_signal = 1;
	if ((p = strstr(l, "10 ms task:")) &&
	    sscanf(p, "10 ms task: %u periods, min %u us, max %u us", &a, &b, &c) == 3) {
		rt_min = b;
		rt_max = c;
		rtos_have_report = 1;
		rtos_last_report = time(NULL);
	}
	if ((p = strstr(l, "[vehicle] 10s: frames")) && sscanf(p, "[vehicle] 10s: frames %u", &a) == 1)
		rtos_frames = a;
}

/* follow the RTOS console log; keep the guest logs bounded */
static void poll_rtos_log(const char *path)
{
	static FILE *f;
	static ino_t ino;
	static char line[256];
	static size_t len;
	struct stat st;
	int ch;

	if (stat(path, &st) == 0 && (st.st_ino != ino || (f && st.st_size < ftell(f)))) {
		if (f)
			fclose(f);
		f = fopen(path, "r");
		ino = st.st_ino;
		len = 0;
	}
	if (!f)
		return;
	while ((ch = fgetc(f)) != EOF) {
		if (ch == '\n' || len == sizeof(line) - 1) {
			line[len] = '\0';
			rtos_line(line);
			len = 0;
		} else if (ch != '\r') {
			line[len++] = ch;
		}
	}
	clearerr(f);
	if (st.st_size > LOG_LIMIT) {
		if (truncate(path, 0) == 0)
			fseek(f, 0, SEEK_SET);
	}
}

static void trim_guest_logs(void)
{
	static const char *const logs[] = {
		"/var/log/xen/console/guest-android1.log",
	};
	struct stat st;
	size_t i;

	for (i = 0; i < sizeof(logs) / sizeof(logs[0]); i++)
		if (stat(logs[i], &st) == 0 && st.st_size > LOG_LIMIT)
			if (truncate(logs[i], 0)) { /* best effort */ }
}

static double cpu_temp(void)
{
	FILE *f = fopen("/sys/class/thermal/thermal_zone1/temp", "r");
	int t = 0;

	if (f) {
		if (fscanf(f, "%d", &t) != 1)
			t = 0;
		fclose(f);
	}
	return t / 1000.0;
}

/* ---- drawing ----------------------------------------------------------- */

static uint32_t c_bg, c_panel, c_text, c_dim, c_green, c_amber, c_red, c_blue;

static void lamp(int x, int y, int w, int h, int s, const char *label, int state)
{
	/* state: 1 = warning (red), 2 = OK (green text), 0 = inactive, -1 = unknown */
	uint32_t c = state == 1 ? c_red : state == -1 ? c_dim : c_panel;
	uint32_t fg = state == 1 ? c_text : state == 2 ? c_green : state == -1 ? c_panel : c_dim;

	(void)s;
	fill(x, y, w, h, c);
	text_box(x + w / 12, y + h / 5, w * 10 / 12, h * 3 / 5, 1, fg, c, label);
}

static void draw_static(const char *ifname)
{
	char t[96];

	fill(0, 0, W, H, c_bg);
	snprintf(t, sizeof(t), "TINKER XEN CLUSTER - %s",
		 strncmp(ifname, "vcan", 4) ? "LISTEN-ONLY CAN" : "BENCH: SIMULATED VEHICLE");
	text_box(W / 40, H / 40, W * 38 / 40, H * 3 / 40, 0, c_blue, c_bg, t);
	text_box(W / 20, H * 7 / 40, W * 18 / 40, H * 2 / 40, 0, c_dim, c_bg, "SPEED  KM/H");
	text_box(W * 22 / 40, H * 7 / 40, W * 16 / 40, H * 2 / 40, 0, c_dim, c_bg, "ENGINE RPM");
}

static void draw_dynamic(const char *ifname)
{
	int s = 1;
	int signal_ok = since(&last_frame) < 0.2;
	double kmh = speed100 / 100.0;
	char t[96];
	int bx = W * 22 / 40, by = H * 18 / 40, bw = W * 16 / 40, bh = H * 6 / 40, i;
	unsigned int g;

	/* speed */
	snprintf(t, sizeof(t), "%3u", signal_ok ? speed100 / 100 : 0);
	text_box(W / 20, H * 10 / 40, W * 18 / 40, H * 13 / 40, 0,
		 signal_ok ? (rtos_overspeed ? c_amber : c_text) : c_dim, c_bg,
		 signal_ok ? t : "---");

	/* rpm bar with the red zone above the redline */
	fill(bx, by, bw, bh, c_panel);
	fill(bx + bw * REDLINE_RPM / RPM_MAX, by, bw - bw * REDLINE_RPM / RPM_MAX, bh / 6, c_red);
	if (signal_ok) {
		int len = bw * (rpm > RPM_MAX ? RPM_MAX : rpm) / RPM_MAX;

		fill(bx, by + bh / 4, len, bh / 2, rpm > REDLINE_RPM ? c_red : c_green);
	}
	for (i = 0; i <= RPM_MAX; i += 1000)
		fill(bx + bw * i / RPM_MAX - W / 640, by + bh, W / 320 + 1, bh / 4, c_dim);
	snprintf(t, sizeof(t), "%4u", signal_ok ? rpm : 0);
	text_box(bx, H * 10 / 40, bw, H * 7 / 40, 0, c_text, c_bg, signal_ok ? t : "----");
	g = gear(kmh);
	snprintf(t, sizeof(t), "GEAR %c", !signal_ok ? '-' : g ? (char)('0' + g) : 'N');
	text_box(bx, by + bh * 3 / 2, bw, H * 3 / 40, 0, c_text, c_bg, t);

	/* warning lamps from the FreeRTOS real-time checks */
	{
		int ly = H * 30 / 40, lh = H * 4 / 40, lw = W * 21 / 100, gap = W / 50, lx = W / 40;
		int rtos_alive = rtos_have_report && time(NULL) - rtos_last_report < 25;

		lamp(lx, ly, lw, lh, s * 2, "OVERSPEED", rtos_overspeed);
		lamp(lx + (lw + gap), ly, lw, lh, s * 2, "REDLINE", rtos_redline);
		lamp(lx + 2 * (lw + gap), ly, lw, lh, s * 2,
		     rtos_signal == 0 ? "SIGNAL LOST" : "SIGNAL OK",
		     rtos_signal < 0 ? -1 : rtos_signal == 0 ? 1 : 2);
		lamp(lx + 3 * (lw + gap), ly, lw, lh, s * 2,
		     rtos_alive ? "RTOS OK" : "RTOS ?", rtos_alive ? 2 : 1);
	}

	/* status line */
	snprintf(t, sizeof(t), "RTOS 10MS TASK %u-%u US  CAN %u/S  RTOS RX %u/10S  CPU %.1f C  %s",
		 rt_min, rt_max, frames_10s / 10, rtos_frames, cpu_temp(), ifname);
	text_box(W / 40, H * 36 / 40, W * 38 / 40, H * 2 / 40, 0, c_dim, c_bg, t);
	(void)s;
}

static void snapshot(const char *path)
{
	FILE *f = fopen(path, "w");
	int x, y;

	if (!f) {
		perror(path);
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", W / 4, H / 4);
	for (y = 0; y < H / 4; y++)
		for (x = 0; x < W / 4; x++) {
			uint32_t p = pixel(x * 4, y * 4);
			unsigned char c[3] = {
				((p >> var.red.offset) & ((1 << var.red.length) - 1)) << (8 - var.red.length),
				((p >> var.green.offset) & ((1 << var.green.length) - 1)) << (8 - var.green.length),
				((p >> var.blue.offset) & ((1 << var.blue.length) - 1)) << (8 - var.blue.length),
			};

			fwrite(c, 1, 3, f);
		}
	fclose(f);
	printf("cluster-hdmi: snapshot %s (%dx%d)\n", path, W / 4, H / 4);
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char **argv)
{
	const char *fbdev = "/dev/fb0", *ifname = "vcan0", *snap = NULL;
	const char *log = "/var/log/xen/console/guest-rtos1.log";
	struct sockaddr_can addr = { .can_family = AF_CAN };
	struct can_filter flt[2] = {
		{ .can_id = ID_SPEED, .can_mask = CAN_SFF_MASK },
		{ .can_id = ID_ENGINE, .can_mask = CAN_SFF_MASK },
	};
	struct timespec start, last_draw = { 0 }, last_rate;
	unsigned int frames = 0;
	int fd, s, opt, gw = 0, gh = 0, demo = 0;

	while ((opt = getopt(argc, argv, "f:i:l:S:g:D")) != -1) {
		switch (opt) {
		case 'f': fbdev = optarg; break;
		case 'i': ifname = optarg; break;
		case 'l': log = optarg; break;
		case 'S': snap = optarg; break;
		case 'g': if (sscanf(optarg, "%dx%d", &gw, &gh) != 2) return 2; break;
		case 'D': demo = 1; break;
		default:
			fprintf(stderr, "usage: %s [-f fbdev] [-i iface] [-l rtos log] [-S snapshot.ppm]\n",
				argv[0]);
			return 2;
		}
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	if (gw > 0 && gh > 0) {
		/* memory "framebuffer", XRGB8888 */
		var.xres = gw;
		var.yres = var.yres_virtual = gh;
		var.bits_per_pixel = 32;
		var.red = (struct fb_bitfield){ 16, 8, 0 };
		var.green = (struct fb_bitfield){ 8, 8, 0 };
		var.blue = (struct fb_bitfield){ 0, 8, 0 };
		fix.line_length = gw * 4;
		fb = calloc((size_t)gw * gh, 4);
		fbdev = "memory";
	} else {
		fd = open(fbdev, O_RDWR);
		if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) ||
		    ioctl(fd, FBIOGET_FSCREENINFO, &fix) ||
		    (var.bits_per_pixel != 16 && var.bits_per_pixel != 32)) {
			fprintf(stderr, "cluster-hdmi: %s: unusable framebuffer\n", fbdev);
			return 1;
		}
		fb = mmap(NULL, fix.line_length * var.yres_virtual, PROT_READ | PROT_WRITE,
			  MAP_SHARED, fd, 0);
		if (fb == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
		tty_fd = open("/dev/tty1", O_RDWR);
		if (tty_fd >= 0)
			ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
	}
	W = var.xres;
	H = var.yres;
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	c_bg = rgb(8, 10, 16);
	c_panel = rgb(28, 32, 44);
	c_text = rgb(235, 238, 245);
	c_dim = rgb(110, 118, 135);
	c_green = rgb(40, 200, 90);
	c_amber = rgb(255, 176, 0);
	c_red = rgb(220, 40, 40);
	c_blue = rgb(80, 160, 255);

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	addr.can_ifindex = if_nametoindex(ifname);
	if (s < 0 || !addr.can_ifindex ||
	    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, flt, sizeof(flt)) ||
	    bind(s, (struct sockaddr *)&addr, sizeof(addr)))
		fprintf(stderr, "cluster-hdmi: %s: %s (no vehicle data)\n", ifname, strerror(errno));
	printf("cluster-hdmi: %s %dx%d %u bpp, CAN %s (receive only), RTOS log %s\n",
	       fbdev, W, H, var.bits_per_pixel, ifname, log);

	draw_static(ifname);
	clock_gettime(CLOCK_MONOTONIC, &start);
	last_rate = start;
	last_frame.tv_sec = start.tv_sec - 10;

	for (;;) {
		struct pollfd p = { .fd = s, .events = POLLIN };
		struct can_frame f;

		if (poll(&p, s >= 0, 20) > 0 && read(s, &f, sizeof(f)) == sizeof(f) && f.can_dlc >= 2) {
			unsigned int v = f.data[0] << 8 | f.data[1];

			if (f.can_id == ID_SPEED) {
				speed100 = v;
				frames++;
				clock_gettime(CLOCK_MONOTONIC, &last_frame);
			} else {
				rpm = v;
			}
		}
		if (since(&last_rate) >= 10.0) {
			frames_10s = frames;
			frames = 0;
			clock_gettime(CLOCK_MONOTONIC, &last_rate);
			trim_guest_logs();
		}
		if (demo) {
			speed100 = 12800;
			rpm = 6250;
			frames_10s = 1000;
			clock_gettime(CLOCK_MONOTONIC, &last_frame);
			rtos_line("[vehicle] ALERT overspeed: 128 km/h (limit 120)");
			rtos_line("[vehicle] ALERT engine over redline: 6250 rpm (limit 6000)");
			rtos_line("[vehicle] SIGNAL OK again (seq 1)");
			rtos_line("[rtos] 10 ms task: 1000 periods, min 9997 us, max 10003 us; heartbeats 9");
			rtos_line("[vehicle] 10s: frames 1000, lost 0");
		}
		if (since(&last_draw) >= 0.1) {	/* 10 frames per second */
			poll_rtos_log(log);
			draw_dynamic(ifname);
			clock_gettime(CLOCK_MONOTONIC, &last_draw);
			if (snap && since(&start) >= 3.0) {
				snapshot(snap);
				restore_console();
				return 0;
			}
		}
	}
}

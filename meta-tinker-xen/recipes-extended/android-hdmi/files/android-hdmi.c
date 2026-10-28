// SPDX-License-Identifier: MIT
/*
 * android-hdmi: show a Xen guest's PV framebuffer on dom0's HDMI monitor
 * and give the guest dom0's touchscreen, mouse and keyboard.
 *
 * QEMU (Xen's device model in dom0) serves the guest's framebuffer and
 * keyboard/pointer as a VNC server (android1.cfg: vnc=1, vnclisten=127.0.0.1).
 * This is a minimal VNC client: raw encoding into /dev/fb0, centred, and
 * dom0 evdev input sent back as VNC pointer and key events.
 *
 *   android-hdmi [-H host] [-p port] [-f /dev/fb0] [-I input-glob | -I none]
 *   android-hdmi [-H host] [-p port] -t X,Y   (one tap at guest X,Y, then exit)
 *
 * -I picks the input devices forwarded to the guest (default all of
 * /dev/input/event*); with one screen per guest (display-map) each viewer
 * gets its own, or none.
 */

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
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
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#define MAX_INPUTS	16
#define CURSOR		5	/* cursor half size in pixels */

/* ---- framebuffer ------------------------------------------------------- */

static int fb_fd = -1, tty_fd = -1;
static uint8_t *fb;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static unsigned int fb_w, fb_h, bpp;	/* bpp in bytes */

/* guest screen size and where it sits on the monitor */
static unsigned int g_w, g_h, off_x, off_y;

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

/* rear-camera stops this program (SIGSTOP) while it shows the camera on the
 * same screen; on SIGCONT the whole guest screen is fetched again */
static volatile sig_atomic_t refresh;

static void on_cont(int sig)
{
	(void)sig;
	refresh = 1;
}

/* Is the text console (tty1) on this framebuffer? With several screens it
 * may be on another one, which must keep showing text. */
static int console_on(int fd)
{
	struct fb_con2fbmap map = { .console = 1 };
	struct stat st;

	if (fstat(fd, &st) || ioctl(fd, FBIOGET_CON2FBMAP, &map))
		return 0;
	return map.framebuffer == minor(st.st_rdev);
}

static int fb_open(const char *dev)
{
	fb_fd = open(dev, O_RDWR);
	if (fb_fd < 0 || ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) ||
	    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix)) {
		perror(dev);
		return -1;
	}
	if (var.bits_per_pixel != 16 && var.bits_per_pixel != 32) {
		fprintf(stderr, "%s: %u bpp not supported\n", dev, var.bits_per_pixel);
		return -1;
	}
	fb_w = var.xres;
	fb_h = var.yres;
	bpp = var.bits_per_pixel / 8;
	fb = mmap(NULL, fix.line_length * var.yres_virtual, PROT_READ | PROT_WRITE,
		  MAP_SHARED, fb_fd, 0);
	if (fb == MAP_FAILED) {
		perror("mmap");
		return -1;
	}
	ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);

	/* keep the text console from drawing over the picture */
	if (console_on(fb_fd)) {
		tty_fd = open("/dev/tty1", O_RDWR);
		if (tty_fd >= 0)
			ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
	}
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCONT, on_cont);
	return 0;
}

static uint8_t *fb_pixel(unsigned int x, unsigned int y)
{
	return fb + (size_t)(y + var.yoffset) * fix.line_length + (size_t)x * bpp;
}

static void fb_clear(void)
{
	unsigned int y;

	for (y = 0; y < fb_h; y++)
		memset(fb_pixel(0, y), 0, (size_t)fb_w * bpp);
}

static void set_guest_size(unsigned int w, unsigned int h)
{
	g_w = w;
	g_h = h;
	off_x = fb_w > w ? (fb_w - w) / 2 : 0;
	off_y = fb_h > h ? (fb_h - h) / 2 : 0;
	fb_clear();
	printf("android-hdmi: guest %ux%u on %ux%u monitor at +%u+%u\n",
	       w, h, fb_w, fb_h, off_x, off_y);
}

/* ---- mouse cursor (drawn by us, the guest has none) --------------------- */

static int cur_x, cur_y, cur_shown, cur_used;
static uint8_t cur_save[(2 * CURSOR + 1) * (2 * CURSOR + 1) * 4];

static int cur_cell(int dx, int dy, unsigned int *x, unsigned int *y)
{
	int px = (int)off_x + cur_x + dx, py = (int)off_y + cur_y + dy;

	if (px < 0 || py < 0 || px >= (int)fb_w || py >= (int)fb_h)
		return 0;
	*x = px;
	*y = py;
	return 1;
}

static void cursor_hide(void)
{
	unsigned int x, y, i = 0;
	int dx, dy;

	if (!cur_shown)
		return;
	for (dy = -CURSOR; dy <= CURSOR; dy++)
		for (dx = -CURSOR; dx <= CURSOR; dx++, i++)
			if ((dx == 0 || dy == 0) && cur_cell(dx, dy, &x, &y))
				memcpy(fb_pixel(x, y), &cur_save[i * 4], bpp);
	cur_shown = 0;
}

static void cursor_show(void)
{
	unsigned int x, y, i = 0;
	int dx, dy;

	if (!cur_used || cur_shown)
		return;
	for (dy = -CURSOR; dy <= CURSOR; dy++)
		for (dx = -CURSOR; dx <= CURSOR; dx++, i++)
			if ((dx == 0 || dy == 0) && cur_cell(dx, dy, &x, &y)) {
				memcpy(&cur_save[i * 4], fb_pixel(x, y), bpp);
				memset(fb_pixel(x, y), 0xff, bpp);
			}
	cur_shown = 1;
}

/* ---- VNC (RFB 3.8, raw encoding) ---------------------------------------- */

static int sock = -1;

static int read_full(void *buf, size_t n)
{
	uint8_t *p = buf;

	while (n) {
		ssize_t r = read(sock, p, n);

		if (r <= 0) {
			if (r < 0 && errno == EINTR)
				continue;
			return -1;
		}
		p += r;
		n -= r;
	}
	return 0;
}

static int skip(size_t n)
{
	uint8_t buf[4096];

	while (n) {
		size_t c = n < sizeof(buf) ? n : sizeof(buf);

		if (read_full(buf, c))
			return -1;
		n -= c;
	}
	return 0;
}

static int write_full(const void *buf, size_t n)
{
	return write(sock, buf, n) == (ssize_t)n ? 0 : -1;
}

static void put16(uint8_t *p, unsigned int v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }
static unsigned int get16(const uint8_t *p) { return p[0] << 8 | p[1]; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) << 16 | get16(p + 2); }

static int vnc_connect(const char *host, int port)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(port) };
	uint8_t buf[64];
	unsigned int i, n, one = 1;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	inet_pton(AF_INET, host, &sa.sin_addr);
	if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)))
		goto fail;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

	if (read_full(buf, 12) || memcmp(buf, "RFB 003.", 8))
		goto fail;
	if (write_full("RFB 003.008\n", 12))
		goto fail;

	/* security: only "None" (the server listens on 127.0.0.1) */
	if (read_full(buf, 1) || (n = buf[0]) == 0 || read_full(buf + 1, n))
		goto fail;
	for (i = 1; i <= n && buf[i] != 1; i++)
		;
	if (i > n) {
		fprintf(stderr, "android-hdmi: server needs a password\n");
		goto fail;
	}
	buf[0] = 1;
	if (write_full(buf, 1) || read_full(buf, 4) || get32(buf) != 0)
		goto fail;

	buf[0] = 1;	/* ClientInit: shared */
	if (write_full(buf, 1) || read_full(buf, 24))
		goto fail;
	set_guest_size(get16(buf), get16(buf + 2));
	if (skip(get32(buf + 20)))
		goto fail;

	/* SetPixelFormat: the monitor's own format, so raw rows copy as is */
	memset(buf, 0, 20);
	buf[4] = var.bits_per_pixel;
	buf[5] = var.bits_per_pixel == 32 ? 24 : 16;
	buf[6] = 0;	/* little endian */
	buf[7] = 1;	/* true colour */
	put16(buf + 8, (1 << var.red.length) - 1);
	put16(buf + 10, (1 << var.green.length) - 1);
	put16(buf + 12, (1 << var.blue.length) - 1);
	buf[14] = var.red.offset;
	buf[15] = var.green.offset;
	buf[16] = var.blue.offset;
	if (write_full(buf, 20))
		goto fail;

	/* SetEncodings: Raw, DesktopSize */
	memset(buf, 0, 12);
	buf[0] = 2;
	put16(buf + 2, 2);
	put32(buf + 4, 0);
	put32(buf + 8, (uint32_t)-223);
	if (write_full(buf, 12))
		goto fail;
	return 0;
fail:
	close(sock);
	sock = -1;
	return -1;
}

static int request_update(int incremental)
{
	uint8_t buf[10] = { 3, incremental };

	put16(buf + 6, g_w);
	put16(buf + 8, g_h);
	return write_full(buf, 10);
}

static int read_rect_raw(unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
	static uint8_t row[8192 * 4];
	size_t len = (size_t)w * bpp;
	unsigned int r, cw;

	if (len > sizeof(row))
		return -1;
	for (r = 0; r < h; r++) {
		if (read_full(row, len))
			return -1;
		if (off_y + y + r >= fb_h || off_x + x >= fb_w)
			continue;
		cw = off_x + x + w > fb_w ? fb_w - off_x - x : w;
		memcpy(fb_pixel(off_x + x, off_y + y + r), row, (size_t)cw * bpp);
	}
	return 0;
}

/* one server message; 1 when a framebuffer update is complete */
static int vnc_message(void)
{
	uint8_t buf[16];
	unsigned int i, n;

	if (read_full(buf, 1))
		return -1;
	switch (buf[0]) {
	case 0:		/* FramebufferUpdate */
		if (read_full(buf, 3))
			return -1;
		n = get16(buf + 1);
		cursor_hide();
		for (i = 0; i < n; i++) {
			int32_t enc;

			if (read_full(buf, 12))
				return -1;
			enc = (int32_t)get32(buf + 8);
			if (enc == 0) {
				if (read_rect_raw(get16(buf), get16(buf + 2),
						  get16(buf + 4), get16(buf + 6)))
					return -1;
			} else if (enc == -223) {
				set_guest_size(get16(buf + 4), get16(buf + 6));
			} else {
				fprintf(stderr, "android-hdmi: encoding %d\n", enc);
				return -1;
			}
		}
		cursor_show();
		return 1;
	case 1:		/* SetColourMapEntries */
		if (read_full(buf, 5))
			return -1;
		return skip((size_t)get16(buf + 3) * 6);
	case 2:		/* Bell */
		return 0;
	case 3:		/* ServerCutText */
		if (read_full(buf, 7))
			return -1;
		return skip(get32(buf + 3));
	default:
		fprintf(stderr, "android-hdmi: message type %u\n", buf[0]);
		return -1;
	}
}

static void send_pointer(unsigned int buttons, int x, int y)
{
	uint8_t buf[6] = { 5, buttons };

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= (int)g_w) x = g_w - 1;
	if (y >= (int)g_h) y = g_h - 1;
	put16(buf + 2, x);
	put16(buf + 4, y);
	write_full(buf, 6);
}

static void send_key(int down, uint32_t keysym)
{
	uint8_t buf[8] = { 4, down };

	put32(buf + 4, keysym);
	write_full(buf, 8);
}

/* ---- input ------------------------------------------------------------- */

struct input {
	int fd;
	char path[64];
	int abs;			/* touchscreen / tablet */
	struct input_absinfo ax, ay;
	int x, y, touch;
};

static struct input inputs[MAX_INPUTS];
static int ninputs;
static const char *input_glob = "/dev/input/event*";
static unsigned int mouse_buttons;

#define TEST(bits, b) ((bits)[(b) / 8] & (1 << ((b) % 8)))

static void scan_inputs(void)
{
	glob_t g;
	size_t i;
	int j;

	if (!strcmp(input_glob, "none") || glob(input_glob, 0, NULL, &g))
		return;
	for (i = 0; i < g.gl_pathc && ninputs < MAX_INPUTS; i++) {
		uint8_t evbits[EV_MAX / 8 + 1] = { 0 }, absbits[ABS_MAX / 8 + 1] = { 0 };
		struct input *in = &inputs[ninputs];

		for (j = 0; j < ninputs; j++)
			if (!strcmp(inputs[j].path, g.gl_pathv[i]))
				break;
		if (j < ninputs)
			continue;
		memset(in, 0, sizeof(*in));
		in->fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
		if (in->fd < 0)
			continue;
		snprintf(in->path, sizeof(in->path), "%s", g.gl_pathv[i]);
		ioctl(in->fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
		if (TEST(evbits, EV_ABS)) {
			ioctl(in->fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
			if (TEST(absbits, ABS_X) && TEST(absbits, ABS_Y)) {
				in->abs = 1;
				ioctl(in->fd, EVIOCGABS(ABS_X), &in->ax);
				ioctl(in->fd, EVIOCGABS(ABS_Y), &in->ay);
			}
		}
		ninputs++;
	}
	globfree(&g);
}

/* Linux key code -> X keysym (US layout); 0 = not forwarded */
static uint32_t keysym(unsigned int code)
{
	static const char row1[] = "1234567890-=", row2[] = "qwertyuiop[]",
			  row3[] = "asdfghjkl;'`", row4[] = "zxcvbnm,./";

	if (code >= KEY_1 && code <= KEY_EQUAL) return row1[code - KEY_1];
	if (code >= KEY_Q && code <= KEY_RIGHTBRACE) return row2[code - KEY_Q];
	if (code >= KEY_A && code <= KEY_GRAVE) return row3[code - KEY_A];
	if (code >= KEY_Z && code <= KEY_SLASH) return row4[code - KEY_Z];
	if (code >= KEY_F1 && code <= KEY_F10) return 0xffbe + code - KEY_F1;
	switch (code) {
	case KEY_BACKSLASH: return '\\';
	case KEY_SPACE: return ' ';
	case KEY_ESC: return 0xff1b;
	case KEY_BACKSPACE: return 0xff08;
	case KEY_TAB: return 0xff09;
	case KEY_ENTER: case KEY_KPENTER: return 0xff0d;
	case KEY_LEFTSHIFT: return 0xffe1;
	case KEY_RIGHTSHIFT: return 0xffe2;
	case KEY_LEFTCTRL: return 0xffe3;
	case KEY_RIGHTCTRL: return 0xffe4;
	case KEY_LEFTALT: return 0xffe9;
	case KEY_RIGHTALT: return 0xffea;
	case KEY_CAPSLOCK: return 0xffe5;
	case KEY_F11: return 0xffc8;
	case KEY_F12: return 0xffc9;
	case KEY_HOME: return 0xff50;
	case KEY_LEFT: return 0xff51;
	case KEY_UP: return 0xff52;
	case KEY_RIGHT: return 0xff53;
	case KEY_DOWN: return 0xff54;
	case KEY_PAGEUP: return 0xff55;
	case KEY_PAGEDOWN: return 0xff56;
	case KEY_END: return 0xff57;
	case KEY_INSERT: return 0xff63;
	case KEY_DELETE: return 0xffff;
	default: return 0;
	}
}

static void handle_event(struct input *in, const struct input_event *ev)
{
	if (ev->type == EV_ABS && in->abs) {
		const struct input_absinfo *a = NULL;
		unsigned int size = 0;
		int *v = NULL;

		if (ev->code == ABS_X || ev->code == ABS_MT_POSITION_X) {
			a = &in->ax; size = fb_w; v = &in->x;
		} else if (ev->code == ABS_Y || ev->code == ABS_MT_POSITION_Y) {
			a = &in->ay; size = fb_h; v = &in->y;
		}
		/* monitor coordinates, then relative to the guest picture */
		if (a && a->maximum > a->minimum)
			*v = (int)((long long)(ev->value - a->minimum) * size /
				   (a->maximum - a->minimum + 1));
	} else if (ev->type == EV_KEY && in->abs &&
		   (ev->code == BTN_TOUCH || ev->code == BTN_LEFT)) {
		in->touch = ev->value != 0;
	} else if (ev->type == EV_SYN && ev->code == SYN_REPORT && in->abs) {
		send_pointer(in->touch, in->x - (int)off_x, in->y - (int)off_y);
	} else if (ev->type == EV_REL) {
		if (ev->code == REL_X || ev->code == REL_Y) {
			int *c = ev->code == REL_X ? &cur_x : &cur_y;
			int max = ev->code == REL_X ? (int)g_w - 1 : (int)g_h - 1;

			cursor_hide();
			*c += ev->value;
			if (*c < 0) *c = 0;
			if (*c > max) *c = max;
			cur_used = 1;
			cursor_show();
			send_pointer(mouse_buttons, cur_x, cur_y);
		} else if (ev->code == REL_WHEEL && ev->value) {
			unsigned int b = ev->value > 0 ? 8 : 16;	/* buttons 4/5 */

			send_pointer(mouse_buttons | b, cur_x, cur_y);
			send_pointer(mouse_buttons, cur_x, cur_y);
		}
	} else if (ev->type == EV_KEY && ev->code >= BTN_LEFT && ev->code <= BTN_MIDDLE) {
		unsigned int bit = ev->code == BTN_LEFT ? 1 : ev->code == BTN_MIDDLE ? 2 : 4;

		mouse_buttons = ev->value ? mouse_buttons | bit : mouse_buttons & ~bit;
		send_pointer(mouse_buttons, cur_x, cur_y);
	} else if (ev->type == EV_KEY && ev->value <= 2) {
		uint32_t k = keysym(ev->code);

		if (k)
			send_key(ev->value != 0, k);
	}
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char **argv)
{
	const char *host = "127.0.0.1", *fbdev = "/dev/fb0", *tap = NULL;
	int port = 5900, opt;
	time_t last_scan = 0;

	while ((opt = getopt(argc, argv, "H:p:f:t:I:")) != -1) {
		switch (opt) {
		case 'H': host = optarg; break;
		case 'p': port = atoi(optarg); break;
		case 'f': fbdev = optarg; break;
		case 't': tap = optarg; break;
		case 'I': input_glob = optarg; break;
		default:
			fprintf(stderr, "usage: %s [-H host] [-p port] [-f fbdev] [-I input-glob|none] [-t X,Y]\n", argv[0]);
			return 2;
		}
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	if (tap) {
		/* one touch at guest coordinates X,Y (scripts, tests); no framebuffer */
		int x, y;

		if (sscanf(tap, "%d,%d", &x, &y) != 2)
			return 2;
		var.bits_per_pixel = 32;
		var.red = (struct fb_bitfield){ 16, 8, 0 };
		var.green = (struct fb_bitfield){ 8, 8, 0 };
		var.blue = (struct fb_bitfield){ 0, 8, 0 };
		if (vnc_connect(host, port)) {
			fprintf(stderr, "android-hdmi: cannot connect to %s:%d\n", host, port);
			return 1;
		}
		send_pointer(1, x, y);
		usleep(100000);
		send_pointer(0, x, y);
		usleep(100000);
		printf("android-hdmi: tapped %d,%d\n", x, y);
		return 0;
	}
	if (fb_open(fbdev))
		return 1;
	printf("android-hdmi: %s %ux%u, %u bpp\n", fbdev, fb_w, fb_h, var.bits_per_pixel);

	for (;;) {
		struct pollfd pfd[MAX_INPUTS + 1];
		int i;

		if (sock < 0) {
			if (vnc_connect(host, port)) {
				sleep(2);	/* guest not started yet */
				continue;
			}
			printf("android-hdmi: connected to %s:%d\n", host, port);
			if (request_update(0))
				goto lost;
		}
		if (refresh) {
			refresh = 0;
			fb_clear();
			if (request_update(0))
				goto lost;
		}
		if (time(NULL) - last_scan >= 5) {
			scan_inputs();	/* also picks up USB devices plugged in later */
			last_scan = time(NULL);
		}

		pfd[0].fd = sock;
		pfd[0].events = POLLIN;
		for (i = 0; i < ninputs; i++) {
			pfd[i + 1].fd = inputs[i].fd;
			pfd[i + 1].events = POLLIN;
		}
		if (poll(pfd, ninputs + 1, 1000) < 0)
			continue;

		if (pfd[0].revents) {
			int r = vnc_message();

			if (r < 0 || (r == 1 && request_update(1)))
				goto lost;
		}
		for (i = 0; i < ninputs; i++) {
			struct input_event ev;

			if (pfd[i + 1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				close(inputs[i].fd);	/* unplugged */
				inputs[i] = inputs[--ninputs];
				break;
			}
			while (read(inputs[i].fd, &ev, sizeof(ev)) == sizeof(ev))
				handle_event(&inputs[i], &ev);
		}
		continue;
lost:
		printf("android-hdmi: connection lost, retrying\n");
		close(sock);
		sock = -1;
		fb_clear();
		sleep(2);
	}
}

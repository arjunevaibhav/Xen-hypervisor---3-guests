// SPDX-License-Identifier: MIT
/*
 * rear-camera: rear-view camera on a dom0 screen while the car is in reverse.
 *
 * Watches the gear frame (0x0C2, byte 0 'R' = reverse) on the vehicle CAN
 * bus (receive only). In reverse it stops the screen's normal viewer
 * (SIGSTOP), shows the USB camera (UVC, V4L2, YUYV) mirrored like a mirror
 * with parking guide lines, and plays the reverse chime (audio-mixer). One
 * second after reverse ends the viewer gets the screen back (SIGCONT) and
 * draws it again. No gear frame for 1 s also ends the camera view, so a lost
 * bus gives the cluster (and its SIGNAL LOST lamp) back. Without a camera
 * the screen shows colour bars and "NO CAMERA"; a camera plugged in later is
 * picked up.
 *
 *   rear-camera [-f /dev/fb0] [-i vcan0] [-d /dev/video0] [-s 640x480] [-r fps]
 *               [-n] [-T] [-g WxH] [-- viewer [args...]]
 *   -n: do not mirror the picture; -T: camera view always on (test, no CAN)
 *   -g WxH: draw into memory instead of a framebuffer (tests)
 *   SIGUSR1 switches the camera view on or off by hand.
 *
 * The viewer (e.g. "cluster-hdmi -f /dev/fb0") is started as a child; when it
 * exits, rear-camera exits too, so display-map restarts both.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/videodev2.h>
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
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ID_GEAR		0x0C2		/* byte 0 selector P/R/N/D */
#define HOLD_S		1.0		/* camera stays on this long after reverse */
#define GEAR_TIMEOUT_S	1.0		/* no gear frame for this long: not in reverse */
#define CHIME_FIFO	"/run/audio-mixer/chime"
#define MAX_BUFS	4

/* ---- framebuffer ------------------------------------------------------- */

static uint8_t *fb;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static int W, H, tty_fd = -1;
static pid_t viewer = -1;

static void restore_console(void)
{
	if (tty_fd >= 0) {
		ioctl(tty_fd, KDSETMODE, KD_TEXT);
		close(tty_fd);
		tty_fd = -1;
	}
}

/* Is the text console (tty1) on this framebuffer? */
static int console_on(int fd)
{
	struct fb_con2fbmap map = { .console = 1 };
	struct stat st;

	if (fstat(fd, &st) || ioctl(fd, FBIOGET_CON2FBMAP, &map))
		return 0;
	return map.framebuffer == minor(st.st_rdev);
}

static void on_signal(int sig)
{
	(void)sig;
	if (viewer > 0) {
		/* a stopped viewer only acts on SIGTERM once it runs again */
		kill(viewer, SIGTERM);
		kill(viewer, SIGCONT);
	}
	restore_console();
	_exit(0);
}

static volatile sig_atomic_t manual;

static void on_usr1(int sig)
{
	(void)sig;
	manual = !manual;
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

/* ---- 5x7 font (the letters this program writes) ------------------------ */

static const struct { char c; uint8_t r[7]; } font[] = {
	{ 'A', { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } },
	{ 'C', { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e } },
	{ 'D', { 0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c } },
	{ 'E', { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f } },
	{ 'G', { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f } },
	{ 'H', { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } },
	{ 'I', { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e } },
	{ 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
	{ 'M', { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 } },
	{ 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
	{ 'O', { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e } },
	{ 'R', { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 } },
	{ 'S', { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e } },
	{ 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e } },
	{ 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 } },
	{ 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a } },
	{ '-', { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 } },
};

static const uint8_t *glyph(char c)
{
	size_t i;

	for (i = 0; i < sizeof(font) / sizeof(font[0]); i++)
		if (font[i].c == c)
			return font[i].r;
	return NULL;	/* space: blank */
}

/* one line of text centred in a box, as large as fits */
static void text_box(int x, int y, int w, int h, uint32_t fg, uint32_t bg, const char *str)
{
	int n = (int)strlen(str), s = h / 8, tx;

	if (n && w / (6 * n) < s)
		s = w / (6 * n);
	if (s < 1)
		s = 1;
	fill(x, y, w, h, bg);
	tx = x + (w - 6 * s * n) / 2;
	y += (h - 8 * s) / 2;
	for (; *str; str++, tx += 6 * s) {
		const uint8_t *g = glyph(*str);
		int r, c;

		if (!g)
			continue;
		for (r = 0; r < 7; r++)
			for (c = 0; c < 5; c++)
				if (g[r] & (0x10 >> c))
					fill(tx + c * s, y + r * s, s, s, fg);
	}
}

/* ---- camera (V4L2, YUYV, mmap buffers) --------------------------------- */

static int cam = -1, cw, ch, cstride, mirror = 1;
static struct { void *p; size_t len; } bufs[MAX_BUFS];
static unsigned int nbufs;
/* where the picture goes on the screen (aspect ratio kept), and for each
 * screen column the camera column it shows */
static int vx, vy, vw, vh, *xmap;

static void cam_close(void)
{
	enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	unsigned int i;

	if (cam < 0)
		return;
	ioctl(cam, VIDIOC_STREAMOFF, &t);
	for (i = 0; i < nbufs; i++)
		munmap(bufs[i].p, bufs[i].len);
	nbufs = 0;
	close(cam);
	cam = -1;
}

/* picture area for a cw x ch source, centred with black bars */
static void layout(void)
{
	int x;

	if ((long)W * ch <= (long)H * cw) {
		vw = W;
		vh = (int)((long)W * ch / cw);
	} else {
		vh = H;
		vw = (int)((long)H * cw / ch);
	}
	vx = (W - vw) / 2;
	vy = (H - vh) / 2;
	for (x = 0; x < vw; x++) {
		int sx = (int)((long)x * cw / vw);

		xmap[x] = mirror ? cw - 1 - sx : sx;
	}
}

static int cam_open(const char *dev, int w, int h, int quiet)
{
	struct v4l2_format f = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
	struct v4l2_requestbuffers rb = {
		.count = MAX_BUFS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP,
	};
	enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	unsigned int i;

	cam = open(dev, O_RDWR | O_NONBLOCK);
	if (cam < 0) {
		if (!quiet)
			fprintf(stderr, "rear-camera: %s: %s\n", dev, strerror(errno));
		return -1;
	}
	f.fmt.pix.width = w;
	f.fmt.pix.height = h;
	f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	f.fmt.pix.field = V4L2_FIELD_NONE;
	if (ioctl(cam, VIDIOC_S_FMT, &f) || f.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
		fprintf(stderr, "rear-camera: %s: no YUYV format (MJPEG-only cameras are not supported)\n",
			dev);
		goto fail;
	}
	cw = f.fmt.pix.width;
	ch = f.fmt.pix.height;
	cstride = f.fmt.pix.bytesperline ? (int)f.fmt.pix.bytesperline : cw * 2;
	if (ioctl(cam, VIDIOC_REQBUFS, &rb) || rb.count < 2) {
		fprintf(stderr, "rear-camera: %s: no capture buffers\n", dev);
		goto fail;
	}
	for (i = 0; i < rb.count && i < MAX_BUFS; i++) {
		struct v4l2_buffer b = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i,
		};

		if (ioctl(cam, VIDIOC_QUERYBUF, &b))
			goto fail;
		bufs[i].len = b.length;
		bufs[i].p = mmap(NULL, b.length, PROT_READ, MAP_SHARED, cam, b.m.offset);
		if (bufs[i].p == MAP_FAILED)
			goto fail;
		nbufs++;
		if (ioctl(cam, VIDIOC_QBUF, &b))
			goto fail;
	}
	if (ioctl(cam, VIDIOC_STREAMON, &t)) {
		fprintf(stderr, "rear-camera: %s: cannot start streaming: %s\n", dev, strerror(errno));
		goto fail;
	}
	layout();
	printf("rear-camera: %s %dx%d YUYV\n", dev, cw, ch);
	return 0;
fail:
	cam_close();
	return -1;
}

static uint8_t clamp(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* one YUYV camera picture, scaled into the picture area */
static void draw_picture(const uint8_t *img)
{
	int x, y;

	for (y = 0; y < vh; y++) {
		const uint8_t *row = img + (size_t)((long)y * ch / vh) * cstride;
		uint8_t *p = fb + (size_t)(vy + y + var.yoffset) * fix.line_length +
			     (size_t)vx * (var.bits_per_pixel / 8);

		for (x = 0; x < vw; x++) {
			int sx = xmap[x];
			const uint8_t *pair = row + (sx & ~1) * 2;	/* Y0 U Y1 V */
			int c = 298 * (row[sx * 2] - 16), d = pair[1] - 128, e = pair[3] - 128;
			uint32_t px = rgb(clamp((c + 409 * e + 128) >> 8),
					  clamp((c - 100 * d - 208 * e + 128) >> 8),
					  clamp((c + 516 * d + 128) >> 8));

			if (var.bits_per_pixel == 32)
				((uint32_t *)p)[x] = px;
			else
				((uint16_t *)p)[x] = px;
		}
	}
}

/* ---- overlay ----------------------------------------------------------- */

static uint32_t c_black, c_white, c_red, c_amber, c_green;

/* parking guide lines: the car's width on the ground, red near (about
 * 0.5 m), amber, green far (about 3 m). Fixed lines: they do not follow the
 * steering. */
static void draw_guides(void)
{
	const int y_near = vy + vh * 95 / 100, y_far = vy + vh * 45 / 100;
	const int half_near = vw * 35 / 100, half_far = vw * 18 / 100, cx = vx + vw / 2;
	const int t = W / 200 + 2;
	int y;

	for (y = y_far; y <= y_near; y++) {
		int half = half_far + (half_near - half_far) * (y - y_far) / (y_near - y_far);
		int band = (y - y_far) * 3 / (y_near - y_far + 1);	/* 0 far .. 2 near */
		uint32_t c = band == 0 ? c_green : band == 1 ? c_amber : c_red;

		fill(cx - half - t / 2, y, t, 1, c);
		fill(cx + half - t / 2, y, t, 1, c);
	}
	/* cross bars at the band edges */
	for (y = 1; y <= 3; y++) {
		int yy = y_far + (y_near - y_far) * (y - 1) / 3;
		int half = half_far + (half_near - half_far) * (yy - y_far) / (y_near - y_far);

		fill(cx - half, yy, half / 3, t, y == 1 ? c_green : y == 2 ? c_amber : c_red);
		fill(cx + half - half / 3, yy, half / 3, t, y == 1 ? c_green : y == 2 ? c_amber : c_red);
	}
	text_box(0, 0, W, H / 14, c_white, c_black, "CHECK SURROUNDINGS");
}

/* colour bars and a message, when there is no camera picture */
static void draw_no_camera(void)
{
	static const uint8_t bars[7][3] = {
		{ 192, 192, 192 }, { 192, 192, 0 }, { 0, 192, 192 }, { 0, 192, 0 },
		{ 192, 0, 192 }, { 192, 0, 0 }, { 0, 0, 192 },
	};
	int i;

	for (i = 0; i < 7; i++)
		fill(W * i / 7, 0, W * (i + 1) / 7 - W * i / 7, H, rgb(bars[i][0], bars[i][1], bars[i][2]));
	text_box(W / 8, H * 2 / 5, W * 3 / 4, H / 5, c_white, c_black, "NO CAMERA");
}

/* ---- the rest ---------------------------------------------------------- */

static double since(const struct timespec *t)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - t->tv_sec) + (now.tv_nsec - t->tv_nsec) / 1e9;
}

/* ask audio-mixer for a chime; nothing happens when it is not running */
static void chime(const char *name)
{
	int fd = open(CHIME_FIFO, O_WRONLY | O_NONBLOCK);

	if (fd < 0)
		return;
	if (write(fd, name, strlen(name)) < 0 || write(fd, "\n", 1) < 0)
		perror("rear-camera: chime");
	close(fd);
}

/* the viewer has exited: so do we (display-map starts both again) */
static void check_viewer(int stopped_wait)
{
	int st;
	pid_t r;

	if (viewer <= 0)
		return;
	r = waitpid(viewer, &st, stopped_wait ? WUNTRACED : WNOHANG);
	if (r == viewer && !WIFSTOPPED(st)) {
		printf("rear-camera: viewer exited\n");
		restore_console();
		exit(1);
	}
}

int main(int argc, char **argv)
{
	const char *fbdev = "/dev/fb0", *ifname = "vcan0", *dev = "/dev/video0";
	struct sockaddr_can addr = { .can_family = AF_CAN };
	struct can_filter flt = { .can_id = ID_GEAR, .can_mask = CAN_SFF_MASK };
	struct timespec last_gear = { 0 }, last_want = { 0 }, last_try = { 0 }, last_draw = { 0 };
	int fd = -1, s, opt, want_w = 640, want_h = 480, fps = 15, test = 0, gw = 0, gh = 0;
	int reverse = 0, showing = 0, nocam_drawn = 0;

	while ((opt = getopt(argc, argv, "f:i:d:s:r:nTg:")) != -1) {
		switch (opt) {
		case 'f': fbdev = optarg; break;
		case 'i': ifname = optarg; break;
		case 'd': dev = optarg; break;
		case 's': if (sscanf(optarg, "%dx%d", &want_w, &want_h) != 2) return 2; break;
		case 'r': fps = atoi(optarg) > 0 ? atoi(optarg) : 15; break;
		case 'n': mirror = 0; break;
		case 'T': test = 1; break;
		case 'g': if (sscanf(optarg, "%dx%d", &gw, &gh) != 2) return 2; break;
		default:
			fprintf(stderr, "usage: %s [-f fbdev] [-i iface] [-d video dev] [-s WxH] [-r fps] "
				"[-n] [-T] [-g WxH] [-- viewer args...]\n", argv[0]);
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
			fprintf(stderr, "rear-camera: %s: unusable framebuffer\n", fbdev);
			return 1;
		}
		fb = mmap(NULL, fix.line_length * var.yres_virtual, PROT_READ | PROT_WRITE,
			  MAP_SHARED, fd, 0);
		if (fb == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
	}
	W = var.xres;
	H = var.yres;
	xmap = calloc(W, sizeof(*xmap));
	c_black = rgb(0, 0, 0);
	c_white = rgb(235, 238, 245);
	c_red = rgb(230, 40, 40);
	c_amber = rgb(255, 176, 0);
	c_green = rgb(40, 210, 90);

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGUSR1, on_usr1);

	if (optind < argc) {
		viewer = fork();
		if (viewer == 0) {
			signal(SIGTERM, SIG_DFL);
			signal(SIGINT, SIG_DFL);
			signal(SIGUSR1, SIG_DFL);
			execvp(argv[optind], argv + optind);
			perror(argv[optind]);
			_exit(127);
		}
	} else if (fd >= 0) {
		/* a screen of its own: keep the text console off it */
		ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
		if (console_on(fd)) {
			tty_fd = open("/dev/tty1", O_RDWR);
			if (tty_fd >= 0)
				ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
		}
		fill(0, 0, W, H, c_black);
	}

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	addr.can_ifindex = if_nametoindex(ifname);
	if (s < 0 || !addr.can_ifindex ||
	    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &flt, sizeof(flt)) ||
	    bind(s, (struct sockaddr *)&addr, sizeof(addr))) {
		fprintf(stderr, "rear-camera: %s: %s (no gear data)\n", ifname, strerror(errno));
		if (s >= 0)
			close(s);
		s = -1;
	}
	printf("rear-camera: %s %dx%d, CAN %s (receive only), camera %s%s%s\n", fbdev, W, H, ifname,
	       dev, test ? ", always on (test)" : "", viewer > 0 ? ", viewer started" : "");

	for (;;) {
		struct pollfd p[2] = { { .fd = s, .events = POLLIN }, { .fd = cam, .events = POLLIN } };
		struct can_frame f;
		int want;

		poll(p, 2, 100);	/* negative fds are skipped */

		while (s >= 0 && recv(s, &f, sizeof(f), MSG_DONTWAIT) == sizeof(f))
			if (f.can_id == ID_GEAR && f.can_dlc >= 1) {
				int r = f.data[0] == 'R';

				if (r != reverse)
					printf("rear-camera: %s reverse\n", r ? "in" : "out of");
				reverse = r;
				clock_gettime(CLOCK_MONOTONIC, &last_gear);
			}
		if (reverse && since(&last_gear) > GEAR_TIMEOUT_S) {
			printf("rear-camera: no gear frames, leaving reverse\n");
			reverse = 0;
		}
		want = test || manual || reverse;
		if (want)
			clock_gettime(CLOCK_MONOTONIC, &last_want);
		check_viewer(0);

		if (want && !showing) {
			if (viewer > 0) {
				kill(viewer, SIGSTOP);
				check_viewer(1);	/* until it has stopped drawing */
			}
			fill(0, 0, W, H, c_black);
			showing = 1;
			nocam_drawn = 0;
			if (reverse)
				chime("reverse");
			if (cam_open(dev, want_w, want_h, 0) == 0)
				fill(0, 0, W, H, c_black);
			clock_gettime(CLOCK_MONOTONIC, &last_try);
		} else if (!want && showing && since(&last_want) >= HOLD_S) {
			cam_close();
			fill(0, 0, W, H, c_black);
			showing = 0;
			if (viewer > 0)
				kill(viewer, SIGCONT);	/* it draws its screen again */
		}
		if (!showing)
			continue;

		if (cam < 0) {
			if (!nocam_drawn) {
				draw_no_camera();
				nocam_drawn = 1;
			}
			if (since(&last_try) >= 2.0) {	/* plugged in later? */
				clock_gettime(CLOCK_MONOTONIC, &last_try);
				if (cam_open(dev, want_w, want_h, 1) == 0) {
					fill(0, 0, W, H, c_black);
					nocam_drawn = 0;
				}
			}
			continue;
		}
		if (p[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			printf("rear-camera: camera gone\n");
			cam_close();
			continue;
		}
		if (p[1].revents & POLLIN) {
			struct v4l2_buffer b = {
				.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP,
			};

			if (ioctl(cam, VIDIOC_DQBUF, &b)) {
				if (errno != EAGAIN) {
					printf("rear-camera: camera gone (%s)\n", strerror(errno));
					cam_close();
				}
				continue;
			}
			if (b.index < nbufs && since(&last_draw) >= 1.0 / fps &&
			    b.bytesused >= (unsigned int)(cstride * ch)) {
				draw_picture(bufs[b.index].p);
				draw_guides();
				clock_gettime(CLOCK_MONOTONIC, &last_draw);
			}
			ioctl(cam, VIDIOC_QBUF, &b);
		}
	}
}

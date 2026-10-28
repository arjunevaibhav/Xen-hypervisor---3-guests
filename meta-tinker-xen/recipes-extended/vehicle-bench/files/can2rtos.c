// SPDX-License-Identifier: MIT
/*
 * can2rtos: read-only gateway from a CAN bus to the FreeRTOS guest.
 *
 * Receives the speed (0x0C0) and engine (0x0C1) frames, and for every speed
 * frame writes "\036V <seq> <speed 0.01 km/h> <rpm>\n" to the FreeRTOS guest's
 * console (the pty that xenconsoled gives it), which the guest's vehicle
 * task reads. The RS byte (036) tells the guest this is a frame, not keys
 * typed in "xl console rtos1"; each frame is one write(), so typed keys
 * cannot land inside it. This program never transmits on the CAN bus; for a real bus
 * also put the interface in listen-only mode (vehicle-bench does that).
 *
 *   can2rtos [-i vcan0] [-d rtos1]
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define ID_SPEED	0x0C0
#define ID_ENGINE	0x0C1

/* the guest console's pty, from xenstore: /local/domain/<id>/console/tty */
static int open_guest_console(const char *dom)
{
	char cmd[160], path[128] = "";
	struct termios t;
	FILE *p;
	int fd;

	snprintf(cmd, sizeof(cmd),
		 "xenstore-read /local/domain/$(xl domid %s 2>/dev/null)/console/tty 2>/dev/null",
		 dom);
	p = popen(cmd, "r");
	if (!p)
		return -1;
	if (!fgets(path, sizeof(path), p))
		path[0] = '\0';
	pclose(p);
	path[strcspn(path, "\r\n")] = '\0';
	if (path[0] != '/')
		return -1;

	fd = open(path, O_WRONLY | O_NOCTTY | O_NONBLOCK);
	if (fd < 0)
		return -1;
	if (tcgetattr(fd, &t) == 0) {
		cfmakeraw(&t);			/* no echo or line editing on the pty */
		tcsetattr(fd, TCSANOW, &t);
	}
	printf("can2rtos: writing to %s (%s)\n", dom, path);
	return fd;
}

int main(int argc, char **argv)
{
	const char *ifname = "vcan0", *dom = "rtos1";
	struct can_filter flt[2] = {
		{ .can_id = ID_SPEED, .can_mask = CAN_SFF_MASK },
		{ .can_id = ID_ENGINE, .can_mask = CAN_SFF_MASK },
	};
	struct sockaddr_can addr = { .can_family = AF_CAN };
	unsigned int rpm = 0, seq = 0, sent = 0;
	unsigned long dropped = 0;
	double max_gap = 0, last_write = 0, last_report = 0;
	int opt, s, con = -1;

	while ((opt = getopt(argc, argv, "i:d:")) != -1) {
		switch (opt) {
		case 'i': ifname = optarg; break;
		case 'd': dom = optarg; break;
		default:
			fprintf(stderr, "usage: %s [-i iface] [-d domain]\n", argv[0]);
			return 2;
		}
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	addr.can_ifindex = if_nametoindex(ifname);
	if (s < 0 || !addr.can_ifindex ||
	    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, flt, sizeof(flt)) ||
	    bind(s, (struct sockaddr *)&addr, sizeof(addr))) {
		perror(ifname);
		return 1;
	}
	printf("can2rtos: reading %s (receive only)\n", ifname);

	for (;;) {
		struct can_frame f;
		char line[48];
		int n;

		if (con < 0) {
			con = open_guest_console(dom);
			if (con < 0) {
				sleep(2);	/* guest not running yet */
				continue;
			}
		}
		if (read(s, &f, sizeof(f)) != sizeof(f) || f.can_dlc < 2)
			continue;
		if (f.can_id == ID_ENGINE) {
			rpm = f.data[0] << 8 | f.data[1];
			continue;
		}

		n = snprintf(line, sizeof(line), "\036V %u %u %u\n", ++seq,
			     f.data[0] << 8 | f.data[1], rpm);
		{
			/* 10 s statistics: frames passed on, largest gap between writes */
			struct timespec ts;
			double now;

			clock_gettime(CLOCK_MONOTONIC, &ts);
			now = ts.tv_sec + ts.tv_nsec / 1e9;
			if (last_write > 0 && now - last_write > max_gap)
				max_gap = now - last_write;
			last_write = now;
			if (last_report == 0)
				last_report = now;
			if (now - last_report >= 10.0) {
				printf("can2rtos: 10s: frames %u, max gap %.1f ms, dropped %lu\n",
				       sent, max_gap * 1000, dropped);
				sent = 0;
				max_gap = 0;
				last_report = now;
			}
		}
		sent++;
		if (write(con, line, n) != n) {
			if (errno == EAGAIN) {
				dropped++;	/* guest not reading: the next frame is newer anyway */
				continue;
			}
			printf("can2rtos: %s console closed (%s), reopening\n", dom, strerror(errno));
			close(con);
			con = -1;
		}
	}
}

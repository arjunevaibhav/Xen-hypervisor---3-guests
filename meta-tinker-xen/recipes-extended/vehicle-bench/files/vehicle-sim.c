// SPDX-License-Identifier: MIT
/*
 * vehicle-sim: a test vehicle on a CAN bus, for the automotive bench setup.
 *
 * Sends a repeating drive cycle (accelerate through the gears to 135 km/h,
 * cruise, brake to a stop, idle) as two CAN frames every 10 ms:
 *   0x0C0  speed  uint16 big endian, 0.01 km/h
 *   0x0C1  engine uint16 big endian, rpm
 * Only for a virtual bus (vcan0) or an isolated bench bus: never connect it
 * to a real vehicle.
 *
 *   vehicle-sim [-i vcan0] [-p period_ms]
 *   SIGUSR1 pauses/resumes sending (to test signal-loss detection).
 */

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define ID_SPEED	0x0C0
#define ID_ENGINE	0x0C1

static volatile sig_atomic_t paused;

static void on_usr1(int sig)
{
	(void)sig;
	paused = !paused;
}

/* speed in km/h at time t (seconds) of a 60 s cycle */
static double profile(double t)
{
	t = t - 60.0 * (int)(t / 60.0);
	if (t < 20.0)
		return 135.0 * t / 20.0;		/* accelerate */
	if (t < 35.0)
		return 135.0;				/* cruise above the limit */
	if (t < 50.0)
		return 135.0 * (50.0 - t) / 15.0;	/* brake */
	return 0.0;					/* stand still */
}

/* rpm from speed with a simple 6-speed gearbox; shifts late in 2nd gear */
static unsigned int engine_rpm(double kmh)
{
	static const double top[] = { 25, 70, 95, 115, 130, 1e9 };	/* gear limits */
	static const double ratio[] = { 160, 90, 62, 48, 40, 34 };	/* rpm per km/h */
	unsigned int g = 0;
	double r;

	if (kmh < 1.0)
		return 800;					/* idle */
	while (kmh > top[g])
		g++;
	r = kmh * ratio[g];
	return r < 800 ? 800 : (unsigned int)r;
}

static int send_u16(int s, unsigned int id, unsigned int v)
{
	struct can_frame f = { .can_id = id, .can_dlc = 2 };

	f.data[0] = v >> 8;
	f.data[1] = v & 0xff;
	return write(s, &f, sizeof(f)) == sizeof(f) ? 0 : -1;
}

int main(int argc, char **argv)
{
	const char *ifname = "vcan0";
	struct sockaddr_can addr = { .can_family = AF_CAN };
	struct timespec next, start;
	int period_ms = 10, opt, s;

	while ((opt = getopt(argc, argv, "i:p:")) != -1) {
		switch (opt) {
		case 'i': ifname = optarg; break;
		case 'p': period_ms = atoi(optarg); break;
		default:
			fprintf(stderr, "usage: %s [-i iface] [-p period_ms]\n", argv[0]);
			return 2;
		}
	}

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	addr.can_ifindex = if_nametoindex(ifname);
	if (s < 0 || !addr.can_ifindex ||
	    bind(s, (struct sockaddr *)&addr, sizeof(addr))) {
		perror(ifname);
		return 1;
	}
	signal(SIGUSR1, on_usr1);
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("vehicle-sim: %s, frames every %d ms (SIGUSR1 = pause)\n", ifname, period_ms);

	clock_gettime(CLOCK_MONOTONIC, &start);
	next = start;
	for (;;) {
		double t, kmh;

		next.tv_nsec += period_ms * 1000000L;
		while (next.tv_nsec >= 1000000000L) {
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		if (paused)
			continue;

		t = (next.tv_sec - start.tv_sec) + (next.tv_nsec - start.tv_nsec) / 1e9;
		kmh = profile(t);
		send_u16(s, ID_ENGINE, engine_rpm(kmh));
		send_u16(s, ID_SPEED, (unsigned int)(kmh * 100.0 + 0.5));
	}
}

// SPDX-License-Identifier: MIT
/*
 * con2fb: move a text console to another framebuffer (what fbset's
 * con2fbmap does), so dom0's console can have a screen of its own.
 *
 *   con2fb /dev/fbN [console]   (console defaults to 1, i.e. tty1)
 */

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct fb_con2fbmap map = { .console = 1 };
	struct stat st;
	int fd;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s /dev/fbN [console]\n", argv[0]);
		return 2;
	}
	if (argc == 3)
		map.console = atoi(argv[2]);
	fd = open(argv[1], O_RDWR);
	if (fd < 0 || fstat(fd, &st)) {
		perror(argv[1]);
		return 1;
	}
	map.framebuffer = minor(st.st_rdev);
	if (ioctl(fd, FBIOPUT_CON2FBMAP, &map)) {
		perror("FBIOPUT_CON2FBMAP");
		return 1;
	}
	close(fd);
	return 0;
}

/*
Copyright (c) 2015, Wolfgang Bumiller
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>          /* fprintf */
#include <stdlib.h>         /* EXIT_SUCCESS, EXIT_FAILURE */
#include <stdint.h>         /* uint16_t */
#include <string.h>         /* strcmp, strncmp, strlen */
#include <unistd.h>         /* read, write, stat */
#include <sys/types.h>      /* open, stat */
#include <sys/stat.h>       /* open, stat */
#include <fcntl.h>          /* open */
#include <sys/ioctl.h>      /* ioctl */
#include <sys/socket.h>     /* struct ifreq completion */
#include <sys/select.h>     /* select */
#include <net/if.h>         /* if_indextoname, if_nametoindex, struct ifreq */
#include <linux/if_tun.h>   /* TUN{GET,SET}IFF, IFF_{NO_PI,TAP,VNET_HDR} */

static ssize_t buffer_size = 16384;
static const char *tun_device = "/dev/net/tun";
static int iff_pi = 0;
static int iff_vnet_hdr = 0;

static int set_buffer_size(const char *arg) {
	char *endptr;

	buffer_size = strtol(arg, &endptr, 0);
	if (*endptr) {
		fprintf(stderr, "not a number: %s\n", arg);
		return 0;
	}

	return 1;
}

static int set_tun_device(const char *name) {
	if (!strlen(name)) {
		fprintf(stderr, "tunnel device name expected");
		return 0;
	}
	return 1;
}

static int setup_macvtap_device(int fd) {
	int err;
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));

	if ((err = ioctl(fd, TUNGETIFF, (void*)&ifr))) {
		perror("ioctl(TUNGETIFF)");
		return (EXIT_FAILURE);
	}

	if (iff_vnet_hdr)
		ifr.ifr_flags |= IFF_VNET_HDR;
	else
		ifr.ifr_flags &= ~(IFF_VNET_HDR);

	if ((err = ioctl(fd, TUNSETIFF, (void*)&ifr))) {
		perror("ioctl(TUNSETIFF)");
		return (EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
}

static int setup_tap_device(int fd, const char *tapname) {
	int err;
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | (iff_pi ? 0 : IFF_NO_PI);
	strncpy(ifr.ifr_name, tapname, IFNAMSIZ);

	if ((err = ioctl(fd, TUNSETIFF, (void*)&ifr))) {
		perror("ioctl");
		return (EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
}

static int pipedata(int from, int to, char *buffer, int *ret) {
	ssize_t rc, wc;
	rc = read(from, buffer, buffer_size);
	if (rc < 0) {
		perror("read");
		*ret = (EXIT_FAILURE);
		return 0;
	}
	if (rc == 0) {
		*ret = (EXIT_SUCCESS);
		return 0;
	}
	wc = write(to, buffer, rc);
	if (wc != rc) {
		perror("write");
		*ret = (EXIT_FAILURE);
		return 0;
	}
	return 1;
}

static int ifpipe(int fd) {
	char *buffer;
	int ret = (EXIT_SUCCESS);

	buffer = (char*)malloc(buffer_size);
	while (1) {
		fd_set fds;
		fd_set efds;
		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(fd, &fds);
		FD_ZERO(&efds);
		FD_SET(0, &efds);
		FD_SET(1, &efds);
		FD_SET(fd, &efds);
		if (!(ret = select(fd+1, &fds, NULL, &efds, NULL)))
			continue;
		if (ret < 0) {
			perror("select");
			ret = (EXIT_FAILURE);
			break;
		}
		ret = EXIT_SUCCESS;
		if (FD_ISSET(0, &fds)) {
			if (!pipedata(0, fd, buffer, &ret))
				break;
		}
		if (FD_ISSET(fd, &fds)) {
			if (!pipedata(fd, 1, buffer, &ret))
				break;
		}
		if (FD_ISSET(0, &efds)) {
			fprintf(stderr, "input error\n");
			ret = (EXIT_FAILURE);
			break;
		}
		if (FD_ISSET(1, &efds)) {
			fprintf(stderr, "output error\n");
			ret = (EXIT_FAILURE);
			break;
		}
		if (FD_ISSET(fd, &efds)) {
			fprintf(stderr, "interface error\n");
			ret = (EXIT_FAILURE);
			break;
		}
	}

cleanup:
	free(buffer);
	close(fd);
	return ret;
}

static int open_tap_device(const char *tapname) {
	int fd, ret;

	if (strlen(tapname) >= IFNAMSIZ) {
		fprintf(stderr, "device name too long (max is %i): %s\n",
		        (int)(IFNAMSIZ), tapname);
		return (EXIT_FAILURE);
	}

	fd = open(tun_device, O_RDWR);
	if (fd < 0) {
		perror("open");
		fprintf(stderr, "failed to open device %s\n", tun_device);
		return 1;
	}

	if ((ret = setup_tap_device(fd, tapname)) != (EXIT_SUCCESS)) {
		close(fd);
		return ret;
	}

	return ifpipe(fd);
}

static int open_macvtap_device(const char *tapdev) {
	int fd, ret;
	fd = open(tapdev, O_RDWR);
	if (fd < 0) {
		perror("open");
		return (EXIT_FAILURE);
	}

	if ((ret = setup_macvtap_device(fd)) != (EXIT_SUCCESS)) {
		close(fd);
		return ret;
	}

	return ifpipe(fd);
}

/* got a device name, see if it's a macvtap, they have a tapXY dir */
static int open_by_name_index(const char *name, unsigned int idx) {
	int fd;
	char tap[64]; /* at least /sys/class/net/<16 chars>/tap<8 chars> */
	snprintf(tap, sizeof(tap), "/sys/class/net/%s/tap%u", name, idx);
	fd = open(tap, O_RDONLY);
	if (fd < 0)
		return open_tap_device(name);
	close(fd);
	snprintf(tap, sizeof(tap), "/dev/tap%u", idx);
	return open_macvtap_device(tap);
}

/* if it's a node file, check the major/minor number, find its name.  */
static int open_by_node(const char *node) {
	char dev[64];
	struct stat stbuf;
	ssize_t got;
	unsigned int maj, min;
	unsigned long idx;
	int fd = open(node, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return (EXIT_FAILURE);
	}
	if (fstat(fd, &stbuf) != 0) {
		perror("stat");
		close(fd);
		return (EXIT_FAILURE);
	}
	close(fd);
	if (!S_ISCHR(stbuf.st_mode)) {
		fprintf(stderr, "interface name or device file expected");
		return (EXIT_FAILURE);
	}
	maj = major(stbuf.st_dev);
	min = minor(stbuf.st_dev);
	snprintf(dev, sizeof(dev), "/sys/dev/char/%u:%u/device/ifindex", maj, min);
	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return (EXIT_FAILURE);
	}
	got = read(fd, dev, sizeof(dev)-1);
	if (got <= 0) {
		perror("read");
		close(fd);
		return (EXIT_FAILURE);
	}
	close(fd);
	dev[got] = 0;
	idx = strtoul(dev, NULL, 0);
	if (!if_indextoname((unsigned int)idx, dev)) {
		perror("failed to find interface name");
		return (EXIT_FAILURE);
	}
	return open_by_name_index(dev, idx);
}

static int open_device(const char *name) {
	unsigned int idx = if_nametoindex(name);
	if (idx != 0)
		return open_by_name_index(name, idx);
	return open_by_node(name);
}

int main(int argc, char **argv) {
	int i, ret = (EXIT_FAILURE);
	FILE *help = stderr;

	for (i = 1; i != argc; ++i) {
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0)
		{
			help = stdout;
			ret = (EXIT_SUCCESS);
			break;
		}
		else if (strcmp(argv[i], "-V") == 0 ||
		         strcmp(argv[i], "--version") == 0)
		{
			fprintf(stdout, "ifpipe " IFPIPE_VERSION "\n");
			return (EXIT_SUCCESS);
		}
		else if (strcmp(argv[i], "-s") == 0) {
			if (argc <= ++i)
				break;
			if (!set_buffer_size(argv[i]))
				break;
		}
		else if (strncmp(argv[i], "-s", 2) == 0) {
			if (!set_buffer_size(argv[i]+2))
				break;
		}
		else if (strcmp(argv[i], "-d") == 0) {
			if (argc <= ++i)
				break;
			if (!set_tun_device(argv[i]));
				break;
		}
		else if (strncmp(argv[i], "-d", 2) == 0) {
			if (!set_tun_device(argv[i]+2));
				break;
		}
		else if (strcmp(argv[i], "--pi") == 0) {
			iff_pi = 1;
		}
		else if (strcmp(argv[i], "--no-pi") == 0) {
			iff_pi = 0;
		}
		else if (strcmp(argv[i], "--vnet-hdr") == 0) {
			iff_vnet_hdr = 1;
		}
		else if (strcmp(argv[i], "--no-vnet-hdr") == 0) {
			iff_vnet_hdr = 0;
		}
		else if (i+1 == argc) {
			return open_device(argv[i]);
		}
		else {
			fprintf(stderr, "unrecognized option: %s\n", argv[i]);
			break;
		}
	}

	fprintf(help, "usage: %s [options] { IFNAME | TAP_PATH }\n"
	              "options:\n"
	              "  -s BUFSIZE  packet buffer size (default: 16384)\n"
	              "  -d DEVICE   tunnel device (default: /dev/net/tun)\n"
	              "  --no-pi     [tap] set IFF_NO_PI (default)\n"
	              "  --pi        [tap] unset IFF_NO_PI\n"
	              "  --vnet      [macvtap] set IFF_VNET_HDR (default)\n"
	              "  --no-vnet   [macvtap] unset IFF_VNET_HDR\n"
	              , argv[0]);
	return ret;
}

/* vim: set ts=4 sts=4 sw=4 noet: */

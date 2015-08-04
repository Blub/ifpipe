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
#include <unistd.h>         /* read, write */
#include <sys/types.h>      /* open */
#include <sys/stat.h>       /* open */
#include <fcntl.h>          /* open */
#include <sys/ioctl.h>      /* ioctl */
#include <sys/socket.h>     /* struct ifreq completion */
#include <sys/select.h>     /* select */
#include <linux/if.h>       /* struct ifreq */
#include <linux/if_tun.h>   /* TUNSETIFF, IFF_NO_PI, IFF_TAP */

static ssize_t buffer_size = 16384;
static const char *tun_device = "/dev/net/tun";
static int iff_pi = 0;

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

static int setup_device(int fd, const char *tapname) {
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

static int ifpipe(const char *tapname) {
	int fd;
	char *buffer;
	int ret = (EXIT_SUCCESS);

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

	if ((ret = setup_device(fd, tapname)) != (EXIT_SUCCESS))
		goto cleanup;

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
		else if (i+1 == argc) {
			return ifpipe(argv[i]);
		}
		else {
			fprintf(stderr, "unrecognized option: %s\n", argv[i]);
			break;
		}
	}

	fprintf(help, "usage: %s [options] IFNAME\n"
	              "options:\n"
	              "  -s BUFSIZE  packet buffer size (default: 16384)\n"
	              "  -d DEVICE   tunnel device (default: /dev/net/tun)\n"
	              "  --no-pi     set IFF_NO_PI (default)\n"
	              "  --pi        don't set IFF_NO_PI\n"
	              , argv[0]);
	return ret;
}

/* vim: noet:
 */

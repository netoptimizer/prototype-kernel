/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 " XDP example: DDoS protection via IPv4 blacklist";

#include <linux/bpf.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <sys/resource.h>
#include <getopt.h>
#include <net/if.h>

#include <arpa/inet.h>

#include "bpf_load.h"
#include "bpf_util.h"
#include "libbpf.h"

#include "xdp_ddos01_blacklist_common.h"

static char ifname_buf[IF_NAMESIZE];
static char *ifname = NULL;
static int ifindex = -1;

static void remove_xdp_program(int ifindex, const char *ifname)
{
	fprintf(stderr, "Removing XDP program on ifindex:%d device:%s\n",
		ifindex, ifname);
	if (ifindex > -1)
		set_link_xdp_fd(ifindex, -1);
	if (unlink(file_blacklist) < 0) {
		printf("WARN: cannot remove map file:%s err(%d):%s\n",
		       file_blacklist, errno, strerror(errno));
	}
	if (unlink(file_verdict) < 0) {
		printf("WARN: cannot remove map file:%s err(%d):%s\n",
		       file_verdict, errno, strerror(errno));
	}
}

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"remove",	no_argument,		NULL, 'r' },
	{"dev",		required_argument,	NULL, 'd' },
	{0, 0, NULL,  0 }
};

static void usage(char *argv[])
{
	int i;
	printf("\nDOCUMENTATION:\n%s\n", __doc__);
	printf("\n");
	printf(" Usage: %s (options-see-below)\n",
	       argv[0]);
	printf(" Listing options:\n");
	for (i = 0; long_options[i].name != 0; i++) {
		printf(" --%-12s", long_options[i].name);
		if (long_options[i].flag != NULL)
			printf(" flag (internal value:%d)",
			       *long_options[i].flag);
		else
			printf(" short-option: -%c",
			       long_options[i].val);
		printf("\n");
	}
	printf("\n");
}

bool export_map(int fd, const char *file)
{
	int retries = 2;

retry:
	if (!retries--)
		return EXIT_FAIL_MAP;

	/* Export map as a file */
	if (bpf_obj_pin(fd, file) != 0) {
		if (errno == 17) {
			/* File exists, remove it as this bpf XDP
			 * program force-fully overwrite/swap existing
			 * XDP prog.
			 */
			printf("Delete previous map file: %s\n", file);
			if (unlink(file) < 0) {
				printf("ERR: cannot cleanup old map"
				       "file:%s err(%d):%s\n",
				       file, errno, strerror(errno));
				exit(EXIT_FAIL_MAP);
			}
			/* FIXME: shouldn't we let an existing
			 * blacklist map "survive", and feed it to the
			 * eBPF program?
			 */
			goto retry;
		} else {
			printf("ERR: Cannot pin map file:%s err(%d):%s\n",
			       file, errno, strerror(errno));
			return EXIT_FAIL_MAP;
		}
	}
	return true;
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	bool rm_xdp_prog = false;
	char filename[256];
	int longindex = 0;
	int interval = 2;
	int opt;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hd:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'r':
			rm_xdp_prog = true;
			break;
		case 'd':
			if (strlen(optarg) >= IF_NAMESIZE) {
				printf("ERR: --dev name too long\n");
				goto error;
			}
			ifname = (char *)&ifname_buf;
			strncpy(ifname, optarg, IF_NAMESIZE);
			ifindex = if_nametoindex(ifname);
			if (ifindex == 0) {
				printf("ERR: --dev name unknown err(%d):%s\n",
				       errno, strerror(errno));
				goto error;
			}
			if (verbose)
				printf("Device:%s have ifindex:%d\n",
				       ifname, ifindex);
			break;
		case 'h':
		error:
		default:
			usage(argv);
			return EXIT_FAIL_OPTION;
		}
	}
	/* Required options */
	if (ifindex == -1) {
		printf("ERR: required option --dev missing");
		usage(argv);
		return EXIT_FAIL_OPTION;
	}
	if (rm_xdp_prog) {
		remove_xdp_program(ifindex, ifname);
		return EXIT_OK;
	}

	/* Increase resource limits */
	if (setrlimit(RLIMIT_MEMLOCK, &r)) {
		perror("setrlimit(RLIMIT_MEMLOCK, RLIM_INFINITY)");
		return 1;
	}

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	if (!prog_fd[0]) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	export_map(map_fd[0], file_blacklist);
	printf("Blacklist exported to file: %s\n", file_blacklist);

	export_map(map_fd[1], file_verdict);
	printf("Verdict stats exported to file: %s\n", file_verdict);

	if (set_link_xdp_fd(ifindex, prog_fd[0]) < 0) {
		printf("link set xdp fd failed\n");
		return EXIT_FAIL_XDP;
	}

	blacklist_add(map_fd[0], "192.2.1.3");
	blacklist_add(map_fd[0], "192.2.1.3");
	sleep(10);
	blacklist_add(map_fd[0], "198.18.50.3");

	return EXIT_OK;
}

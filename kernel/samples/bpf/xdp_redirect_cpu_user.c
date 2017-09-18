/* GPLv2 Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 " XDP redirect with a CPU-map type \"BPF_MAP_TYPE_CPUMAP\" (EXPERIMENTAL)";

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <sys/resource.h>
#include <getopt.h>
#include <net/if.h>
#include <time.h>

#include <arpa/inet.h>
#include <linux/if_link.h>

/* Wanted to get rid of bpf_load.h and fake-"libbpf.h" (and instead
 * use bpf/libbpf.h), but cannot as (currently) needed for XDP
 * attaching to a device via set_link_xdp_fd()
 */
#include "libbpf.h"
#include "bpf_load.h"

static int ifindex = -1;
static char ifname_buf[IF_NAMESIZE];
static char *ifname = NULL;
static __u32 xdp_flags = 0;

/* Exit return codes */
#define EXIT_OK                 0
#define EXIT_FAIL               1
#define EXIT_FAIL_OPTION        2
#define EXIT_FAIL_XDP           3

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"dev",		required_argument,	NULL, 'd' },
	{"skb-mode", 	no_argument,		NULL, 'S' },
	{0, 0, NULL,  0 }
};

static void int_exit(int sig)
{
	fprintf(stderr,
		"Interrupted: Removing XDP program on ifindex:%d device:%s\n",
		ifindex, ifname);
// FIXME: dependency of load_bpf.h due to set_link_xdp_fd
//	if (ifindex > -1)
//		set_link_xdp_fd(ifindex, -1, xdp_flags);
	exit(EXIT_OK);
}

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

int main(int argc, char **argv)
{
	char filename[256];
	int longindex = 0;
	int opt;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hSd:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'd':
			if (strlen(optarg) >= IF_NAMESIZE) {
				fprintf(stderr, "ERR: --dev name too long\n");
				goto error;
			}
			ifname = (char *)&ifname_buf;
			strncpy(ifname, optarg, IF_NAMESIZE);
			ifindex = if_nametoindex(ifname);
			if (ifindex == 0) {
				fprintf(stderr,
					"ERR: --dev name unknown err(%d):%s\n",
					errno, strerror(errno));
				goto error;
			}
			break;
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
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
		fprintf(stderr, "ERR: required option --dev missing");
		usage(argv);
		return EXIT_FAIL_OPTION;
	}

	
	// TODO: Use libbpf instead of bpf_load.c
	

	/* Remove XDP program when program is interrupted */
	signal(SIGINT, int_exit);

	return EXIT_OK;
}

/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 " TC redirect benchmark\n\n"
 "  You must attach bpf object via TC cmdline tool\n"
;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>

#include <getopt.h>
#include <net/if.h>
#include <time.h>

#include "libbpf.h"

static int verbose = 1;
static const char *mapfile = "/sys/fs/bpf/tc/globals/egress_ifindex";

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"egress",	required_argument,	NULL, 'o' },
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

int main(int argc, char **argv)
{
	int longindex = 0, opt, fd = -1;
	int egress_ifindex = 0;
	int ret = EXIT_SUCCESS;
	int key = 0;

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "ho:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'o':
			egress_ifindex = atoi(optarg);
			break;
		case 'h':
		default:
			usage(argv);
			return EXIT_FAILURE;
		}
	}

	fd = bpf_obj_get(mapfile);
	if (fd < 0) {
		fprintf(stderr, "ERROR: cannot open bpf_obj_get(%s): %s(%d)\n",
			mapfile, strerror(errno), errno);
		ret = EXIT_FAILURE;
		goto out;
	}

	/* Only update/set egress port when set via cmdline */
	if (egress_ifindex) {
		ret = bpf_map_update_elem(fd, &key, &egress_ifindex, 0);
		if (ret) {
			perror("ERROR: bpf_map_update_elem");
			ret = EXIT_FAILURE;
			goto out;
		}
		if (verbose)
			printf("Change egress redirect ifindex to: %d\n",
			       egress_ifindex);
	} else {
		/* Read info from map */
		ret = bpf_map_lookup_elem(fd, &key, &egress_ifindex);
		if (ret) {
			perror("ERROR: bpf_map_lookup_elem");
			ret = EXIT_FAILURE;
			goto out;
		}
		if (verbose)
			printf("Current egress redirect ifindex: %d\n",
			       egress_ifindex);
	}
out:
	if (fd != -1)
		close(fd);
	return ret;
}

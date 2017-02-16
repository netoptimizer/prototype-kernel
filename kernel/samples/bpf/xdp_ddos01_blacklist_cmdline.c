/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 " XDP ddos01: command line tool";

#include <assert.h>
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
#include <time.h>

#include <arpa/inet.h>

#include "bpf_load.h"
#include "bpf_util.h"
#include "libbpf.h"

#include "xdp_ddos01_blacklist_common.h"

static const struct option long_options[] = {
	{"help",	no_argument,			NULL, 'h' },
	{"ip",		required_argument,		NULL, 's' },
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

int open_bpf_map(const char *file)
{
	int fd;

	fd = bpf_obj_get(file);
	if (!fd) {
		printf("ERR: Failed to get bpf map file:%s err(%d):%s\n",
		       file, errno, strerror(errno));
		exit(EXIT_FAIL_MAP);
	}
	return fd;
}

#define STR_MAX 42

int main(int argc, char **argv)
{
	int longindex = 0;
	int fd_blacklist;
	int fd_verdict;
	int opt;

	fd_blacklist = open_bpf_map(file_blacklist);
	fd_verdict   = open_bpf_map(file_verdict);

	while ((opt = getopt_long(argc, argv, "hs:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 's':
			printf("Blacklist add IP:%s\n", optarg);
			if (!optarg || strlen(optarg) >= STR_MAX) {
				printf("ERR: srcip too long or NULL\n");
				goto error;
			}
			blacklist_add(fd_blacklist, optarg);
			break;
		case 'h':
		error:
		default:
			usage(argv);
			return EXIT_FAIL_OPTION;
		}
	}

}

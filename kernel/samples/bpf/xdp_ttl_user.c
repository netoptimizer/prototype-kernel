/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 " XDP example of parsing TTL value of IP-header.";

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

#include "bpf_load.h"
#include "bpf_util.h"
#include "libbpf.h"

static int ifindex = -1;

static void int_exit(int sig)
{
	fprintf(stderr, "Interrupted: Removing XDP program on ifindex:%d\n",
		ifindex);
	if (ifindex > -1)
		set_link_xdp_fd(ifindex, -1);
	exit(0);
}

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"ifindex",	required_argument,	NULL, 'i' },
	{0, 0, NULL,  0 }
};

/* Exit return codes */
#define	EXIT_OK			0
#define EXIT_FAIL		1
#define EXIT_FAIL_OPTION	2
#define EXIT_FAIL_XDP		3

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

#define MAX_KEYS	256

struct ttl_stats {
	__u64 data[MAX_KEYS];
};

static bool stats_collect(struct ttl_stats *record)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	const unsigned int nr_keys = MAX_KEYS;
	__u64 values[nr_cpus];
	__u32 key;
	int i;

	for (key = 0; key < nr_keys; key++) {
		__u64 sum = 0;

		if ((bpf_map_lookup_elem(map_fd[0], &key, values)) != 0) {
			printf("DEBUG: bpf_map_lookup_elem failed\n");
			return false;
		}

		/* Sum values from each CPU */
		for (i = 0; i < nr_cpus; i++) {
			sum += values[i];
		}

		record->data[key] = sum;
	}
	return true;
}

static void stats_print_headers(void)
{
	static unsigned int i;
#define DEBUG 1
#ifdef  DEBUG
	{
	int debug_notice_interval = 3;
	char msg[] =
		"\nDebug outout avail via:\n"
		" sudo cat /sys/kernel/debug/tracing/trace_pipe\n\n";
	printf(msg, debug_notice_interval);
	}
#endif
	i++;
	printf("Stats: %d\n", i);
}

static void stats_print(struct ttl_stats *record)
{
	const unsigned int nr_keys = MAX_KEYS;
	__u64 count;
	__u32 ttl;

	/* clear screen */
	printf("\033[2J");

	stats_print_headers();
	for (ttl = 0; ttl < nr_keys; ttl++) {
		count = record->data[ttl];
		if (count)
			printf("TTL: %3d count:%llu\n", ttl, count);
	}
}

static void stats_poll(int interval)
{
	struct ttl_stats record;

	while (1) {
		memset(&record, 0, sizeof(record));

		if (stats_collect(&record))
			stats_print(&record);

		sleep(interval);
	}
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	char filename[256];
	int longindex = 0;
	int opt;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hi:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'i':
			ifindex = atoi(optarg);
			break;
		case 'h':
		default:
			usage(argv);
			return EXIT_FAIL_OPTION;
		}
	}
	/* Required options */
	if (ifindex == -1) {
		printf("**Error**: required option --ifindex missing");
		usage(argv);
		return EXIT_FAIL_OPTION;
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

	/* Remove XDP program when program is interrupted */
	signal(SIGINT, int_exit);

	if (set_link_xdp_fd(ifindex, prog_fd[0]) < 0) {
		printf("link set xdp fd failed\n");
		return EXIT_FAIL_XDP;
	}

	stats_poll(1);

	return EXIT_OK;
}

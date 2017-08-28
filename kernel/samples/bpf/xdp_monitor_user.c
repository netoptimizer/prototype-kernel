/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 " XDP monitor tool, based on tracepoints\n"
;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <locale.h>

#include <getopt.h>
#include <net/if.h>
#include <time.h>

#include "libbpf.h"
#include "bpf_load.h"
#include "bpf_util.h"

static int verbose = 1;

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"sec", 	required_argument,	NULL, 's' },
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
		printf(" --%-15s", long_options[i].name);
		if (long_options[i].flag != NULL)
			printf(" flag (internal value:%d)",
			       *long_options[i].flag);
		else
			printf("(internal short-option: -%c)",
			       long_options[i].val);
		printf("\n");
	}
	printf("\n");
}

#define NANOSEC_PER_SEC 1000000000 /* 10^9 */
uint64_t gettime(void)
{
	struct timespec t;
	int res;

	res = clock_gettime(CLOCK_MONOTONIC, &t);
	if (res < 0) {
		fprintf(stderr, "Error with gettimeofday! (%i)\n", res);
		exit(EXIT_FAILURE);
	}
	return (uint64_t) t.tv_sec * NANOSEC_PER_SEC + t.tv_nsec;
}

enum {
	REDIR_SUCCESS = 0,
	REDIR_ERROR = 1,
};
#define REDIR_RES_MAX 2
static const char *redir_names[REDIR_RES_MAX] = {
	[REDIR_SUCCESS]	= "Success",
	[REDIR_ERROR]	= "Error",
};
static const char *err2str(int err)
{
	if (err < REDIR_RES_MAX)
		return redir_names[err];
	return NULL;
}

struct record {
	__u64 counter;
	__u64 timestamp;
};

struct stats_record {
	struct record xdp_redir[REDIR_RES_MAX];
};

static void stats_print_headers(void)
{
	/* clear screen */
	// printf("\033[2J");
	printf("%-14s %-10s %-18s %-9s\n",
	       "XDP_REDIRECT", "pps ", "pps-human-readable", "period/sec");
}

static void stats_print(struct stats_record *rec,
			struct stats_record *prev)
{
	int i;

	for (i = 0; i < REDIR_RES_MAX; i++) {
		struct record *r = &rec->xdp_redir[i];
		struct record *p = &prev->xdp_redir[i];
		__u64 period  = 0;
		__u64 packets = 0;
		double pps = 0;
		double period_ = 0;

		if (p->timestamp) {
			packets = r->counter - p->counter;
			period  = r->timestamp - p->timestamp;
			if (period > 0) {
				period_ = ((double) period / NANOSEC_PER_SEC);
				pps = packets / period_;
			}
		}

		printf("%-14s %-10.0f %'-18.0f %f\n",
		       err2str(i), pps, pps, period_);
	}
}

static __u64 get_key32_value64_percpu(int fd, __u32 key)
{
	/* For percpu maps, userspace gets a value per possible CPU */
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u64 values[nr_cpus];
	__u64 sum = 0;
	int i;

	if ((bpf_map_lookup_elem(fd, &key, values)) != 0) {
		fprintf(stderr,
			"ERR: bpf_map_lookup_elem failed key:0x%X\n", key);
		return 0;
	}

	/* Sum values from each CPU */
	for (i = 0; i < nr_cpus; i++) {
		sum += values[i];
	}
	return sum;
}

static bool stats_collect(int fd, struct stats_record *rec)
{
	int i;

	for (i = 0; i < REDIR_RES_MAX; i++) {
		rec->xdp_redir[i].timestamp = gettime();
		rec->xdp_redir[i].counter = get_key32_value64_percpu(fd, i);
	}
	return true;
}

static void stats_poll(int interval)
{
	struct stats_record rec, prev;
	int map_fd;

	memset(&rec, 0, sizeof(rec));

	/* Trick to pretty printf with thousands separators use %' */
	setlocale(LC_NUMERIC, "en_US");

	/* Header */
	if (verbose)
		printf("%s", __doc__);

	/* TODO Need more advanced stats on error types */
	if (verbose)
		printf(" - Stats map: %s\n", map_data[0].name);
	map_fd = map_data[0].fd;

	stats_print_headers();
	fflush(stdout);

	while (1) {
		memcpy(&prev, &rec, sizeof(rec));
		stats_collect(map_fd, &rec);
		stats_print(&rec, &prev);
		fflush(stdout);
		sleep(interval);
	}
}

int main(int argc, char **argv)
{
	int longindex = 0, opt;
	int ret = EXIT_SUCCESS;
	char bpf_obj_file[256];
	int interval = 2;

	snprintf(bpf_obj_file, sizeof(bpf_obj_file), "%s_kern.o", argv[0]);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "h",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 's':
			interval = atoi(optarg);
			break;
		case 'h':
		default:
			usage(argv);
			return EXIT_FAILURE;
		}
	}

	if (load_bpf_file(bpf_obj_file)) {
		printf("ERROR - bpf_log_buf: %s", bpf_log_buf);
		return 1;
	}
	if (!prog_fd[0]) {
		printf("ERROR - load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	stats_poll(interval);

	return ret;
}

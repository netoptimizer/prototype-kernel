/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 " NAPI montor tool\n"
;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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
#include "napi_monitor.h" /* Shared structs between _user & _kern */

static int verbose = 1;

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"debug",	no_argument,		NULL, 'D' },
	{0, 0, NULL,  0 }
};

struct stats_record {
	struct napi_bulk_histogram napi_bulk;
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
			printf("(short-option: -%c)",
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

static bool stats_collect(struct stats_record *record)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	struct napi_bulk_histogram values[nr_cpus];
	struct napi_bulk_histogram sum = { 0 };
	__u32 key = 0;
	int i, j;

	/* Notice map is percpu: BPF_MAP_TYPE_PERCPU_ARRAY */
	if ((bpf_map_lookup_elem(map_fd[0], &key, values)) != 0) {
		fprintf(stderr, "WARN: bpf_map_lookup_elem failed\n");
		return false;
	}
	/* Sum values from each CPU */
	for (i = 0; i < nr_cpus; i++) {
		for (j = 0; j < 65; j++) {
			sum.hist[j] += values[i].hist[j];
		}
		sum.idle_task      += values[i].idle_task;
		sum.idle_task_pkts += values[i].idle_task_pkts;
		sum.ksoftirqd      += values[i].ksoftirqd;
		sum.ksoftirqd_pkts += values[i].ksoftirqd_pkts;
	}
	memcpy(record, &sum, sizeof(sum));
	return true;
}

static void stats_poll(int interval)
{
	struct stats_record rec, prev;
	__u64 prev_timestamp;
	__u64 timestamp;
	__u64 period;

	memset(&rec, 0, sizeof(rec));
	timestamp = gettime();

	/* Trick to pretty printf with thousands separators use %' */
	setlocale(LC_NUMERIC, "en_US");

	/* Header */
	// printf("Header\n");
	fflush(stdout);

	while (1) {
		unsigned long cnt;
		double period_;
		double pps;
		int i;

		sleep(interval);
		prev_timestamp = timestamp;
		memcpy(&prev, &rec, sizeof(rec));
		timestamp = gettime();

		if (!stats_collect(&rec))
			exit(EXIT_FAILURE);

		period = timestamp - prev_timestamp;
		period_ = ((double) period / NANOSEC_PER_SEC);

		printf("\nNAPI RX bulking (measurement period: %f)\n", period_);
		for (i = 0; i < 65; i++) {

			cnt = (signed long) rec.napi_bulk.hist[i]
			    - (signed long)prev.napi_bulk.hist[i];
			if (cnt) {
				pps = (cnt * i) / period_;
				printf("bulk[%02d]\t%lu\t( %'11.0f pps)\n",
				       i, cnt, pps);
			}
		}
		{ /* Watch pkt processing started from idle-task */
			double avg_idle_bulk = 0;
			unsigned long cnt2;

			cnt = (signed long) rec.napi_bulk.idle_task_pkts
			    - (signed long)prev.napi_bulk.idle_task_pkts;
			// FIXME: idle_task contains work==0
			cnt2= (signed long) rec.napi_bulk.idle_task
			    - (signed long)prev.napi_bulk.idle_task;
			pps = cnt / period_;
			if (cnt2) avg_idle_bulk = cnt / cnt2;

			printf("NAPI-from-idle,\t%lu\taverage bulk"
			       "\t%.2f\t( %'11.0f pps)\n",
			       cnt2, avg_idle_bulk, pps);
		}
		{ /* Watch pkt processing started from ksoftirqd */
			double avg_bulk = 0;
			unsigned long cnt2;

			cnt = (signed long) rec.napi_bulk.ksoftirqd_pkts
			    - (signed long)prev.napi_bulk.ksoftirqd_pkts;
			cnt2= (signed long) rec.napi_bulk.ksoftirqd
			    - (signed long)prev.napi_bulk.ksoftirqd;
			pps = cnt / period_;
			if (cnt2) avg_bulk = cnt / cnt2;

			printf("NAPI-ksoftirqd,\t%lu\taverage bulk"
			       "\t%.2f\t( %'11.0f pps)\n",
			       cnt2, avg_bulk, pps);
		}
		fflush(stdout);
	}
}

int main(int argc, char **argv)
{
	int longindex = 0, opt;
	int ret = EXIT_SUCCESS;
	char bpf_obj_file[256];
	bool debug = false;
	// size_t len;

	snprintf(bpf_obj_file, sizeof(bpf_obj_file), "%s_kern.o", argv[0]);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "h",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'D':
			debug = true;
			break;
		case 'h':
		default:
			usage(argv);
			return EXIT_FAILURE;
		}
	}

	if (load_bpf_file(bpf_obj_file)) {
		printf("%s", bpf_log_buf);
		return 1;
	}
	if (!prog_fd[0]) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	if (debug) {
		if (verbose)
			printf("Read: /sys/kernel/debug/tracing/trace_pipe\n");
		read_trace_pipe();
	}

	stats_poll(2);

	return ret;
}

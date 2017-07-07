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
	struct softirq_data softirq;
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

static bool stats_collect_napi(struct stats_record *record)
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
		for (j = 0; j < 3; j++) {
			sum.type[j].cnt       += values[i].type[j].cnt;
			sum.type[j].cnt_bulk0 += values[i].type[j].cnt_bulk0;
			sum.type[j].pkts      += values[i].type[j].pkts;
		}
	}
	memcpy(&record->napi_bulk, &sum, sizeof(sum));
	return true;
}

static bool stats_collect_softirq(struct stats_record *record)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	struct softirq_data cpu[nr_cpus];
	struct softirq_data sum = { 0 };
	__u32 key = 0;
	int i, j;

	/* Notice map is percpu: BPF_MAP_TYPE_PERCPU_ARRAY */
	if ((bpf_map_lookup_elem(map_fd[1], &key, cpu)) != 0) {
		fprintf(stderr, "WARN: bpf_map_lookup_elem failed\n");
		return false;
	}
	/* Sum values from each CPU */
	for (i = 0; i < nr_cpus; i++) {
		for (j = 0; j < SOFTIRQ_MAX; j++) {
			sum.counters[j].enter += cpu[i].counters[j].enter;
			sum.counters[j].exit  += cpu[i].counters[j].exit;
			sum.counters[j].raise += cpu[i].counters[j].raise;
		}
	}
	memcpy(&record->softirq, &sum, sizeof(sum));
	return true;
}


static inline
void stats_type(
	enum event_t event,
	struct stats_record *rec, struct stats_record *prev,
	double period)
{
	unsigned long cnt, cnt2, pkts, bulk0;
	double avg_bulk, pps;

	pkts =  (signed long) rec->napi_bulk.type[event].pkts -
		(signed long)prev->napi_bulk.type[event].pkts;
	cnt  =  (signed long) rec->napi_bulk.type[event].cnt -
		(signed long)prev->napi_bulk.type[event].cnt;
	bulk0 = (signed long) rec->napi_bulk.type[event].cnt_bulk0 -
		(signed long)prev->napi_bulk.type[event].cnt_bulk0;

	cnt2 = (cnt - bulk0); /* cnt contains work==0 */
	avg_bulk = 0;
	if (cnt2)
		avg_bulk = pkts / cnt2;

	pps = pkts / period;

	switch (event) {
	case TYPE_IDLE_TASK:
		printf("NAPI-from-idle,");
		break;
	case TYPE_SOFTIRQ:
		printf("NAPI-ksoftirqd,");
		break;
	case TYPE_VIOLATE:
		if (!rec->napi_bulk.type[event].cnt)
			return;
		printf("NAPI-violation,");
		break;
	default:
		printf("NAPI-(unknown),");
	}
	printf("\t%lu\taverage bulk\t%.2f\t( %'11.0f pps) bulk0=%lu\n",
	       cnt, avg_bulk, pps, bulk0);
}

static void stats_softirq(struct stats_record *rec, struct stats_record *prev,
			  double p)
{
	unsigned long rx_enter, rx_exit, rx_raise;

	rx_enter= (signed long) rec->softirq.counters[SOFTIRQ_NET_RX].enter
		- (signed long)prev->softirq.counters[SOFTIRQ_NET_RX].enter;
	rx_exit = (signed long) rec->softirq.counters[SOFTIRQ_NET_RX].exit
		- (signed long)prev->softirq.counters[SOFTIRQ_NET_RX].exit;
	rx_raise= (signed long) rec->softirq.counters[SOFTIRQ_NET_RX].raise
		- (signed long)prev->softirq.counters[SOFTIRQ_NET_RX].raise;
//	printf("Stats SOFTIRQ_NET_RX: enter:%lu exit:%lu raise:%lu\n",
//	       rx_enter, rx_exit, rx_raise);
	printf("SOFTIRQ_NET_RX/sec enter:%.0f/s exit:%.0f/s raise:%.0f/s\n",
	       rx_enter/p, rx_exit/p, rx_raise/p);
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

		if (!stats_collect_napi(&rec))
			exit(EXIT_FAILURE);
		if (!stats_collect_softirq(&rec))
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
		stats_type(TYPE_IDLE_TASK, &rec, &prev, period_);
		stats_type(TYPE_SOFTIRQ,   &rec, &prev, period_);
		stats_type(TYPE_VIOLATE,   &rec, &prev, period_);

		stats_softirq(&rec, &prev, period_);

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
			if (!debug_enabled()) {
				printf("ERR: Not compiled with DEBUG\n");
				exit(EXIT_FAILURE);
			}
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

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

#include "bpf_util.h"

static int ifindex = -1;
static char ifname_buf[IF_NAMESIZE];
static char *ifname = NULL;
static __u32 xdp_flags = 0;

/* Exit return codes */
#define EXIT_OK                 0
#define EXIT_FAIL               1
#define EXIT_FAIL_OPTION        2
#define EXIT_FAIL_XDP           3
#define EXIT_FAIL_BPF           4

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"dev",		required_argument,	NULL, 'd' },
	{"skb-mode", 	no_argument,		NULL, 'S' },
	{"debug",	no_argument,		NULL, 'D' },
	{"sec", 	required_argument,	NULL, 's' },
	{0, 0, NULL,  0 }
};

static void int_exit(int sig)
{
	fprintf(stderr,
		"Interrupted: Removing XDP program on ifindex:%d device:%s\n",
		ifindex, ifname);
	if (ifindex > -1)
		set_link_xdp_fd(ifindex, -1, xdp_flags);
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

/* gettime returns the current time of day in nanoseconds.
 * Cost: clock_gettime (ns) => 26ns (CLOCK_MONOTONIC)
 *       clock_gettime (ns) =>  9ns (CLOCK_MONOTONIC_COARSE)
 */
#define NANOSEC_PER_SEC 1000000000 /* 10^9 */
__u64 gettime(void)
{
	struct timespec t;
	int res;

	res = clock_gettime(CLOCK_MONOTONIC, &t);
	if (res < 0) {
		fprintf(stderr, "Error with gettimeofday! (%i)\n", res);
		exit(EXIT_FAIL);
	}
	return (__u64) t.tv_sec * NANOSEC_PER_SEC + t.tv_nsec;
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

struct record {
	__u64 counter;
	__u64 timestamp;
};
struct stats_record {
	struct record rx_cnt;
};

static void calc_pps(struct record *r, struct record *p,
		     double *pps, double *period_)
{
	__u64 period  = 0;
	__u64 packets = 0;

	if (p->timestamp) {
		packets = r->counter - p->counter;
		period  = r->timestamp - p->timestamp;
		if (period > 0) {
			*period_ = ((double) period / NANOSEC_PER_SEC);
			*pps = packets / *period_;
		}
	}
}

static void stats_print(struct stats_record *rec,
			struct stats_record *prev)
{
	double pps = 0;
	double period_ = 0;

	calc_pps(&rec->rx_cnt, &prev->rx_cnt, &pps, &period_);
	printf("%-14s %-10.0f %'-18.0f %f\n",
	       "RX-counter", pps, pps, period_);
}

static void stats_collect(struct stats_record *rec)
{
	int fd;

	fd = map_fd[1]; /* map: rx_cnt */
	rec->rx_cnt.timestamp = gettime();
	rec->rx_cnt.counter   = get_key32_value64_percpu(fd, 0);
}

static void stats_poll(int interval)
{
	struct stats_record record, prev;

	memset(&record, 0, sizeof(record));

	/* Trick to pretty printf with thousands separators use %' */
	setlocale(LC_NUMERIC, "en_US");

	/* Header */
	printf("%-14s %-10s %-18s %-9s\n",
	       "xdp", "pps ", "pps-human-readable", "period");
	while (1) {
		memcpy(&prev, &record, sizeof(record));
		stats_collect(&record);
		stats_print(&record, &prev);
		sleep(interval);
	}
}

int create_cpu_entry(u32 cpu, u32 queue_size)
{
	int ret;

	/* Add a CPU entry to map, as this allocate a cpu entry in
	 * the kernel for the cpu.
	 */
	ret = bpf_map_update_elem(map_fd[0], &cpu, &queue_size, 0);
	if (ret) {
		fprintf(stderr, "Create CPU entry failed\n");
		exit(EXIT_FAIL_BPF);
	}
	return 0;
}

int main(int argc, char **argv)
{
	char filename[256];
	bool debug = false;
	int interval = 2;
	int longindex = 0;
	int opt, ret;
	__u32 qsize;

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
		case 's':
			interval = atoi(optarg);
			break;
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
			break;
		case 'D':
			debug = true;
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

	if (load_bpf_file(filename)) {
		fprintf(stderr, "ERR in load_bpf_file(): %s", bpf_log_buf);
		return EXIT_FAIL;
	}

	if (!prog_fd[0]) {
		fprintf(stderr, "ERR: load_bpf_file: %s\n", strerror(errno));
		return EXIT_FAIL;
	}

	/* Notice: choosing he queue size is very important with the
	 * ixgbe driver, because it's driver recycling trick is
	 * dependend on pages being returned quickly.  The number of
	 * out-standing packets in the system must be less-than 2x
	 * RX-ring size.
	 */
	qsize = 128+64;
	create_cpu_entry(0, qsize);
	create_cpu_entry(1, qsize);
	create_cpu_entry(2, qsize);

	/* Remove XDP program when program is interrupted */
	signal(SIGINT, int_exit);

	if (set_link_xdp_fd(ifindex, prog_fd[0], xdp_flags) < 0) {
		fprintf(stderr, "link set xdp fd failed\n");
		return EXIT_FAIL_XDP;
	}

	if (debug) {
		printf("Debug-mode reading trace pipe (fix #define DEBUG)\n");
		read_trace_pipe();
	}

	stats_poll(interval);
	return EXIT_OK;
}

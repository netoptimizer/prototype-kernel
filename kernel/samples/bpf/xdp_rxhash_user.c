// *** DO NOT USE THIS PROGRAM ***
// Obsoleted: only kept for historical reasons

/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
" XDP rxhash: EXPERIMENTAL testing kernel XDP rxhash feature\n\n"
" This program simply test feature under development ;-)\n";

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
#include <net/if.h>
#include <time.h>

#include <arpa/inet.h>
#include <linux/if_link.h>

#include "libbpf.h"
#include "bpf_load.h"
#include "bpf_util.h"

#include "xdp_rxhash.h"

static int ifindex = -1;
static char ifname_buf[IF_NAMESIZE];
static char *ifname = NULL;
static __u32 xdp_flags = 0;

/* Exit return codes */
#define EXIT_OK                 0
#define EXIT_FAIL               1
#define EXIT_FAIL_OPTION        2
#define EXIT_FAIL_XDP           3

static void int_exit(int sig)
{
	fprintf(stderr,
		"Interrupted: Removing XDP program on ifindex:%d device:%s\n",
		ifindex, ifname);
	if (ifindex > -1)
		set_link_xdp_fd(ifindex, -1, xdp_flags);
	exit(EXIT_OK);
}

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"dev",		required_argument,	NULL, 'd' },
	{"stats",	no_argument,		NULL, 's' },
	{"sec", 	required_argument,	NULL, 's' },
	{"action", 	required_argument,	NULL, 'a' },
	{"notouch", 	no_argument,		NULL, 'n' },
	{"skbmode",     no_argument,		NULL, 'S' },
	{"debug",	no_argument,		NULL, 'D' },
	{0, 0, NULL,  0 }
};

/* Perhaps move these:   MAX defines to bpf.h ??? */
#define XDP_HASH_TYPE_L3_MAX	(1 << XDP_HASH_TYPE_L3_BITS)
#define XDP_HASH_TYPE_L4_MAX	(1 << XDP_HASH_TYPE_L4_BITS)

static const char *L3_type_names[XDP_HASH_TYPE_L3_MAX] = {
	[0]			= "Unknown",
	[XDP_HASH_TYPE_L3_IPV4]	= "IPv4",
	[XDP_HASH_TYPE_L3_IPV6]	= "IPv6",
	/* Rest is hopefully inited to zero?!? */
};
static const char *L3_type2str(unsigned int type)
{
	const char *str;

	if (type < XDP_HASH_TYPE_L3_MAX) {
		str = L3_type_names[type];
		if (str)
			return str;
	}
	return NULL;
}
static const char *L4_type_names[XDP_HASH_TYPE_L4_MAX] = {
	[0]			= "Unknown",
	[_XDP_HASH_TYPE_L4_TCP]	= "TCP",
	[_XDP_HASH_TYPE_L4_UDP]	= "UDP",
	/* Rest is hopefully inited to zero?!? */
};
static const char *L4_type2str(unsigned int type)
{
	const char *str;

	if (type < XDP_HASH_TYPE_L4_MAX) {
		str = L4_type_names[type];
		if (str)
			return str;
	}
	return NULL;
}



#define XDP_ACTION_MAX (XDP_TX + 2) /* Extra fake "rx_total" */
#define RX_TOTAL (XDP_TX + 1)
#define XDP_ACTION_MAX_STRLEN 11
static const char *xdp_action_names[XDP_ACTION_MAX] = {
	[XDP_ABORTED]	= "XDP_ABORTED",
	[XDP_DROP]	= "XDP_DROP",
	[XDP_PASS]	= "XDP_PASS",
	[XDP_TX]	= "XDP_TX",
	[RX_TOTAL]	= "rx_total",
};
static const char *action2str(int action)
{
	if (action < XDP_ACTION_MAX)
		return xdp_action_names[action];
	return NULL;
}

static bool set_xdp_action(__u64 action)
{
	__u64 value = action;
	__u32 key = 0;

	/* map_fd[2] == map(xdp_action) */
	if ((bpf_map_update_elem(map_fd[2], &key, &value, BPF_ANY)) != 0) {
		fprintf(stderr, "ERR %s(): bpf_map_update_elem failed\n",
			__func__);
		return false;
	}
	return true;
}

static int parse_xdp_action(char *action_str)
{
	size_t maxlen;
	__u64 action = -1;
	int i;

	for (i = 0; i < XDP_ACTION_MAX; i++) {
		maxlen = XDP_ACTION_MAX_STRLEN;
		if (strncmp(xdp_action_names[i], action_str, maxlen) == 0) {
			action = i;
			break;
		}
	}
	return action;
}

static void list_xdp_action(void)
{
	int i;

	printf("Available XDP (default:XDP_PASS) --action <options>\n");
	for (i = 0; i < XDP_ACTION_MAX; i++) {
		printf("\t%s\n", xdp_action_names[i]);
	}
	printf("\n");
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
	list_xdp_action();
}

struct record {
	__u64 counter;
	__u64 timestamp;
};

struct stats_record {
	struct record xdp_action[XDP_ACTION_MAX];
	struct record hash_type_L3[XDP_HASH_TYPE_L3_MAX];
	struct record hash_type_L4[XDP_HASH_TYPE_L4_MAX];
	__u64 touch_mem;
};

enum touch_mem_type {
	NO_TOUCH = 0x0ULL,
	READ_MEM = 0x1ULL,
};
static char* mem2str(enum touch_mem_type touch_mem)
{
	if (touch_mem == NO_TOUCH)
		return "no_touch";
	if (touch_mem == READ_MEM)
		return "read";
	fprintf(stderr, "ERR: Unknown memory touch type");
	exit(EXIT_FAIL);
}

static __u64 get_touch_mem(void)
{
	__u64 value;
	__u32 key = 0;

	/* map_fd[3] == map(touch_memory) */
	if ((bpf_map_lookup_elem(map_fd[3], &key, &value)) != 0) {
		fprintf(stderr, "ERR: %s(): bpf_map_lookup_elem failed\n",
			__func__);
		exit(EXIT_FAIL_XDP);
	}
	return value;
}

static bool set_touch_mem(__u64 value)
{
	__u32 key = 0;

	/* map_fd[3] == map(touch_memory) */
	if ((bpf_map_update_elem(map_fd[3], &key, &value, BPF_ANY)) != 0) {
		fprintf(stderr, "ERR: %s(): bpf_map_update_elem failed\n",
			__func__);
		return false;
	}
	return true;
}

/* gettime returns the current time of day in nanoseconds.
 * Cost: clock_gettime (ns) => 26ns (CLOCK_MONOTONIC)
 *       clock_gettime (ns) =>  9ns (CLOCK_MONOTONIC_COARSE)
 */
#define NANOSEC_PER_SEC 1000000000 /* 10^9 */
uint64_t gettime(void)
{
	struct timespec t;
	int res;

	res = clock_gettime(CLOCK_MONOTONIC, &t);
	if (res < 0) {
		fprintf(stderr, "Error with gettimeofday! (%i)\n", res);
		exit(EXIT_FAIL);
	}
	return (uint64_t) t.tv_sec * NANOSEC_PER_SEC + t.tv_nsec;
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

/* stats_hash_type */
#define	STAT_L3 1
#define	STAT_L4 0

static void stats_print_hash_type(struct stats_record *record,
				  struct stats_record *prev,
				  int stat_type)
{
	int i, max;

	/* Header */
	printf("%-14s %-10s %-18s %-9s\n",
	       stat_type ? "hash_type:L3" : "hash_type:L4",
	       "pps ", "pps-human-readable", "sample-period");

	max = stat_type ? XDP_HASH_TYPE_L3_MAX : XDP_HASH_TYPE_L4_MAX;

	for (i = 0; i < max; i++) {
		struct record *r = NULL;
		struct record *p = NULL;
		double pps = 0;
		double period_ = 0;
		const char *str;

		if (stat_type == STAT_L3) {
			r = &record->hash_type_L3[i];
			p = &prev->hash_type_L3[i];
			str = L3_type2str(i);
		} else if (stat_type == STAT_L4) {
			r = &record->hash_type_L4[i];
			p = &prev->hash_type_L4[i];
			str = L4_type2str(i);
		}
		calc_pps(r, p, &pps, &period_);

		if (str) {
			printf("%-14s %-10.0f %'-18.0f %f\n",
			       str, pps, pps, period_);
		} else {
			if (!r->counter)
				continue;
			printf("%-14d %-10.0f %'-18.0f %f\n",
			       i, pps, pps, period_);
		}
	}
	printf("\n");
}

static void stats_print_actions(struct stats_record *record,
				struct stats_record *prev)
{
	int i;

	/* Header - "xdp-action" */
	printf("%-14s %-10s %-18s %-9s\n",
	       "xdp-action    ", "pps ", "pps-human-readable", "mem");

	for (i = 0; i < XDP_ACTION_MAX; i++) {
		struct record *r = &record->xdp_action[i];
		struct record *p = &prev->xdp_action[i];
		double pps = 0;
		double period_ = 0;

		calc_pps(r, p, &pps, &period_);

		printf("%-14s %-10.0f %'-18.0f %f"
		       "  %s\n",
		       action2str(i), pps, pps, period_,
		       mem2str(record->touch_mem)
			);
	}
	printf("\n");
}

static void stats_print(struct stats_record *record,
			struct stats_record *prev)
{
	stats_print_actions  (record, prev);
	stats_print_hash_type(record, prev, STAT_L3);
	stats_print_hash_type(record, prev, STAT_L4);
}

static bool stats_collect(struct stats_record *rec)
{
	int i, fd;

	fd = map_fd[1]; /* map: verdict_cnt */
	for (i = 0; i < (XDP_ACTION_MAX - 1) ; i++) {
		rec->xdp_action[i].timestamp = gettime();
		rec->xdp_action[i].counter = get_key32_value64_percpu(fd, i);
	}
	/* Global counter */
	fd = map_fd[0]; /* map: rx_cnt */
	rec->xdp_action[RX_TOTAL].timestamp = gettime();
	rec->xdp_action[RX_TOTAL].counter = get_key32_value64_percpu(fd, 0);

	/* Collect hash_type stats */
	fd = map_fd[4]; /* map: stats_htype_L3 */
	for (i = 0; i < XDP_HASH_TYPE_L3_MAX; i++) {
		rec->hash_type_L3[i].timestamp = gettime();
		rec->hash_type_L3[i].counter = get_key32_value64_percpu(fd, i);
	}
	fd = map_fd[5]; /* map: stats_htype_L4 */
	for (i = 0; i < XDP_HASH_TYPE_L4_MAX; i++) {
		rec->hash_type_L4[i].timestamp = gettime();
		rec->hash_type_L4[i].counter = get_key32_value64_percpu(fd, i);
	}

	return true;
}

static void stats_poll(int interval)
{
	struct stats_record record, prev;

	memset(&record, 0, sizeof(record));

	/* Read current XDP touch mem setting */
	record.touch_mem   = get_touch_mem();

	/* Trick to pretty printf with thousands separators use %' */
	setlocale(LC_NUMERIC, "en_US");

	while (1) {
		memcpy(&prev, &record, sizeof(record));
		stats_collect(&record);
		stats_print(&record, &prev);
		sleep(interval);
	}
}

int main(int argc, char **argv)
{
	__u64 touch_mem = READ_MEM; /* Default: touch packet memory */
	__u64 override_action = 0; /* Default disabled */
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	char action_str_buf[XDP_ACTION_MAX_STRLEN + 1 /* for \0 */] = {};
	char *action_str = NULL;
	char filename[256];
	int longindex = 0;
	bool stats = true;
	bool debug = false;
	int interval = 1;
	int opt;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hSd:s:",
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
			/* shared: --stats && --sec */
			stats = true;
			if (optarg)
				interval = atoi(optarg);
			break;
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
			break;
		case 'a':
			action_str = (char *)&action_str_buf;
			strncpy(action_str, optarg, XDP_ACTION_MAX_STRLEN);
			break;
		case 'n':
			touch_mem = NO_TOUCH;
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

	/* Parse action string */
	if (action_str) {
		override_action = parse_xdp_action(action_str);
		if (override_action < 0) {
			fprintf(stderr, "ERR: Invalid XDP action: %s\n",
				action_str);
			usage(argv);
			list_xdp_action();
			return EXIT_FAIL_OPTION;
		}
	}

	/* Increase resource limits */
	if (setrlimit(RLIMIT_MEMLOCK, &r)) {
		perror("setrlimit(RLIMIT_MEMLOCK, RLIM_INFINITY)");
		return EXIT_FAIL;
	}

	if (load_bpf_file(filename)) {
		fprintf(stderr, "ERR in load_bpf_file(): %s", bpf_log_buf);
		return EXIT_FAIL;
	}

	if (!prog_fd[0]) {
		fprintf(stderr, "ERR: load_bpf_file: %s\n", strerror(errno));
		return EXIT_FAIL;
	}

	/* Control behavior of XDP program */
	set_xdp_action(override_action);
	set_touch_mem(touch_mem);

	/* Remove XDP program when program is interrupted or killed */
	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	if (set_link_xdp_fd(ifindex, prog_fd[0], xdp_flags) < 0) {
		fprintf(stderr, "link set xdp fd failed\n");
		return EXIT_FAIL_XDP;
	}

	if (debug) {
		printf("Debug-mode reading trace pipe (fix #define DEBUG)\n");
		read_trace_pipe();
	}

	/* Show statistics by polling */
	if (stats) {
		stats_poll(interval);
	}

	return EXIT_OK;
}

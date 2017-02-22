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
	{"help",	no_argument,		NULL, 'h' },
	{"add",		no_argument,		NULL, 'a' },
	{"del",		no_argument,		NULL, 'x' },
	{"ip",		required_argument,	NULL, 'i' },
	{"stats",	no_argument,		NULL, 's' },
	{"sec",		required_argument,	NULL, 's' },
	{"list",	no_argument,		NULL, 'l' },
	{0, 0, NULL,  0 }
};

#define XDP_ACTION_MAX (XDP_TX + 1)
#define XDP_ACTION_MAX_STRLEN 11
static const char *xdp_action_names[XDP_ACTION_MAX] = {
	[XDP_ABORTED]	= "XDP_ABORTED",
	[XDP_DROP]	= "XDP_DROP",
	[XDP_PASS]	= "XDP_PASS",
	[XDP_TX]	= "XDP_TX",
};
static const char *action2str(int action)
{
	if (action < XDP_ACTION_MAX)
		return xdp_action_names[action];
	return NULL;
}

struct record {
	__u64 counter;
	__u64 timestamp;
};

struct stats_record {
	struct record xdp_action[XDP_ACTION_MAX];
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
	if (fd < 0) {
		printf("ERR: Failed to open bpf map file:%s err(%d):%s\n",
		       file, errno, strerror(errno));
		exit(EXIT_FAIL_MAP_FILE);
	}
	return fd;
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

static void stats_print_headers(void)
{
	/* clear screen */
	printf("\033[2J");
	printf("%-12s %-10s %-18s %-9s\n",
	       "XDP_action", "pps ", "pps-human-readable", "period/sec");
}

static void stats_print(struct stats_record *record,
			struct stats_record *prev)
{
	int i;

	for (i = 0; i < XDP_ACTION_MAX; i++) {
		struct record *r = &record->xdp_action[i];
		struct record *p = &prev->xdp_action[i];
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

		printf("%-12s %-10.0f %'-18.0f %f\n",
		       action2str(i), pps, pps, period_);
	}
}

static void stats_collect(int fd, struct stats_record *rec)
{
	int i;

	for (i = 0; i < XDP_ACTION_MAX; i++) {
		rec->xdp_action[i].timestamp = gettime();
		rec->xdp_action[i].counter = get_key32_value64_percpu(fd, i);
	}
}

static void stats_poll(int interval)
{
	struct stats_record record, prev;
	int fd;

	/* TODO: Howto handle reload and clearing of maps */
	fd = open_bpf_map(file_verdict);

	memset(&record, 0, sizeof(record));

	/* Trick to pretty printf with thousands separators use %' */
	setlocale(LC_NUMERIC, "en_US");

	while (1) {
		memcpy(&prev, &record, sizeof(record));
		stats_print_headers();
		stats_collect(fd, &record);
		stats_print(&record, &prev);
		sleep(interval);
	}
	/* Not reached, but (hint) remember to close fd in other code */
	close(fd);
}

static void blacklist_print_ip(__u32 ip, __u64 count)
{
	char ip_txt[INET_ADDRSTRLEN] = {0};

	/* Convert IPv4 addresses from binary to text form */
	if (!inet_ntop(AF_INET, &ip, ip_txt, sizeof(ip_txt))) {
		fprintf(stderr,
			"ERR: Cannot convert u32 IP:0x%X to IP-txt\n", ip);
		exit(EXIT_FAIL_IP);
	}
	printf("\"%s\" : %llu\n", ip_txt, count);
}

static void blacklist_list_all(int fd)
{
	__u32 key = 0, next_key;
	__u64 value;

	printf("{\n");
	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		printf("%s", key ? "," : " ");
		key = next_key;
		value = get_key32_value64_percpu(fd, key);
		blacklist_print_ip(key, value);
	}
	printf("}\n");
}

/* Blacklist operations */
#define ACTION_ADD	(1 << 0)
#define ACTION_DEL	(1 << 1)

int main(int argc, char **argv)
{
#	define STR_MAX 42 /* For trivial input validation */
	char _ip_string_buf[STR_MAX] = {};
	char *ip_string = NULL;

	unsigned int action = 0;
	bool stats = false;
	int interval = 1;
	int fd_blacklist;
	int fd_verdict;
	int longindex = 0;
	bool do_list = false;
	int opt;

	fd_verdict = open_bpf_map(file_verdict);

	while ((opt = getopt_long(argc, argv, "adshi:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'a':
			action |= ACTION_ADD;
			break;
		case 'd':
			action |= ACTION_DEL;
			break;
		case 'i':
			printf("Blacklist IP:%s\n", optarg);
			if (!optarg || strlen(optarg) >= STR_MAX) {
				printf("ERR: src ip too long or NULL\n");
				goto fail_opt;
			}
			ip_string = (char *)&_ip_string_buf;
			strncpy(ip_string, optarg, STR_MAX);
			break;
		case 's': /* shared: --stats && --sec */
			stats = true;
			if (optarg)
				interval = atoi(optarg);
			break;
		case 'l':
			do_list = true;
			break;
		case 'h':
		fail_opt:
		default:
			usage(argv);
			return EXIT_FAIL_OPTION;
		}
	}

	/* Update blacklist */
	if (action) {
		if (!ip_string) {
			fprintf(stderr,
			  "ERR: action require type+data, e.g option --ip\n");
			goto fail_opt;
		}
		fd_blacklist = open_bpf_map(file_blacklist);
		if ((action == ACTION_ADD))
			blacklist_add(fd_blacklist, ip_string);
		else if ((action == ACTION_DEL) && ip_string)
			//blacklist_add(fd_blacklist, ip_string);
			;
		else {
			fprintf(stderr,
				"WARN: unknown action specified for IP:%s\n",
				ip_string);
		}
		close(fd_blacklist);
	}

	/* Catch non-option arguments */
	if (argv[optind] != NULL) {
		fprintf(stderr, "ERR: Unknown non-option argument: %s\n",
			argv[optind]);
		goto fail_opt;
	}

	if (do_list) {
		fd_blacklist = open_bpf_map(file_blacklist);
		blacklist_list_all(fd_blacklist);
		close(fd_blacklist);
	}

	/* Show statistics by polling */
	if (stats) {
		stats_poll(interval);
	}

	// TODO: implement stats for verdicts
	// Hack: keep it open to inspect /proc/pid/fdinfo/3
	close(fd_verdict);
}

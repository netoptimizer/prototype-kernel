/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
   Copyright(c) 2017 Andy Gospodarek, Broadcom Limited, Inc.
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

/* libbpf.h defines bpf_* function helpers for syscalls,
 * indirectly via ./tools/lib/bpf/bpf.h */
#include "libbpf.h"

#include "bpf_util.h"

#include "xdp_ddos01_blacklist_common.h"

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"add",		no_argument,		NULL, 'a' },
	{"del",		no_argument,		NULL, 'x' },
	{"ip",		required_argument,	NULL, 'i' },
	{"stats",	no_argument,		NULL, 's' },
	{"sec",		required_argument,	NULL, 's' },
	{"list",	no_argument,		NULL, 'l' },
	{"udp-dport",	required_argument,	NULL, 'u' },
	{"tcp-dport",	required_argument,	NULL, 't' },
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

static const char *xdp_proto_filter_names[DDOS_FILTER_MAX] = {
	[DDOS_FILTER_TCP]	= "TCP",
	[DDOS_FILTER_UDP]	= "UDP",
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

static void blacklist_print_ipv4(__u32 ip, __u64 count)
{
	char ip_txt[INET_ADDRSTRLEN] = {0};

	/* Convert IPv4 addresses from binary to text form */
	if (!inet_ntop(AF_INET, &ip, ip_txt, sizeof(ip_txt))) {
		fprintf(stderr,
			"ERR: Cannot convert u32 IP:0x%X to IP-txt\n", ip);
		exit(EXIT_FAIL_IP);
	}
	printf("\n \"%s\" : %llu", ip_txt, count);
}

static void blacklist_print_proto(int key, __u64 count)
{
	printf("\n\t\"%s\" : %llu", xdp_proto_filter_names[key], count);
}

static void blacklist_print_port(int key, __u32 val, int countfds[])
{
	int i;
	__u64 count;
	bool started = false;

	printf("\n \"%d\" : ", key);
	for (i = 0; i < DDOS_FILTER_MAX; i++) {
		if (val & (1 << i)) {
			printf("%s", started ? "," : "{");
			started = true;
			count = get_key32_value64_percpu(countfds[i], key);
			blacklist_print_proto(i, count);
		}
	}
	if (started)
		printf("\n }");
}

static void blacklist_list_all_ipv4(int fd)
{
	__u32 key, *prev_key = NULL;
	__u64 value;

	while (bpf_map_get_next_key(fd, prev_key, &key) == 0) {
		printf("%s", key ? "," : "" );
		value = get_key32_value64_percpu(fd, key);
		blacklist_print_ipv4(key, value);
		prev_key = &key;
	}
	printf("%s", key ? "," : "");
}

static void blacklist_list_all_ports(int portfd, int countfds[])
{
	__u32 key, *prev_key = NULL;
	__u64 value;
	bool started = false;

	/* printf("{\n"); */
	while (bpf_map_get_next_key(portfd, prev_key, &key) == 0) {
		if ((bpf_map_lookup_elem(portfd, &key, &value)) != 0) {
			fprintf(stderr,
				"ERR: bpf_map_lookup_elem(%d) failed key:0x%X\n", portfd, key);
		}

		if (value) {
			printf("%s", started ? "," : "");
			started = true;
			blacklist_print_port(key, value, countfds);
		}
		prev_key = &key;
	}
}

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
	int fd_port_blacklist;
	int fd_port_blacklist_count;
	int longindex = 0;
	bool do_list = false;
	int opt;
	int dport = 0;
	int proto = IPPROTO_TCP;
	int filter = DDOS_FILTER_TCP;

	while ((opt = getopt_long(argc, argv, "adshi:t:u:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'a':
			action |= ACTION_ADD;
			break;
		case 'x':
			action |= ACTION_DEL;
			break;
		case 'i':
			if (!optarg || strlen(optarg) >= STR_MAX) {
				printf("ERR: src ip too long or NULL\n");
				goto fail_opt;
			}
			ip_string = (char *)&_ip_string_buf;
			strncpy(ip_string, optarg, STR_MAX);
			break;
		case 'u':
			proto = IPPROTO_UDP;
			filter = DDOS_FILTER_UDP;
		case 't':
			if (optarg)
				dport = atoi(optarg);
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
	fd_verdict = open_bpf_map(file_verdict);

	/* Update blacklist */
	if (action) {
		int res = 0;

		if (!ip_string && !dport) {
			fprintf(stderr,
			  "ERR: action require type+data, e.g option --ip\n");
			goto fail_opt;
		}

		if (ip_string) {
			fd_blacklist = open_bpf_map(file_blacklist);
			res = blacklist_modify(fd_blacklist, ip_string, action);
			close(fd_blacklist);
		}

		if (dport) {
			fd_port_blacklist = open_bpf_map(file_port_blacklist);
			fd_port_blacklist_count = open_bpf_map(file_port_blacklist_count[filter]);
			res = blacklist_port_modify(fd_port_blacklist, fd_port_blacklist_count, dport, action, proto);
			close(fd_port_blacklist);
			close(fd_port_blacklist_count);
		}
		return res;
	}

	/* Catch non-option arguments */
	if (argv[optind] != NULL) {
		fprintf(stderr, "ERR: Unknown non-option argument: %s\n",
			argv[optind]);
		goto fail_opt;
	}

	if (do_list) {
		printf("{");
		int fd_port_blacklist_count_array[DDOS_FILTER_MAX];
		int i;

		fd_blacklist = open_bpf_map(file_blacklist);
		blacklist_list_all_ipv4(fd_blacklist);
		close(fd_blacklist);

		fd_port_blacklist = open_bpf_map(file_port_blacklist);
		for (i = 0; i < DDOS_FILTER_MAX; i++)
			fd_port_blacklist_count_array[i] = open_bpf_map(file_port_blacklist_count[i]);
		blacklist_list_all_ports(fd_port_blacklist, fd_port_blacklist_count_array);
		close(fd_port_blacklist);
		printf("\n}\n");
		for (i = 0; i < DDOS_FILTER_MAX; i++)
			close(fd_port_blacklist_count_array[i]);
	}

	/* Show statistics by polling */
	if (stats) {
		stats_poll(interval);
	}

	// TODO: implement stats for verdicts
	// Hack: keep it open to inspect /proc/pid/fdinfo/3
	close(fd_verdict);
}

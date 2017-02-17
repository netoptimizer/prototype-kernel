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
	if (fd < 0) {
		printf("ERR: Failed to open bpf map file:%s err(%d):%s\n",
		       file, errno, strerror(errno));
		exit(EXIT_FAIL_MAP);
	}
	return fd;
}

static void stats_print_headers(void)
{
	static unsigned int i;
#define DEBUG 1
#ifdef  DEBUG
	{
	int debug_notice_interval = 3;
	char msg[] =
		"\nDebug output available via:\n"
		" sudo cat /sys/kernel/debug/tracing/trace_pipe\n\n";
	printf(msg, debug_notice_interval);
	}
#endif
	i++;
	printf("Stats: %d\n", i);
}

struct stats_key {
	__u32 key;
	__u64 value_sum;
};

static void stats_print(struct stats_key *record)
{
	__u64 count;
	__u32 key;

	key   = record->key;
	count = record->value_sum;
	//if (count)
		printf("Key: IP-src-raw:0x%X count:%llu\n", key, count);
}
static bool stats_collect(int fd, struct stats_key *record, __u32 key)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u64 values[nr_cpus];
	__u64 sum = 0;
	int i;

	if ((bpf_map_lookup_elem(fd, &key, values)) != 0) {
		printf("DEBUG: bpf_map_lookup_elem failed\n");
		return false;
	}

	/* Sum values from each CPU */
	for (i = 0; i < nr_cpus; i++) {
		sum += values[i];
	}

	record->value_sum = sum;
	record->key = key;
	return true;
}

static void stats_poll(int fd)
{
	struct stats_key record;
	__u32 key = 0, next_key;

	/* clear screen */
	printf("\033[2J");
	stats_print_headers();

	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {

		memset(&record, 0, sizeof(record));
		if (stats_collect(fd, &record, next_key))
			stats_print(&record);

		key = next_key;
	}
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
	int opt;

	fd_blacklist = open_bpf_map(file_blacklist);
	fd_verdict   = open_bpf_map(file_verdict);

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
				goto error;
			}
			ip_string = (char *)&_ip_string_buf;
			strncpy(ip_string, optarg, STR_MAX);
			break;
		case 's': /* shared: --stats && --sec */
			stats = true;
			if (optarg)
				interval = atoi(optarg);
			break;
		case 'h':
		error:
		default:
			usage(argv);
			return EXIT_FAIL_OPTION;
		}
	}

	/* Update blacklist */
	if (ip_string) {
		if ((action == ACTION_ADD))
			blacklist_add(fd_blacklist, ip_string);
		else if ((action == ACTION_DEL) && ip_string)
			//blacklist_add(fd_blacklist, ip_string);
			;
		else {
			printf("Warning: not action specified for IP:%s\n",
				ip_string);
		}
	}

	/* TODO: Implement stats */
	while (stats) {
		stats_poll(fd_blacklist);
		sleep(interval);
	}
}

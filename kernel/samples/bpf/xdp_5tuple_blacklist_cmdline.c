/* Copyright(c) 2018 Justin Iurman
 */
static const char *__doc__=
 " XDP 5tuple: command line tool";

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

#include "xdp_5tuple_blacklist_common.h"

enum {
	ACTION_NONE = 0,
	ACTION_ADD,
	ACTION_LIST,
	ACTION_FLUSH
};

static const char *xdp_proto_filter_names[DDOS_FILTER_MAX] = {
	[DDOS_FILTER_TCP]	= "TCP",
	[DDOS_FILTER_UDP]	= "UDP",
};

#define DEFINED_PROTOCOL		1
#define DEFINED_IP_SOURCE		2
#define DEFINED_IP_DESTINATION		4
#define DEFINED_PORT_SOURCE		8
#define DEFINED_PORT_DESTINATION	16

#define DEFINED_ALL (DEFINED_PROTOCOL | DEFINED_IP_SOURCE | DEFINED_IP_DESTINATION | DEFINED_PORT_SOURCE | DEFINED_PORT_DESTINATION)

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"add",		no_argument,		NULL, 'a' },
	{"udp",		no_argument,		NULL, 'u' },
	{"tcp",		no_argument,		NULL, 't' },
	{"ips",		required_argument,	NULL, 'i' },
	{"ipd",		required_argument,	NULL, 'j' },
	{"sport",	required_argument,	NULL, 's' },
	{"dport",	required_argument,	NULL, 'd' },
	{"list",	no_argument,		NULL, 'l' },
	{"flush",	no_argument,		NULL, 'f' },
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
		exit(EXIT_FAIL_MAP_FILE);
	}
	return fd;
}

int blacklist_tuple_add(int fd, char *ip_source, char *ip_destination, int port_source, int port_destination, int protocol)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u64 values[nr_cpus];
	int res;
	struct five_tuple key_tuple = {};

	memset(values, 0, sizeof(__u64) * nr_cpus);

	/* Convert IP-string into 32-bit network byte-order value */
	res = inet_pton(AF_INET, ip_source, &(key_tuple.ip_source));
	res &= inet_pton(AF_INET, ip_destination, &(key_tuple.ip_destination));
	if (res <= 0) {
		fprintf(stderr,
			"ERR: either IPv4 \"%s\" or \"%s\" not in presentation format\n",
			ip_source, ip_destination);
		return EXIT_FAIL_IP;
	}

	if (port_source > 65535 || port_destination > 65535) {
		fprintf(stderr,
			"ERR: source port \"%d\" or destination port \"%d\" invalid\n",
			port_source, port_destination);
		return EXIT_FAIL_PORT;
	}

	/* TODO ntohl for src/dst IPs end up with no matching -> why ? */
        key_tuple.port_source = port_source;
	key_tuple.port_destination = port_destination;
	key_tuple.protocol = protocol;

	res = bpf_map_update_elem(fd, &key_tuple, values, BPF_NOEXIST);
	if (res != 0) { /* 0 == success */
		res = (protocol == IPPROTO_UDP) ? DDOS_FILTER_UDP : DDOS_FILTER_TCP;
		fprintf(stderr,
			"%s() IPsource:%s IPdest:%s sport:%d dport:%d proto:%s errno(%d/%s)",
			__func__, ip_source, ip_destination, port_source, port_destination, xdp_proto_filter_names[res], errno, strerror(errno));

		if (errno == 17) {
			fprintf(stderr, ": Already in blacklist\n");
			return EXIT_OK;
		}
		fprintf(stderr, "\n");
		return EXIT_FAIL_MAP_KEY;
	}
	if (verbose) {
		res = (protocol == IPPROTO_UDP) ? DDOS_FILTER_UDP : DDOS_FILTER_TCP;
		fprintf(stderr,
			"%s() IPsource:%s IPdest:%s sport:%d dport:%d proto:%s\n", 
			__func__, ip_source, ip_destination, port_source, port_destination, xdp_proto_filter_names[res]);
	}

	return EXIT_OK;
}

static __u64 get_value64_percpu(int fd, struct five_tuple key)
{
	/* For percpu maps, userspace gets a value per possible CPU */
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u64 values[nr_cpus];
	__u64 sum = 0;
	int i;

	if ((bpf_map_lookup_elem(fd, &key, values)) != 0) {
		fprintf(stderr,
			"ERR: bpf_map_lookup_elem failed\n");
		return 0;
	}

	/* Sum values from each CPU */
	for (i = 0; i < nr_cpus; i++) {
		sum += values[i];
	}
	return sum;
}

static void blacklist_print_tuple(struct five_tuple tuple, __u64 count)
{
	int res;
	char ip_src[INET_ADDRSTRLEN] = {0}, ip_dst[INET_ADDRSTRLEN] = {0};
	
	if (inet_ntop(AF_INET, &(tuple.ip_source), ip_src, sizeof(ip_src)) == NULL
		|| inet_ntop(AF_INET, &(tuple.ip_destination), ip_dst, sizeof(ip_dst)) == NULL) {
		fprintf(stderr, "Error while reading current 5-tuple data\n");
	}
	else {
		res = (tuple.protocol == IPPROTO_UDP) ? DDOS_FILTER_UDP : DDOS_FILTER_TCP;
		printf("(%s) <%s> <%s> <%hu> <%hu> : %llu\n", 
			xdp_proto_filter_names[res], ip_src, ip_dst, tuple.port_source, tuple.port_destination, count);
	}
}

static void blacklist_print_tuples(int fd)
{
	struct five_tuple key, next_key;
	__u64 value;

	printf("(Protocol) <IP Src> <IP Dst> <Port Src> <Port Dst> : DROP_COUNT\n\n");
	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		key = next_key;
		value = get_value64_percpu(fd, key);
		blacklist_print_tuple(key, value);
	}
}

static void blacklist_flush(int fd)
{
	int res;
	struct five_tuple key, next_key;

	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		key = next_key;
		res = bpf_map_delete_elem(fd, &key);
		if (res != 0) {
			fprintf(stderr, "Error while deleting a tuple, flushing stopped\n");
			break;
		}
	}
}

int main(int argc, char **argv)
{
	int opt;
	int longindex;
	unsigned int action = ACTION_NONE;
	int proto, sport, dport, tmp;
	__u8 defined = 0;
#define STR_MAX 16 // For trivial input validation
	char _ip_src_buf[STR_MAX] = {}, _ip_dst_buf[STR_MAX] = {};
	char *ip_src, *ip_dst;
	int fd_blacklist;

	while ((opt = getopt_long(argc, argv, "ahltufi:j:s:d:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'a':
			action = ACTION_ADD;
			break;
		case 'i':
		case 'j':
			if (!optarg || strlen(optarg) > STR_MAX) {
				printf("ERR: src and/or dst ip too long or empty\n");
				goto fail_opt;
			}

			if (opt == 'i') {
				ip_src = (char *)&_ip_src_buf;
				strncpy(ip_src, optarg, STR_MAX);
				defined |= DEFINED_IP_SOURCE;
			} else {
				ip_dst = (char *)&_ip_dst_buf;
				strncpy(ip_dst, optarg, STR_MAX);
				defined |= DEFINED_IP_DESTINATION;
			}
			break;
		case 's':
		case 'd':
			if (!optarg) {
				printf("ERR: source and/or destination port is empty\n");
				goto fail_opt;
			}

			tmp = atoi(optarg);
			if (tmp < 0 || tmp > 65535) {
				printf("ERR: source and/or destination port is invalid\n");
				goto fail_opt;
			}

			if (opt == 's') {
				sport = tmp;
				defined |= DEFINED_PORT_SOURCE;
			} else {
				dport = tmp;
				defined |= DEFINED_PORT_DESTINATION;
			}
			break;
		case 'u':
			proto = IPPROTO_UDP;
			defined |= DEFINED_PROTOCOL;
			break;
		case 't':
			proto = IPPROTO_TCP;
			defined |= DEFINED_PROTOCOL;
			break;
		case 'l':
			action = ACTION_LIST;
			break;
		case 'f':
			action = ACTION_FLUSH;
			break;
		case 'h':
		fail_opt:
		default:
			usage(argv);
			return EXIT_FAIL_OPTION;
		}
	}

	// Catch non-option arguments
	if (argv[optind] != NULL) {
		fprintf(stderr, "ERR: Unknown non-option argument: %s\n",
			argv[optind]);
		goto fail_opt;
	}

	if (action == ACTION_ADD) {
		if (defined != DEFINED_ALL) {
			fprintf(stderr,
			  "ERR: missing fields in the 5-tuple\n");
			goto fail_opt;
		}

		fd_blacklist = open_bpf_map(file_blacklist);
		tmp = blacklist_tuple_add(fd_blacklist, ip_src, ip_dst, sport, dport, proto);
		close(fd_blacklist);
		return tmp;
	}
	else if (action == ACTION_LIST) {
		fd_blacklist = open_bpf_map(file_blacklist);
		blacklist_print_tuples(fd_blacklist);
		close(fd_blacklist);
	}
	else if (action == ACTION_FLUSH) {
		fd_blacklist = open_bpf_map(file_blacklist);
		blacklist_flush(fd_blacklist);
		close(fd_blacklist);
	}

	return 0;
}

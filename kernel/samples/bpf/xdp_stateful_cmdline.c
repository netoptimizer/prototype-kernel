/* Copyright(c) 2018 Justin Iurman
 */
static const char *__doc__=
 " XDP stateful: command line tool";

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

#include "xdp_stateful_common.h"

enum {
	ACTION_NONE = 0,
	ACTION_ADD,
	ACTION_LIST,
	ACTION_LIST_RULES,
	ACTION_FLUSH
};

static const char *xdp_proto_filter_names[PROTO_FILTER_MAX] = {
	[PROTO_FILTER_TCP]	= "TCP",
	[PROTO_FILTER_UDP]	= "UDP",
	[PROTO_FILTER_OTHER]	= "Other",
};

#define DEFINED_PROTOCOL		1
#define DEFINED_IP_SOURCE		2
#define DEFINED_IP_DESTINATION		4
#define DEFINED_PORT_SOURCE		8
#define DEFINED_PORT_DESTINATION	16

#define DEFINED_3TUPLE			(DEFINED_PROTOCOL | DEFINED_IP_SOURCE | DEFINED_IP_DESTINATION)
#define DEFINED_5TUPLE 			(DEFINED_3TUPLE | DEFINED_PORT_SOURCE | DEFINED_PORT_DESTINATION)

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
	{"rules",	no_argument,		NULL, 'r' },
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

int add_3tuple(int fd, char *ip_source, char *ip_destination, int protocol)
{
	int res;
	struct three_tuple key_tuple = {};
	__u8 action = TARGET_ACCEPT;

	/* Convert IP-string into 32-bit network byte-order value */
	res = inet_pton(AF_INET, ip_source, &(key_tuple.ip_source));
	res &= inet_pton(AF_INET, ip_destination, &(key_tuple.ip_destination));
	if (res <= 0) {
		fprintf(stderr,
			"ERR: either IPv4 \"%s\" or \"%s\" not in presentation format\n",
			ip_source, ip_destination);
		return EXIT_FAIL_IP;
	}

	key_tuple.protocol = protocol;

	res = bpf_map_update_elem(fd, &key_tuple, &action, BPF_NOEXIST);
	if (res != 0) { /* 0 == success */
		res = (protocol == IPPROTO_UDP) ? PROTO_FILTER_UDP : (protocol == IPPROTO_TCP) ? PROTO_FILTER_TCP : PROTO_FILTER_OTHER;
		fprintf(stderr,
			"%s() IPsource:%s IPdest:%s proto:%s errno(%d/%s)",
			__func__, ip_source, ip_destination, xdp_proto_filter_names[res], errno, strerror(errno));

		if (errno == 17) {
			fprintf(stderr, ": Already in 3-tuples\n");
			return EXIT_OK;
		}
		fprintf(stderr, "\n");
		return EXIT_FAIL_MAP_KEY;
	}
	if (verbose) {
		res = (protocol == IPPROTO_UDP) ? PROTO_FILTER_UDP : (protocol == IPPROTO_TCP) ? PROTO_FILTER_TCP : PROTO_FILTER_OTHER;
		fprintf(stderr,
			"%s() IPsource:%s IPdest:%s proto:%s\n", 
			__func__, ip_source, ip_destination, xdp_proto_filter_names[res]);
	}

	return EXIT_OK;
}

int add_5tuple(int fd, char *ip_source, char *ip_destination, int port_source, int port_destination, int protocol)
{
	int res;
	struct five_tuple key_tuple = {};
	__u8 action = TARGET_DROP;

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

	res = bpf_map_update_elem(fd, &key_tuple, &action, BPF_NOEXIST);
	if (res != 0) { /* 0 == success */
		res = (protocol == IPPROTO_UDP) ? PROTO_FILTER_UDP : (protocol == IPPROTO_TCP) ? PROTO_FILTER_TCP : PROTO_FILTER_OTHER;
		fprintf(stderr,
			"%s() IPsource:%s IPdest:%s sport:%d dport:%d proto:%s errno(%d/%s)",
			__func__, ip_source, ip_destination, port_source, port_destination, xdp_proto_filter_names[res], errno, strerror(errno));

		if (errno == 17) {
			fprintf(stderr, ": Already in 5-tuples\n");
			return EXIT_OK;
		}
		fprintf(stderr, "\n");
		return EXIT_FAIL_MAP_KEY;
	}
	if (verbose) {
		res = (protocol == IPPROTO_UDP) ? PROTO_FILTER_UDP : (protocol == IPPROTO_TCP) ? PROTO_FILTER_TCP : PROTO_FILTER_OTHER;
		fprintf(stderr,
			"%s() IPsource:%s IPdest:%s sport:%d dport:%d proto:%s\n", 
			__func__, ip_source, ip_destination, port_source, port_destination, xdp_proto_filter_names[res]);
	}

	return EXIT_OK;
}

static void print_3tuple(struct three_tuple tuple, __u8 action)
{
	int res;
	char ip_src[INET_ADDRSTRLEN] = {0}, ip_dst[INET_ADDRSTRLEN] = {0};
	
	if (inet_ntop(AF_INET, &(tuple.ip_source), ip_src, sizeof(ip_src)) == NULL
		|| inet_ntop(AF_INET, &(tuple.ip_destination), ip_dst, sizeof(ip_dst)) == NULL) {
		fprintf(stderr, "Error while reading current 3-tuple data\n");
	}
	else {
		res = (tuple.protocol == IPPROTO_UDP) ? PROTO_FILTER_UDP : (tuple.protocol == IPPROTO_TCP) ? PROTO_FILTER_TCP : PROTO_FILTER_OTHER;
		printf("(%s) <%s> <%s> : %s\n", 
			xdp_proto_filter_names[res], ip_src, ip_dst, action == TARGET_DROP ? "XDP_DROP" : "XDP_PASS");
	}
}

static void print_5tuple(struct five_tuple tuple, __u8 action)
{
	int res;
	char ip_src[INET_ADDRSTRLEN] = {0}, ip_dst[INET_ADDRSTRLEN] = {0};
	
	if (inet_ntop(AF_INET, &(tuple.ip_source), ip_src, sizeof(ip_src)) == NULL
		|| inet_ntop(AF_INET, &(tuple.ip_destination), ip_dst, sizeof(ip_dst)) == NULL) {
		fprintf(stderr, "Error while reading current 5-tuple data\n");
	}
	else {
		res = (tuple.protocol == IPPROTO_UDP) ? PROTO_FILTER_UDP : (tuple.protocol == IPPROTO_TCP) ? PROTO_FILTER_TCP : PROTO_FILTER_OTHER;
		printf("(%s) <%s> <%s> <%hu> <%hu> : %s\n", 
			xdp_proto_filter_names[res], ip_src, ip_dst, tuple.port_source, tuple.port_destination, action == TARGET_DROP ? "XDP_DROP" : "XDP_PASS");
	}
}

static void print_conntrack(struct five_tuple tuple, struct flow_state state)
{
	int res;
	char ip_src[INET_ADDRSTRLEN] = {0}, ip_dst[INET_ADDRSTRLEN] = {0};
	
	if (inet_ntop(AF_INET, &(tuple.ip_source), ip_src, sizeof(ip_src)) == NULL
		|| inet_ntop(AF_INET, &(tuple.ip_destination), ip_dst, sizeof(ip_dst)) == NULL) {
		fprintf(stderr, "Error while reading current conntrack 5-tuple data\n");
	}
	else {
		res = (tuple.protocol == IPPROTO_UDP) ? PROTO_FILTER_UDP : (tuple.protocol == IPPROTO_TCP) ? PROTO_FILTER_TCP : PROTO_FILTER_OTHER;
		printf("(%s) <%s> <%s> <%hu> <%hu> : %llu (%llu) %u\n", 
			xdp_proto_filter_names[res], ip_src, ip_dst, tuple.port_source, tuple.port_destination, state.counter, state.timestamp, state.tcp_flags);
	}
}

static void print_3tuples(int fd)
{
	struct three_tuple key, next_key;
	__u8 action;

	printf("(Protocol) <IP Src> <IP Dst> : ACTION\n");
	printf("===============================================\n");
	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		key = next_key;
		bpf_map_lookup_elem(fd, &key, &action);
		print_3tuple(key, action);
	}
}

static void print_5tuples(int fd)
{
	struct five_tuple key, next_key;
	__u8 action;

	printf("\n(Protocol) <IP Src> <IP Dst> <Port Src> <Port Dst> : ACTION\n");
	printf("=====================================================================\n");
	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		key = next_key;
		bpf_map_lookup_elem(fd, &key, &action);
		print_5tuple(key, action);
	}
}

static void print_conntracks(int fd)
{
	struct five_tuple key, next_key;
	struct flow_state state;

	printf("\n(Protocol) <IP Src> <IP Dst> <Port Src> <Port Dst> : COUNT (TIMESTAMP) TCP_FLAGS\n");
	printf("================================================================================\n");
	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		key = next_key;
		bpf_map_lookup_elem(fd, &key, &state);
		print_conntrack(key, state);
	}
}

static void flush_3tuples(int fd)
{
	struct three_tuple key, next_key;

	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		key = next_key;
		if (bpf_map_delete_elem(fd, &key) != 0) {
			fprintf(stderr, "Error while deleting a 3-tuple, flushing stopped\n");
			break;
		}
	}
}

static void flush_5tuples(int fd)
{
	struct five_tuple key, next_key;

	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		key = next_key;
		if (bpf_map_delete_elem(fd, &key) != 0) {
			fprintf(stderr, "Error while deleting a 5-tuple, flushing stopped\n");
			break;
		}
	}
}

static void flush_conntrack(int fd)
{
	struct five_tuple key, next_key;

	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		key = next_key;
		if (bpf_map_delete_elem(fd, &key) != 0) {
			fprintf(stderr, "Error while deleting a conn track, flushing stopped\n");
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
	int fd;

	while ((opt = getopt_long(argc, argv, "ahlrtufi:j:s:d:",
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
		case 'r':
			action = ACTION_LIST_RULES;
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
		if (defined != DEFINED_3TUPLE && defined != DEFINED_5TUPLE) {
			fprintf(stderr,
			  "ERR: missing fields in the 3-tuple or 5-tuple\n");
			goto fail_opt;
		}

		if (defined == DEFINED_3TUPLE) {
			fd = open_bpf_map(file_three_tuple);
			tmp = add_3tuple(fd, ip_src, ip_dst, proto);
			tmp = add_3tuple(fd, ip_dst, ip_src, proto);
		} else {
			fd = open_bpf_map(file_five_tuple);
			tmp = add_5tuple(fd, ip_src, ip_dst, sport, dport, proto);
			tmp = add_5tuple(fd, ip_dst, ip_src, dport, sport, proto);
		}

		close(fd);
		return tmp;
	}
	else if (action == ACTION_LIST) {
		fd = open_bpf_map(file_conn_track);
		print_conntracks(fd);
		close(fd);
	}
	else if (action == ACTION_LIST_RULES) {
		fd = open_bpf_map(file_three_tuple);
		print_3tuples(fd);
		close(fd);

		fd = open_bpf_map(file_five_tuple);
		print_5tuples(fd);
		close(fd);
	}
	else if (action == ACTION_FLUSH) {
		fd = open_bpf_map(file_conn_track);
		flush_conntrack(fd);
		close(fd);

		fd = open_bpf_map(file_three_tuple);
		flush_3tuples(fd);
		close(fd);

		fd = open_bpf_map(file_five_tuple);
		flush_5tuples(fd);
		close(fd);
	}

	return 0;
}

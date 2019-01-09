#ifndef __XDP_STATEFUL_COMMON_H
#define __XDP_STATEFUL_COMMON_H

/* Exit return codes */
#define	EXIT_OK			0
#define EXIT_FAIL		1
#define EXIT_FAIL_OPTION	2
#define EXIT_FAIL_XDP		3
#define EXIT_FAIL_MAP		20
#define EXIT_FAIL_MAP_KEY	21
#define EXIT_FAIL_MAP_FILE	22
#define EXIT_FAIL_MAP_FS	23
#define EXIT_FAIL_IP		30
#define EXIT_FAIL_PORT		31

struct three_tuple {
	__u8  protocol;
	__u32 ip_source;
	__u32 ip_destination;
};

struct five_tuple {
        __u8  protocol;
	__u32 ip_source;
	__u32 ip_destination;
	__u16 port_source;
	__u16 port_destination;
};

struct flow_state {
	__u64 timestamp;
	__u8  tcp_flags;
	__u64 counter;
};

enum {
	PROTO_FILTER_TCP = 0,
	PROTO_FILTER_UDP,
	PROTO_FILTER_OTHER,
	PROTO_FILTER_MAX
};

enum {
	TARGET_DROP = 0,
	TARGET_ACCEPT,
	TARGET_MAX
};

static int verbose = 0;

/* Export eBPF map for stateful 3-tuple and 5-tuple as a file
 * Gotcha need to mount:
 *   mount -t bpf bpf /sys/fs/bpf/
 */
static const char *file_conn_track = "/sys/fs/bpf/stateful_conn_track";
static const char *file_three_tuple = "/sys/fs/bpf/stateful_three_tuple";
static const char *file_five_tuple = "/sys/fs/bpf/stateful_five_tuple";

#endif

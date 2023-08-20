#ifndef __XDP_5TUPLE_BLACKLIST_COMMON_H
#define __XDP_5TUPLE_BLACKLIST_COMMON_H

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

struct five_tuple {
        __u8  protocol;
	__u32 ip_source;
	__u32 ip_destination;
	__u16 port_source;
	__u16 port_destination;
};

enum {
	DDOS_FILTER_TCP = 0,
	DDOS_FILTER_UDP,
	DDOS_FILTER_MAX
};

static int verbose = 0;

/* Export eBPF map for 5-tuples blacklist as a file
 * Gotcha need to mount:
 *   mount -t bpf bpf /sys/fs/bpf/
 */
static const char *file_blacklist = "/sys/fs/bpf/5tuple_blacklist";

#endif

// *** DO NOT USE THIS PROGRAM ***
// Obsoleted: only kept for historical reasons

/* xdp_rxhash feature test example */
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/if_vlan.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/udp.h>

// Strange: cannot get endianness includes with LLVM
//#include <sys/types.h>
//#include <endian.h>
// Only __LITTLE_ENDIAN_BITFIELD and __BIG_ENDIAN_BITFIELD do seem to work

// HACK to make it compile
#define BPF_FUNC_xdp_rxhash 666
static unsigned long long (*bpf_xdp_rxhash)(void *ctx, __u32 new_hash,
					    __u32 type, unsigned int flags) =
	(void *) BPF_FUNC_xdp_rxhash;


#include "bpf_helpers.h"

#include "xdp_rxhash.h"

//#define DEBUG 1
#ifdef  DEBUG
/* Only use this for debug output. Notice output from bpf_trace_printk()
 * end-up in /sys/kernel/debug/tracing/trace_pipe (remember use cat)
 */
# define bpf_debug(fmt, ...)                                            \
	({							       \
		char ____fmt[] = fmt;				       \
		bpf_trace_printk(____fmt, sizeof(____fmt),	       \
				 ##__VA_ARGS__);		       \
	})
#else
//# define bpf_debug(fmt, ...) { } while (0)
# define bpf_debug(fmt, ...)
#endif

struct bpf_map_def SEC("maps") rx_cnt = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = 1,
};

#define XDP_ACTION_MAX (XDP_TX + 1)

/* Counter per XDP "action" verdict */
struct bpf_map_def SEC("maps") verdict_cnt = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = XDP_ACTION_MAX,
};

/* For changing default action */
struct bpf_map_def SEC("maps") xdp_action = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = 1,
};

struct bpf_map_def SEC("maps") touch_memory = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = 1,
};

/* Keeps stats of XDP_DROP vs XDP_PASS */
static __always_inline
void stats_action_verdict(u32 action)
{
	u64 *value;

	if (action >= XDP_ACTION_MAX)
		return;

	value = bpf_map_lookup_elem(&verdict_cnt, &action);
	if (value)
		*value += 1;
}

/* Keep stats of hash_type for L3 (e.g IPv4, IPv6) and L4 (e.g UDP, TCP)
 *
 * Two small array are sufficient, as the supported types are limited.
 * The type is stored in a 8-bit value, partitioned with 3-bits for L3
 * and 5 bits for L4.
 */
struct bpf_map_def SEC("maps") stats_htype_L3 = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = (1 << XDP_HASH_TYPE_L3_BITS),
};

struct bpf_map_def SEC("maps") stats_htype_L4 = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = (1 << XDP_HASH_TYPE_L4_BITS),
};

static __always_inline
void stats_hash_type(u32 hash_type)
{
	u64 *value;
	u32 L3, L4;

	if (hash_type > XDP_HASH_TYPE_MASK)
		return;

	L3 = XDP_HASH_TYPE_L3(hash_type);
	value = bpf_map_lookup_elem(&stats_htype_L3, &L3);
	if (value)
		*value += 1;

	/* The L4 value is shifted down to fit within array size */
	L4 = XDP_HASH_TYPE_L4(hash_type) >> XDP_HASH_TYPE_L4_SHIFT;
	value = bpf_map_lookup_elem(&stats_htype_L4, &L4);
	if (value)
		*value += 1;
}

/* Fake structure to allow compile */
struct xdp_md2 {
        __u32 data;
        __u32 data_end;
	__u32 data_meta;
	/* Below access go through struct xdp_rxq_info */
	__u32 ingress_ifindex; /* rxq->dev->ifindex */
	__u32 rx_queue_index;  /* rxq->queue_index  */
	/* NOT ACCEPTED UPSTREAM */
	__u32 rxhash;
	// Do we need rxhash_type here???
	__u32 rxhash_type;
	// Can be done as a translation, reading part of xdp_buff->flags
};

SEC("xdp_rxhash")
int  xdp_rxhash_prog(struct xdp_md2 *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	long *value;
	u32 action = XDP_PASS;
	u32 *a2;
	u32 key = 0;
	u64 *touch_mem, h = 0;
	u32 hash, hash_type;
	u32 L3, L4;
	u32 rxhash, rxhash_type;

	/* Validate packet length is minimum Eth header size */
	if (eth + 1 > data_end)
		return XDP_DROP;

	/* Direct reading of xdp_md->rxhash */
	rxhash = ctx->rxhash;
	/* Separate (direct) reading xdp_md->rxhash_type */
	rxhash_type = ctx->rxhash_type;

	/* Helper bpf_xdp_rxhash, return 64-bit value with hash_type
	 * in in upper bits.
	 */
	h = bpf_xdp_rxhash(ctx, 0, 0, BPF_F_RXHASH_GET);
	hash      = XDP_HASH(h);
	hash_type = XDP_HASH_TYPE(h);
	stats_hash_type(hash_type);

	//L3 = hash_type & XDP_HASH_TYPE_L3_MASK;
	//L4 = hash_type & XDP_HASH_TYPE_L4_MASK;
	// or
	L3 = XDP_HASH_TYPE_L3(hash_type);
	L4 = XDP_HASH_TYPE_L4(hash_type);

	bpf_debug("xdp_rxhash: hash1=%llu h:%llu hash:%u\n", rxhash, h, hash);
	bpf_debug("helper: type:%u L3:%u L4:%u\n", hash_type, L3, L4);
	bpf_debug("experiment: type:%u rxhash_type_direct:%u\n",
		  hash_type, rxhash_type);

	/* Drop all IPv4 UDP packets without even reading packet data */
	if (hash_type == (XDP_HASH_TYPE_L4_UDP + XDP_HASH_TYPE_L3_IPV4)) {
		action = XDP_ABORTED; /* Notice in --stats output */
		//goto out;
	}

	touch_mem = bpf_map_lookup_elem(&touch_memory, &key);
	if (touch_mem && (*touch_mem == 1)) {
		u16 eth_type = eth->h_proto;

		/* Avoid compiler removing this:
		 * e.g Drop non 802.3 Ethertypes
		 */
		if (ntohs(eth_type) < ETH_P_802_3_MIN)
			return XDP_DROP;
	}

	/* Count all RX packets for benchmark purpose */
	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value)
		*value += 1;

	/* Override action option: allows measure overhead of feature */
	a2 = bpf_map_lookup_elem(&xdp_action, &key);
	if (a2 && (*a2 > 0) && (*a2 < XDP_ACTION_MAX)) {
		action = *a2;
	}
//out:
	stats_action_verdict(action);
	return action;
}

char _license[] SEC("license") = "GPL";

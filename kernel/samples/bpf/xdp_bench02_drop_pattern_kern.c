/* xdp_bench02_drop_pattern */
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/if_vlan.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/udp.h>

#include "bpf_helpers.h"

struct vlan_hdr {
	__be16 h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto;
};

struct pattern {
	/* Remember: sync with _user.c */
	union {
		struct {
			__u32 type;
			__u32 arg;
		};
		__u64 raw;
	};
};

struct bpf_map_def SEC("maps") rx_cnt = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = 1,
};

struct bpf_map_def SEC("maps") xdp_pattern = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(struct pattern),
	.max_entries = 1,
};

struct bpf_map_def SEC("maps") touch_memory = {
	.type = BPF_MAP_TYPE_ARRAY,
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

/* percpu counter safe due to running under RCU and NAPI */
struct bpf_map_def SEC("maps") count = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
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

struct bpf_map_def SEC("maps") blacklist = {
	.type        = BPF_MAP_TYPE_PERCPU_HASH,
	.key_size    = sizeof(u32),
	.value_size  = sizeof(u64), /* Drop counter */
	.max_entries = 100000,
	.map_flags   = BPF_F_NO_PREALLOC,
};

/*
 * Pattern1: N-drop + N-accept
 *
 * Simulate RX-stages where packets gets grouped together before
 * dropping packets and afterwards calling network-stack via XDP_PASS.
 */
static __always_inline
u32 N_drop_N_accept(u64 N)
{
	u64 *value, val = 0;
	u32 key = 0;

	/* Use simple counter */
	value = bpf_map_lookup_elem(&count, &key);
	if (value) {
		val = *value;
		if ((val + 1) >= (N*2))
			*value = 0; /* Reset counter */
		else
			*value += 1;
	}

	if (N == 0) /* For testing PASS overhead */
		return XDP_PASS;

	if (val < N) {
		return XDP_DROP;
	} else if (val >= N && val < (N*2)) {
		return XDP_PASS;
	} else {
		return XDP_TX; /* Should not happen! */
	}
}


static __always_inline
u32 parse_ipv4(struct xdp_md *ctx, u64 l3_offset)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct iphdr *iph = data + l3_offset;
	u64 *value;
	u32 ip_src; /* type need to match map */

	/* Hint: +1 is sizeof(struct iphdr) */
	if (iph + 1 > data_end)
		return XDP_ABORTED;

	ip_src = iph->saddr; /* Extract key */

	value = bpf_map_lookup_elem(&blacklist, &ip_src);
	if (value) {
		/* Don't need __sync_fetch_and_add(); as percpu map */
		*value += 1; /* Keep a counter for drop matches */
		return XDP_DROP;
	}

	return XDP_PASS;
}

/* Parse Ethernet layer 2, extract network layer 3 offset and protocol
 *
 * Returns false on error and non-supported ether-type
 */
static __always_inline
bool parse_eth(struct ethhdr *eth, void *data_end,
	       u16 *eth_proto, u64 *l3_offset)
{
	u16 eth_type;
	u64 offset;

	offset = sizeof(*eth);
	if ((void *)eth + offset > data_end)
		return false;

	eth_type = eth->h_proto;

	/* Skip non 802.3 Ethertypes */
	if (unlikely(ntohs(eth_type) < ETH_P_802_3_MIN))
		return false;

	/* Handle VLAN tagged packet */
	if (eth_type == htons(ETH_P_8021Q) || eth_type == htons(ETH_P_8021AD)) {
		struct vlan_hdr *vlan_hdr;

		vlan_hdr = (void *)eth + offset;
		offset += sizeof(*vlan_hdr);
		if ((void *)eth + offset > data_end)
			return false;
		eth_type = vlan_hdr->h_vlan_encapsulated_proto;
	}
	/* TODO: Handle double VLAN tagged packet */

	*eth_proto = ntohs(eth_type);
	*l3_offset = offset;
	return true;
}

static __always_inline
u32 handle_eth_protocol(struct xdp_md *ctx, u16 eth_proto, u64 l3_offset)
{
	switch (eth_proto) {
	case ETH_P_IP:
		return parse_ipv4(ctx, l3_offset);
		break;
	case ETH_P_IPV6: /* Not handler for IPv6 yet*/
	case ETH_P_ARP:  /* Let OS handle ARP */
		/* Fall-through */
	default:
		return XDP_PASS; /* Not handling eth_proto */
	}
	return XDP_PASS;
}

SEC("xdp_bench02")
int xdp_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	volatile u16 eth_type;
	long *value;
	u64 offset;
	u32 action = XDP_DROP;
	u32 action2;
	struct pattern *pattern;
	u32 key = 0;
	u64 *touch_mem;

	/* Validate packet length is minimum Eth header size */
	offset = sizeof(*eth);
	if (data + offset > data_end)
		return XDP_DROP;

	/* Allow userspace to choose pattern  */
	pattern = bpf_map_lookup_elem(&xdp_pattern, &key);
	if (!pattern)
		return XDP_ABORTED;

	/* Default: Don't touch packet data, only count packets */
	touch_mem = bpf_map_lookup_elem(&touch_memory, &key);
	if (touch_mem && (*touch_mem == 1)) {
		struct ethhdr *eth = data;
		u16 eth_proto = 0;
		u64 l3_offset = 0;

		if (!(parse_eth(eth, data_end, &eth_proto, &l3_offset)))
			return XDP_PASS; /* Skip */
		action2 = handle_eth_protocol(ctx, eth_proto, l3_offset);
		if (action2 == XDP_DROP) {
			stats_action_verdict(action);
			return action2;
		}
	}

	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value)
		*value += 1;

	if (pattern->type == 1)
		action = N_drop_N_accept(pattern->arg);

	// TODO: add "always" XDP_DROP to measure baseline cost of program

	stats_action_verdict(action);
	return action;
}

char _license[] SEC("license") = "GPL";

/* xdp_bench02_drop_pattern */
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/in.h>
#include "bpf_helpers.h"

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

		eth_type = eth->h_proto;
		/* Avoid compile removing this: e.g Drop non 802.3 Ethertypes */
		if (ntohs(eth_type) < ETH_P_802_3_MIN)
			return XDP_DROP;
	}

	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value)
		*value += 1;

	if (pattern->type == 1)
		action = N_drop_N_accept(pattern->arg);

	stats_action_verdict(action);
	return action;
}

char _license[] SEC("license") = "GPL";

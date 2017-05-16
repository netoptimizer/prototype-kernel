/* xdp_rxhash feature test example */
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/if_vlan.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/udp.h>

#include "bpf_helpers.h"

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

SEC("xdp_rxhash")
int  xdp_rxhash_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	long *value;
	u32 action = XDP_PASS;
	u32 *a2;
	struct pattern *pattern;
	u32 key = 0;
	u64 *touch_mem;

	/* Validate packet length is minimum Eth header size */
	if (eth + 1 > data_end)
		return XDP_DROP;

	// TODO: Add call helper bpf_xdp_rxhash

	// TODO: Add reading of xdp_md->rxhash

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
out:
	stats_action_verdict(action);
	return action;
}

char _license[] SEC("license") = "GPL";

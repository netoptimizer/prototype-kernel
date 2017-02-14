/* 
 */
#define KBUILD_MODNAME "foo"
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>
#include "bpf_helpers.h"

struct bpf_map_def SEC("maps") rx_cnt = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = 1,
};

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

SEC("xdp_bench01")
int xdp_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	volatile u16 eth_type;
	long *value;
	u64 offset;
	u32 key = 0;
	int *action;
	u64 *touch_mem;

	/* Validate packet length is minimum Eth header size */
	offset = sizeof(*eth);
	if (data + offset > data_end)
		return XDP_DROP;

	/* Allow userspace to choose XDP_DROP or XDP_PASS */
	action = bpf_map_lookup_elem(&xdp_action, &key);
	if (!action)
		return XDP_DROP;

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

	return *action;
}

char _license[] SEC("license") = "GPL";

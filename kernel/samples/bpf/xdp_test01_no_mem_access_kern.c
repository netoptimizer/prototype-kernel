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


SEC("xdp_test01_no_mem_access")
int xdp_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	long *value;
	u64 offset;
	u32 key = 0;
	int *action;

	/* Validate packet length is minimum Eth header size */
	offset = sizeof(*eth);
	if (data + offset > data_end)
		return XDP_DROP;

	/* Allow userspace to choose XDP_DROP or XDP_PASS */
	action = bpf_map_lookup_elem(&xdp_action, &key);
	if (!action)
		return XDP_DROP;

	/* NOTICE: Don't touch packet data, only count packets */

	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value)
		*value += 1;

	return *action;
}

char _license[] SEC("license") = "GPL";

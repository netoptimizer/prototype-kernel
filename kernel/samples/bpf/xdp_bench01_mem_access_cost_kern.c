/* xdp_bench01_mem_access_cost */
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/in.h>
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

static void swap_src_dst_mac(void *data)
{
	unsigned short *p = data;
	unsigned short dst[3];

	dst[0] = p[0];
	dst[1] = p[1];
	dst[2] = p[2];
	p[0] = p[3];
	p[1] = p[4];
	p[2] = p[5];
	p[3] = dst[0];
	p[4] = dst[1];
	p[5] = dst[2];
}

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
	if (touch_mem && (*touch_mem > 0)) {

		if (*touch_mem & 1) { /* Enable via --readmem */
			struct ethhdr *eth = data;

			eth_type = eth->h_proto;
			/* Avoid compiler removing this:
			 * e.g Drop non 802.3 Ethertypes
			 */
			if (ntohs(eth_type) < ETH_P_802_3_MIN)
				return XDP_DROP;
		}

		/* If touch_mem, also swap MACs for XDP_TX.  This is
		 * needed for action XDP_TX, else HW will not TX packet
		 * (this was observed with mlx5 driver).
		 *
		 * Can also be enabled with --swapmac
		 */
		if (*action == XDP_TX || (*touch_mem & 2))
			swap_src_dst_mac(data);
	}

	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value)
		*value += 1;

	return *action;
}

char _license[] SEC("license") = "GPL";

/* Hack as libbpf require a "version" section */
#define LINUX_VERSION_CODE 1
uint32_t __version SEC("version") = LINUX_VERSION_CODE;

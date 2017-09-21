/*  XDP redirect to CPUs via cpu_map
 *
 * -=- WARNING -=- EXPERIMENTAL: featured under development!
 *
 *  GPLv2, Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

#define MAX_CPUS 12

/* Special map type that can XDP_REDIRECT frames to another CPU */
struct bpf_map_def SEC("maps") cpu_map = {
	.type		= BPF_MAP_TYPE_CPUMAP,
	.key_size	= sizeof(u32),
	.value_size	= sizeof(u32),
	.max_entries	= MAX_CPUS,
};

/* Count RX packets, as XDP bpf_prog doesn't get direct TX-success
 * feedback.  Redirect TX errors can be caught via a tracepoint.
 */
struct bpf_map_def SEC("maps") rx_cnt = {
	.type		= BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size	= sizeof(u32),
	.value_size	= sizeof(long),
	.max_entries	= 1,
};

SEC("xdp_cpu_map0")
int  xdp_prog_cpu_map0(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	u32 cpu_dest = 0;
	u32 key = 0;
	long *value;

	/* Count RX packet in map */
	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value)
		*value += 1;

	return bpf_redirect_map(&cpu_map, cpu_dest, 0);
}

SEC("xdp_cpu_map_round_robin")
int  xdp_prog_cpu_map1_rr(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	u32 cpu_dest = 0;
	u32 key = 0;
	long *value;

	// Count RX packet in map
	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value)
		*value += 1;

//	cpu_dest = (u32)(*value) % 2;
	if (*value == 42)
		cpu_dest = 1;

	if (cpu_dest >= MAX_CPUS )
		return XDP_ABORTED;

	return bpf_redirect_map(&cpu_map, cpu_dest, 0);
}

char _license[] SEC("license") = "GPL";

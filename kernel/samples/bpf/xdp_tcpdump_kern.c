/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018 Jesper Dangaard Brouer
 */
#define KBUILD_MODNAME "foo"
#include <linux/ptrace.h>
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

#define MAX_CPUS 128

struct bpf_map_def SEC("maps") perf_ring_map = {
	.type		= BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	.key_size	= sizeof(int),
	.value_size	= sizeof(u32),
	.max_entries	= MAX_CPUS,
};

char _license[] SEC("license") = "GPL";

/* Header for perf event (meta data place before pkt data) */
struct my_perf_hdr {
	u16 cookie;
	u16 pkt_len;
} __packed;

SEC("xdp_tcpdump_to_perf_ring")
int _xdp_prog0(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct my_perf_hdr hdr;

	if (data < data_end) {
		/* The XDP perf_event_output handler will use the upper 32 bits
		 * of the flags argument as a number of bytes to include of the
		 * packet payload in the event data. If the size is too big, the
		 * call to bpf_perf_event_output will fail and return -EFAULT.
		 *
		 * See bpf_xdp_event_output in net/core/filter.c.
		 *
		 * The BPF_F_CURRENT_CPU flag means that the event output fd
		 * will be indexed by the CPU number in the event map.
		 */
		u64 flags = BPF_F_CURRENT_CPU;
		u16 sample_size;

		hdr.cookie = 0x9ca9;
		hdr.pkt_len = (u16)(data_end - data);
		sample_size = hdr.pkt_len;
		flags |= (u64)sample_size << 32;

		bpf_perf_event_output(ctx, &perf_ring_map, flags,
				      &hdr, sizeof(hdr));
	}

	return XDP_PASS;
}

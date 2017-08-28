/* XDP monitor tool, based on tracepoints
 *
 *  Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 */
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

struct bpf_map_def SEC("maps") cnt_err_map = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(u64),
	.max_entries = 2,
	// TODO: have entries for all possible errno's
};

/* Tracepoint format: /sys/kernel/debug/tracing/events/xdp/xdp_redirect/format
 * Code in:                kernel/include/trace/events/xdp.h
 *
 * -=-WARNING-=-:
 *   THIS IS AFTER I CHANGED THE LAYOUT **PATCH NOT UPSTREAM***
 */
struct xdp_redirect_ctx {
	unsigned short common_type;	//	offset:0;  size:2; signed:0;
	unsigned char common_flags;	//	offset:2;  size:1; signed:0;
	unsigned char common_preempt_count;//	offset:3;  size:1; signed:0;
	int common_pid;			//	offset:4;  size:4; signed:1;

	u8 prog_tag[8];			//	offset:8;  size:8; signed:0;
	u32 act;			//	offset:16  size:4; signed:0;
	int ifindex;			//	offset:20  size:4; signed:1;
	int to_index;			//	offset:24  size:4; signed:1;
	int err;			//	offset:28  size:4; signed:1;
};					//	offset:32

enum {
	XDP_REDIRECT_SUCCESS = 0,
	XDP_REDIRECT_ERROR = 1
};

static __always_inline
int xdp_redirect_collect_stat(struct xdp_redirect_ctx *ctx)
{
	u32 key = XDP_REDIRECT_ERROR;
	int err = ctx->err;
	u64 *cnt;

	if (!err)
		key = XDP_REDIRECT_SUCCESS;

	cnt  = bpf_map_lookup_elem(&cnt_err_map, &key);
	if (!cnt)
		return 0;
	*cnt += 1;

	return 0; /* Indicate event was filtered (no further processing)*/
	// return 1; // allow normal trace points to get info, but slower
}

SEC("tracepoint/xdp/xdp_redirect_err")
int xdp_redirect_err(struct xdp_redirect_ctx *ctx)
{
	return xdp_redirect_collect_stat(ctx);
}

SEC("tracepoint/xdp/xdp_redirect")
int xdp_redirect(struct xdp_redirect_ctx *ctx)
{
	return xdp_redirect_collect_stat(ctx);
}


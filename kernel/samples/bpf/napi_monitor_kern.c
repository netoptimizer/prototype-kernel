/* NAPI monitor tool
 *
 *  Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 */
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

#include <linux/netdevice.h> /* Kernel internal: struct napi_struct */

char _license[] SEC("license") = "GPL";

/* Shared struct between _user & _kern */
struct napi_bulk_histogram {
	/* Keep counters per possible RX bulk value */
	unsigned long hist[65];
};

/* Keep system global map (mostly because extracting the ifindex, was
 * not straight forward)
 */
struct bpf_map_def SEC("maps") napi_hist_map = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(struct napi_bulk_histogram),
	.max_entries = 1,
};

/* Tracepoint format: /sys/kernel/debug/tracing/events/napi/napi_poll/format
 * Code in:                kernel/include/trace/events/napi.h
 */
struct napi_poll_ctx {
	/* Tracepoint common fields */
	unsigned short common_type;	//	offset:0;  size:2; signed:0;
	unsigned char common_flags;	//	offset:2;  size:1; signed:0;
	unsigned char common_preempt_count;//	offset:3;  size:1; signed:0;
	int common_pid;			//	offset:4;  size:4; signed:1;

	/* Tracepoint specific fields */
	struct napi_struct *napi;	//	offset:8;  size:8; signed:0;
	int data_loc_dev_name;		//	offset:16; size:4; signed:1;
	int work;			//	offset:20; size:4; signed:1;
	int budget;			//	offset:24; size:4; signed:1;
};

#define DEBUG 1
#ifdef  DEBUG
/* Only use this for debug output. Notice output from bpf_trace_printk()
 * end-up in /sys/kernel/debug/tracing/trace_pipe
 */
#define bpf_debug(fmt, ...)						\
		({							\
			char ____fmt[] = fmt;				\
			bpf_trace_printk(____fmt, sizeof(____fmt),	\
				     ##__VA_ARGS__);			\
		})
#else
#define bpf_debug(fmt, ...) { } while (0)
#endif

SEC("tracepoint/napi/napi_poll")
int napi_poll(struct napi_poll_ctx *ctx)
{
	unsigned int budget = ctx->budget;
	unsigned int work = ctx->work;
	struct napi_struct *napi = ctx->napi;
	int ifindex = 0;
	u32 key = 0;

	unsigned long	state = 0;
	unsigned int	napi_id = 0;

	struct napi_bulk_histogram *napi_work;

	napi_work = bpf_map_lookup_elem(&napi_hist_map, &key);
	if (!napi_work)
		return 0;

	// Cannot deref napi pointer directly :-(
	//if (ctx->napi->dev)
	//	ifindex = ctx->napi->dev->ifindex;
	//
	// TODO: look at using bpf_probe_read
	//  bpf_probe_read(napi,     sizeof(napi),    ctx->napi);
	//  bpf_probe_read(&ifindex, sizeof(ifindex), dev->ifindex);
	//  bpf_probe_read(&ifindex, 4, &ctx->napi->dev->ifindex);
	if (napi) {
		bpf_probe_read(&napi_id, sizeof(napi_id), &napi->napi_id);
	}
//	if (napi && napi->dev) {
//              (NOT WORKING:)
//		bpf_probe_read(&ifindex, sizeof(ifindex), &(napi->dev->ifindex));
//	}

	 /* Detect API violation, in DEBUG state */
	if (work > budget)
		bpf_debug("API violation ifindex(%d) work(%d)>budget(%d)",
			  ifindex, work, budget);

	if (work < 65)
		napi_work->hist[work]++;

	return 0;
}

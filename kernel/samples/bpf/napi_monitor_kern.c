/* NAPI monitor tool
 *
 *  Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 */
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"
#include <linux/netdevice.h> /* Kernel internal: struct napi_struct */
#include "napi_monitor.h" /* Shared structs between _user & _kern */

/* Keep system global map (mostly because extracting the ifindex, was
 * not straight forward)
 */
struct bpf_map_def SEC("maps") napi_hist_map = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(struct napi_bulk_histogram),
	.max_entries = 1,
};

struct bpf_map_def SEC("maps") softirq_map = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(unsigned int),
	.value_size = sizeof(struct softirq_data),
	.max_entries = 1,
};

struct bpf_map_def SEC("maps") cnt_map = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(u64),
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
	unsigned int data_loc_dev_name;	//	offset:16; size:4; signed:1;
	int work;			//	offset:20; size:4; signed:1;
	int budget;			//	offset:24; size:4; signed:1;
};

/* DATA_LOC_READ not working for some reason?!? */
#define DATA_LOC_READ(ctx, dst, field, max_len)				\
do {									\
	unsigned short __offset = ctx->data_loc_##field & 0xFFFF;	\
	unsigned short __length = ctx->data_loc_##field >> 16;		\
	if (__length > 0) /* && __length < max_len)*/			\
		bpf_probe_read((void *)dst, __length, (char *)ctx + __offset); \
} while (0)

SEC("tracepoint/napi/napi_poll")
int napi_poll(struct napi_poll_ctx *ctx)
{
	unsigned int event_type = TYPE_VIOLATE;
	unsigned int budget = ctx->budget;
	unsigned int work = ctx->work;
	struct napi_struct *napi = ctx->napi;
	u64 pid_tgid = 0;
	int ifindex = 0;
	u32 key = 0;
	u64 *cnt;

	unsigned long	state = 0;
	unsigned int	napi_id = 0;

	struct napi_bulk_histogram *napi_work;

	napi_work = bpf_map_lookup_elem(&napi_hist_map, &key);
	if (!napi_work)
		return 0;

	/* TODO: Want to implement limiting tool to collect from a
	 * specific interface, but I cannot figure out howto extract
	 * the ifindex here.
	 */
	// Cannot deref napi pointer directly :-(
	//if (ctx->napi->dev)
	//	ifindex = ctx->napi->dev->ifindex;

	// TODO: look at using bpf_probe_read
	//  bpf_probe_read(napi,     sizeof(napi),    ctx->napi);
	//  bpf_probe_read(&ifindex, sizeof(ifindex), dev->ifindex);
	//  bpf_probe_read(&ifindex, 4, &ctx->napi->dev->ifindex);
	if (napi) {
		/* This seems to work: */
		bpf_probe_read(&napi_id, sizeof(napi_id), &napi->napi_id);
	}
//	if (napi && napi->dev) {
//              (NOT WORKING: rejected by verifier)
//		bpf_probe_read(&ifindex, sizeof(ifindex), &(napi->dev->ifindex));
//	}

#ifdef DEBUG
	/* Counter that keeps state across invocations (for hacks) */
	cnt = bpf_map_lookup_elem(&cnt_map, &key);
	if (!cnt)
		return 0;
	*cnt += 1;
	if ((*cnt % (1024*10)) == 0) {
		unsigned int a = 0;
		unsigned short t = 0;
		unsigned char c = 0;
		unsigned int pid = 0; //= ctx->common_pid;
		u64 z = 0;
		char devname[IFNAMSIZ] = { 0 };

		unsigned short d_offset = ctx->data_loc_dev_name & 0xFFFF;
		z = bpf_get_current_pid_tgid();
		bpf_probe_read(&c, 1, &ctx->common_flags);
		bpf_probe_read(&t, 2, &ctx->common_type);
		bpf_probe_read(&a, 1, &ctx->common_preempt_count);
		bpf_probe_read(&pid, 4, &ctx->common_pid);
		bpf_debug("TestAAA a:%u c:%u t:%u\n", a, c, t);
		bpf_debug("TestBBB pid:%u z:%u work:%u\n", pid, z, work);

		//DATA_LOC_READ(ctx, devname, dev_name, IFNAMSIZ);
		bpf_probe_read(devname, IFNAMSIZ, (char *)ctx + d_offset);
		bpf_debug("TestCCC data_loc:%u devname:%s\n",
			  ctx->data_loc_dev_name, devname);
	}
#endif
	/* Detect API violation */
	if (work > budget) {
		bpf_debug("API violation ifindex(%d) work(%d)>budget(%d)",
			  ifindex, work, budget);
		goto record_event_type;
	}

	if (work < 65)
		napi_work->hist[work]++;

	/* Detect when this gets invoked from idle task or from ksoftirqd */
	pid_tgid = bpf_get_current_pid_tgid();
	if (pid_tgid == 0)
		event_type = TYPE_IDLE_TASK;
	else
		event_type = TYPE_SOFTIRQ;

record_event_type:
	napi_work->type[event_type].cnt++;
	napi_work->type[event_type].pkts += work;
	if (!work)
		napi_work->type[event_type].cnt_bulk0++;

	return 0;
}

/*
 * IDEA: Use the irq:softirq_* tracepoints, to measure how many times
 * the system enters and exits softirq.
 *
 * Hint look at events used by:
 *  tools/perf/scripts/python/bin/netdev-times-record
 *
 * Tracepoint format: /sys/kernel/debug/tracing/events/irq/softirq.../format
 * Code in:                kernel/include/trace/events/irq.h
 */
struct irq_ctx {
	/* Tracepoint common fields */
	unsigned short common_type;	//	offset:0;  size:2; signed:0;
	unsigned char common_flags;	//	offset:2;  size:1; signed:0;
	unsigned char common_preempt_count;//	offset:3;  size:1; signed:0;
	int common_pid;			//	offset:4;  size:4; signed:1;
	/* Tracepoint specific fields */
	unsigned int vec_nr;		//	offset:8;  size:4; signed:0;
};

SEC("tracepoint/irq/softirq_entry")
int softirq_entry(struct irq_ctx *ctx)
{
	struct softirq_data *data = NULL;
	unsigned int vec_nr = ctx->vec_nr;
	u32 key = 0;

	data = bpf_map_lookup_elem(&softirq_map, &key);
	if (!data)
		return 0;

	if (vec_nr < SOFTIRQ_MAX)
		data->counters[vec_nr].enter++;

	return 0;
}

SEC("tracepoint/irq/softirq_exit")
int softirq_exit(struct irq_ctx *ctx)
{
	struct softirq_data *data = NULL;
	unsigned int vec_nr = ctx->vec_nr;
	u32 key = 0;

	data = bpf_map_lookup_elem(&softirq_map, &key);
	if (!data)
		return 0;

	if (vec_nr < SOFTIRQ_MAX)
		data->counters[vec_nr].exit++;

	return 0;
}

SEC("tracepoint/irq/softirq_raise")
int softirq_raise(struct irq_ctx *ctx)
{
	struct softirq_data *data = NULL;
	unsigned int vec_nr = ctx->vec_nr;
	u32 key = 0;

	data = bpf_map_lookup_elem(&softirq_map, &key);
	if (!data)
		return 0;

	if (vec_nr < SOFTIRQ_MAX)
		data->counters[vec_nr].raise++;

	return 0;
}

char _license[] SEC("license") = "GPL";



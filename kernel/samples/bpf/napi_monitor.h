#ifndef __NAPI_MONITOR_H__
#define __NAPI_MONITOR_H__

/* Shared struct between _user & _kern */

/* NAPI tracepoint data structures */
struct bulk_event_type {
	unsigned long cnt;
	unsigned long cnt_bulk0;
	unsigned long pkts;
};
enum event_t {
	TYPE_IDLE_TASK=0,
	TYPE_SOFTIRQ,
	TYPE_VIOLATE
};
struct napi_bulk_histogram {
	/* Keep counters per possible RX bulk value */
	unsigned long hist[65];
	struct bulk_event_type type[3];
};

/* SOFTIRQ tracepoint data structures */
enum vec_nr_t {
	SOFTIRQ_HI,
	SOFTIRQ_TIMER,
	SOFTIRQ_NET_TX,
	SOFTIRQ_NET_RX,
	SOFTIRQ_BLOCK,
	SOFTIRQ_IRQ_POLL,
	SOFTIRQ_TASKLET,
	SOFTIRQ_SCHED,
	SOFTIRQ_HRTIMER,
	SOFTIRQ_RCU,
	SOFTIRQ_MAX
};
struct softirq_cnt {
	unsigned long enter;
	unsigned long exit;
	unsigned long raise;
};
struct softirq_data {
	struct softirq_cnt counters[SOFTIRQ_MAX];
};

static const char *softirq_names[SOFTIRQ_MAX] = {
	[SOFTIRQ_HI]		= "SOFTIRQ_HI",
	[SOFTIRQ_TIMER]		= "SOFTIRQ_TIMER",
	[SOFTIRQ_NET_TX]	= "SOFTIRQ_NET_TX",
	[SOFTIRQ_NET_RX]	= "SOFTIRQ_NET_RX",
	[SOFTIRQ_BLOCK]		= "SOFTIRQ_BLOCK",
	[SOFTIRQ_IRQ_POLL]	= "SOFTIRQ_IRQ_POLL",
	[SOFTIRQ_TASKLET]	= "SOFTIRQ_TASKLET",
	[SOFTIRQ_SCHED]		= "SOFTIRQ_SCHED",
	[SOFTIRQ_HRTIMER]	= "SOFTIRQ_HRTIMER",
	[SOFTIRQ_RCU]		= "SOFTIRQ_RCU",
};
static inline const char *softirq2str(enum vec_nr_t softirq)
{
	if (softirq < SOFTIRQ_MAX)
		return softirq_names[softirq];
	return NULL;
}

//#define DEBUG 1
#ifdef  DEBUG
/* Only use this for debug output. Notice output from bpf_trace_printk()
 * end-up in /sys/kernel/debug/tracing/trace_pipe
 */
#define debug_enabled() true
#define bpf_debug(fmt, ...)						\
		({							\
			char ____fmt[] = fmt;				\
			bpf_trace_printk(____fmt, sizeof(____fmt),	\
				     ##__VA_ARGS__);			\
		})
#else
#define bpf_debug(fmt, ...) { } while (0)
#define debug_enabled() false
#endif

#endif /* __NAPI_MONITOR_H__ */

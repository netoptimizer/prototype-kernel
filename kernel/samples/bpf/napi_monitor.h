#ifndef __NAPI_MONITOR_H__
#define __NAPI_MONITOR_H__

/* Shared struct between _user & _kern */

struct bulk_event_type {
	unsigned long cnt;
	unsigned long cnt_bulk0;
	unsigned long pkts;
};
enum {
	TYPE_IDLE_TASK,
	TYPE_SOFTIRQ,
	TYPE_VIOLATE_BUDGET
};

struct napi_bulk_histogram {
	/* Keep counters per possible RX bulk value */
	unsigned long hist[65];
	// struct bulk_event_type type[3];
	unsigned long idle_task;
	unsigned long idle_task_pkts;
	unsigned long ksoftirqd;
	unsigned long ksoftirqd_pkts;
};

#endif /* __NAPI_MONITOR_H__ */

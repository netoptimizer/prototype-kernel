/*
 * Concurrency testing of ALF: Array-based Lock-Free queue
 *
 * This test tries to provoke concurrency errors with the alf_queue.
 *
 * In this test multiple concurrent producers(enqueue) tries to race
 * agaist each-ther while a single consumer(dequeue) is running
 * concurrently with producers.
 *
 * Validation test: Producers will enqueue their id and a constantly
 * increasing serial number into the queue.  The single consumer will
 * dequeue and keep track of all producers serial number, and validate
 * that this number is strictly increasing by one.  This validates no
 * elements gets lost, due to incorrect concurrency handling.
 *
 * Copyright (C) 2014, Red Hat, Inc. Jesper Dangaard Brouer
 *  for licensing details see kernel-base/COPYING
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/alf_queue.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/time_bench.h>

static int verbose=1;

static struct completion dequeue_start;

/* Struct hack to send data in the void ptr */
struct my_data {
	union {
		struct {
			u32 id;
			u32 cnt;
		};
		struct {
			void *raw;
		};
	};
};

struct my_producer {
	struct task_struct *kthread;
	struct my_data data;
} ____cacheline_aligned_in_smp;

#define NR_PRODUCERS	3
static struct my_producer producers[NR_PRODUCERS];

struct my_consumer {
	struct task_struct *kthread;
	u32 id;
	u32 prod_cnt[NR_PRODUCERS];
} ____cacheline_aligned_in_smp;
static struct my_consumer consumer;

/* Multi-Producer-Multi-Consumer Queue */
static struct alf_queue *mpmc;

#define SLEEP_TIME_ENQ	0
#define SLEEP_TIME_DEQ	1
#define QUEUE_SIZE	2048
#define PRODUCER_BULK	8
#define CONSUMER_BULK	8

/* PRODUCER_ELEMS_ENQ is the number of elements a single producer will
 * try to enqueue.  If set below the QUEUE_SIZE, we have a better
 * chance of avoiding a single producer starve other producers. We
 * want to measure/provoke the worst case situation, where several
 * producers compete and touch the queue data structures.
 */
#define PRODUCER_ELEMS_ENQ 1000

/* Trying to catching and reporting benchmark performance when a race
 * happened between the many enqueuers and the single dequeue.  If the
 * number of dequeued packets exceed teh QUEUE_SIZE, then the
 * situation should have occured.  But if the NR_PRODUCERS and the
 * number of elements they enqueue is smaller than 2x QUEUE_SIZE, then
 * the situation cannot occur, take this into account.
 */
#define CONSUMER_HIGH_DEQ_CNT min(QUEUE_SIZE * 2, \
				  NR_PRODUCERS * PRODUCER_ELEMS_ENQ)

#ifndef U32_MAX
#define U32_MAX                ((u32)~0U)
#endif

static noinline unsigned int
alf_run_producer(struct alf_queue *q, struct my_producer *me)
{
	int i, j, n, total = 0;
	void *objs[PRODUCER_BULK];
	int retries = 0, retries_max = U32_MAX;
	int elements = PRODUCER_ELEMS_ENQ;
	int32_t loops = elements / PRODUCER_BULK;

	for (j = 0; j < loops; j++) {

		/* Transfer the data part of producer via the void ptr, and
		 * send an increasing number for the consumer to validate
		 */
		for (i = 0; i < PRODUCER_BULK; i++, me->data.cnt++) {
			objs[i] = me->data.raw;
		}

		retries = 0;
	retry:
		n = alf_mp_enqueue(q, objs, PRODUCER_BULK);
		if (n == 0) {
			if (++retries < retries_max) {
				cpu_relax(); // cond_resched();
				goto retry;
			}
			/* scroll back counter */
			me->data.cnt -= PRODUCER_BULK;
			continue;
		} else {
			/* Fix this code if the API changed ;-) */
			BUG_ON(n != PRODUCER_BULK);
		}
		total += n;
		/* Hack: Wake up consumer after some enqueue */
//		if (loops == 10)
//			wake_up_process(consumer.kthread);
	}

//full:
	return total;
}

static noinline int alf_producer_thread(void *arg)
{
	struct my_producer *me = arg;
	unsigned int cnt;

	while (!kthread_should_stop()) {

		/* For max race, wait for consumer to start dequeue */
		//wake_up_process(consumer.kthread);
		wait_for_completion(&dequeue_start);

		cnt = alf_run_producer(mpmc, me);

		if (verbose >= 2) {
			preempt_disable();
			pr_info("Producer(%u) enq:%u cpu:%d sleep %d secs\n",
				me->data.id, cnt, smp_processor_id(),
				SLEEP_TIME_ENQ);
			preempt_enable();
		}

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ * SLEEP_TIME_ENQ);
	}

	return 0;
}

static void bench_reset_record(struct time_bench_record *rec,
			       uint32_t loops, int step)
{
	/* Setup time_bench record */
	memset(rec, 0, sizeof(*rec)); /* zero func might not update all */
	rec->version_abi = 1;
	rec->loops = loops;
	rec->step  = step;
	rec->flags = (TIME_BENCH_LOOP|TIME_BENCH_TSC|TIME_BENCH_WALLCLOCK);
}

static unsigned int
alf_run_consumer(struct alf_queue *q, struct my_consumer *me,
		 struct time_bench_record *rec)
{
	int i, j, n, total = 0;
	void *deq_objs[CONSUMER_BULK];
	struct my_data data;
	u32 predict;
	int elements = 100000;
	int32_t loops = elements / CONSUMER_BULK;

	bench_reset_record(rec, loops, CONSUMER_BULK);

	/* Signals all threads waiting on this completion */
	complete_all(&dequeue_start); /* enqueues raceing with dequeue */

	time_bench_start(rec);
	for (j = 0; j < loops; j++) {

		n = alf_mc_dequeue(q, deq_objs, CONSUMER_BULK);
		if (n == 0)
			break; /* empty queue */
		total += n;

		for (i = 0; i < n; i++) {
			data.raw = deq_objs[i];
			/* Basic idea is to validate all producers counters
			 * is increasing compared to last dequeued
			 */
			predict = me->prod_cnt[data.id] + 1;
			if (predict != data.cnt) {
				pr_err("ERROR: id:%u predicted:%u but was:%u\n",
				       data.id, predict, data.cnt);
				BUG();
			}
			me->prod_cnt[data.id] = data.cnt;
		}

	}
	time_bench_stop(rec, total);

	// complete_all(&dequeue_start); /* should give more enqueue race */

	return total;
}

static void bench_calc(struct time_bench_record *rec)
{
	/* Calculate stats */
	time_bench_calc_stats(rec);

	pr_info("Cost_Per_Dequeue: %llu cycles(tsc) %llu.%03llu ns (step:%d)"
		" - (measurement period time:%llu.%09u sec time_interval:%llu)"
		" - (invoke count:%llu tsc_interval:%llu)\n",
		rec->tsc_cycles,
		rec->ns_per_call_quotient, rec->ns_per_call_decimal, rec->step,
		rec->time_sec, rec->time_sec_remainder, rec->time_interval,
		rec->invoked_cnt, rec->tsc_interval);
}

static int alf_consumer_thread(void *arg)
{
	struct my_consumer *me = arg;
	unsigned int cnt;
	int min_bench_cnt = CONSUMER_HIGH_DEQ_CNT; /* Should be > QUEUE_SIZE */
	struct time_bench_record rec;
	int cpu;

	while (!kthread_should_stop()) {

		cnt = alf_run_consumer(mpmc, me, &rec);

		preempt_disable();
		cpu = smp_processor_id();
		preempt_enable();
		/* In case cnt is larger than queue size, congestion
		 * occured and concurrent enqueuers and deqeue have
		 * been running.
		 */
		if (cnt > min_bench_cnt) {
			if (verbose >= 1)
				pr_info("High dequeue cnt:%u cpu:%d\n",
					cnt, cpu);
			bench_calc(&rec);
		}
		if (verbose >= 2)
			pr_info("Consumer(%u) deq:%u cpu:%d sleep %d secs"
				" qsz:%d\n" ,
				me->id, cnt, cpu, SLEEP_TIME_DEQ,
				alf_queue_count(mpmc));
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ * SLEEP_TIME_DEQ);
	}

	return 0;
}

static int __init alf_queue_concurrent_module_init(void)
{
	int i;

	if (verbose)
		pr_info("Loaded\n");

	mpmc = alf_queue_alloc(QUEUE_SIZE, GFP_KERNEL);
	if (IS_ERR_OR_NULL(mpmc))
		return -ENOMEM;

	init_completion(&dequeue_start);
	// Do we need to reinit_completion() somewhere?

	consumer.kthread = kthread_run(alf_consumer_thread,
				       &consumer,
				       "alf_consumer");
	for (i = 0; i < NR_PRODUCERS;  i++) {
		consumer.prod_cnt[i] = U32_MAX;
	}


	for (i = 0; i < NR_PRODUCERS;  i++) {
		producers[i].data.id  = i;
		producers[i].data.cnt = 0;
		producers[i].kthread = kthread_run(alf_producer_thread,
						   &producers[i],
						   "alf_producer_%u", i);
	}

	return 0;
}
module_init(alf_queue_concurrent_module_init);

static unsigned int
empty_queue(struct alf_queue *q)
{
	int j, n, total = 0;
	void *deq_objs[1];
	unsigned int loops = 10000000;

	for (j = 0; j < loops; j++) {
		n = alf_mc_dequeue(q, deq_objs, 1);
		if (n == 0)
			break;
		total += n;
	}
	return total;
}

static void __exit alf_queue_concurrent_module_exit(void)
{
	int i, n;

	if (verbose)
		pr_info("Unloaded\n");

	for (i = 0; i < NR_PRODUCERS;  i++) {
		kthread_stop(producers[i].kthread);
	}
	kthread_stop(consumer.kthread);

	n = empty_queue(mpmc);
	if (verbose > 0)
		pr_info("Remaining elements in queue:%d", n);
	/* FIXME: Need to wait for kthreads to finish */
	alf_queue_free(mpmc);
}
module_exit(alf_queue_concurrent_module_exit);

MODULE_DESCRIPTION("Concurrency testing of ALF queue");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

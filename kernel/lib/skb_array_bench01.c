/*
 * Benchmark module for linux/skb_array.h
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time_bench.h>
#include <linux/mm.h> /* missing in ptr_ring.h on >= v4.16 */
#include <linux/skb_array.h>

static int verbose=1;

/* Simulating the most simple case: 1 enqueue + 1 dequeue on same CPU
 *
 *  Cost is enqueue+dequeue
 *
 * This is a really fake benchmark, but it sort of shows the minimum
 * overhead achievable with this type of queue, where it is the same
 * CPU enqueuing and dequeuing, and cache is guaranteed to be hot.
 */
static int time_bench_one_enq_deq(
	struct time_bench_record *rec, void *data)
{
	struct skb_array *queue = (struct skb_array*)data;
	struct sk_buff *skb, *nskb;
	uint64_t loops_cnt = 0;
	int i;

	/* Fake pointer value to enqueue */
	skb = (struct sk_buff *)(unsigned long)42;

	if (queue == NULL) {
		pr_err("Need queue struct ptr as input\n");
		return -1;
	}
	/* loop count is limited to 32-bit due to div_u64_rem() use */
	if (((uint64_t)rec->loops * 2) >= ((1ULL<<32)-1)) {
		pr_err("Loop cnt too big will overflow 32-bit\n");
		return 0;
	}

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		if (skb_array_produce(queue, skb) < 0) /* enqueue */
			goto fail;

		loops_cnt++;
		barrier(); /* compiler barrier */

		nskb = skb_array_consume(queue);	/* dequeue */
		if (skb != nskb)			/* validate object */
			goto fail;

		/* How to account: if we don't inc loops_cnt below,
		 * then the cost recorded is enqueue+dequeue cost
		 */
		// loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
fail:
	return 0;
}

/* Helper for emptying the queue before calling skb_array_cleanup(),
 * because we are using fake SKB pointers, which will Oops the kernel
 * if the destructor kfree_skb() is invoked.
 */
void helper_empty_queue(struct skb_array *queue)
{
	struct sk_buff *skb;

	while ((skb = skb_array_consume(queue)))
		/* Emptying fake SKB pointers */;
}

void noinline run_bench_min_overhead(uint32_t loops, int q_size)
{
	struct skb_array *queue;
	int result;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);

	result = skb_array_init(queue, q_size, GFP_KERNEL);
	if (result < 0) {
		pr_err("%s() err creating skb_array queue size:%d\n",
		       __func__, q_size);
		return;
	}

	time_bench_loop(loops, q_size, "skb_array_min_overhead", queue,
			time_bench_one_enq_deq);

	helper_empty_queue(queue);
	skb_array_cleanup(queue);
	kfree(queue);
}

/* This benchmark prefill the queue with objects, prior to running the
 * benchmark measurement.  The idea is to create some distance between
 * the producer and consumer.
 */
void noinline run_bench_prefillq(uint32_t loops, int q_size, int prefill)
{
	struct skb_array *queue;
	struct sk_buff *skb;
	int result, i;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);

	result = skb_array_init(queue, q_size, GFP_KERNEL);
	if (result < 0) {
		pr_err("%s() err creating skb_array queue size:%d\n",
		       __func__, q_size);
		return;
	}

	/* Fake pointer value to enqueue */
	skb = (struct sk_buff *)(unsigned long)42;

	/* Add some fake objects to the queue, in-order to create some
	 * distance between the producer and consumer.  Given they are
	 * fake objects we don''t need to clean them up later.
	 */
	for (i = 0; i < prefill; i++) {
		if (skb_array_produce(queue, skb) < 0) {
			pr_err("%s() err cannot prefill:%d sz:%d\n",
			       __func__, prefill, q_size);
			goto out;
		}
	}

	time_bench_loop(loops, prefill, "skb_array_prefilled", queue,
			time_bench_one_enq_deq);
out:
	helper_empty_queue(queue);
	skb_array_cleanup(queue);
	kfree(queue);
}

int run_benchmark_tests(void)
{
	uint32_t loops = 10000000;

	if (verbose)
		pr_info("For 'skb_array_min_overhead' step = queue_size"
			", cost is enqueue+dequeue\n");
	/* Adjusting queue size, although it should not matter it
	 * should be cache hot anyhow.
	 */
	run_bench_min_overhead(loops, 8);
	run_bench_min_overhead(loops, 64);
	run_bench_min_overhead(loops, 1000);
	run_bench_min_overhead(loops, 10000);
	run_bench_min_overhead(loops, 32000);

	if (verbose)
		pr_info("For 'skb_array_prefilled' step = prefilled objs"
			", cost is enqueue+dequeue\n");
	run_bench_prefillq(loops, 1000, 64);

	return 0;
}

static int __init skb_array_bench01_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_benchmark_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(skb_array_bench01_module_init);

static void __exit skb_array_bench01_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(skb_array_bench01_module_exit);

MODULE_DESCRIPTION("Benchmark of skb_array");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

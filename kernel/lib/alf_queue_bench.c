/*
* Benchmark module for linux/alf_queue.h usage
*  a Producer/Consumer Array-based Lock-Free pointer queue
*/
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/alf_queue.h>

//#include <linux/list.h>
//#include <linux/spinlock.h>
//#include <linux/slab.h>
//#include <linux/skbuff.h>
//#include <linux/errno.h>

#include <linux/time_bench.h>

static int verbose=1;

/* Timing at the nanosec level, we need to know the overhead
 * introduced by the for loop itself */
static int time_bench_for_loop(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier(); /* avoid compiler to optimize this loop */
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_bench_single_enqueue_dequeue(
	struct time_bench_record *rec, void *data)
{
	int on_stack = 123;
	int *obj = &on_stack;
	int *deq_obj = NULL;
	int i;
	uint64_t loops_cnt = 0;
	struct alf_queue *queue = (struct alf_queue*)data;

	if (queue == NULL) {
		pr_err("Need ring_queue as input\n");
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
		if (alf_mp_enqueue(queue, (void **)&obj, 1) < 0)
			goto fail;
		loops_cnt++;
		barrier(); /* compiler barrier */
		if (alf_mc_dequeue(queue, (void **)&deq_obj, 1) < 0)
			goto fail;
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
fail:
	return 0;
}


int run_benchmark_tests(void)
{
	uint32_t loops = 10000000;
	int ring_size = 512;
	int passed_count = 0;
	struct alf_queue *MPMC;

	time_bench_loop(loops*1000, 0,
			"for_loop", NULL, time_bench_for_loop);

	MPMC = alf_queue_alloc(ring_size, GFP_KERNEL);
	time_bench_loop(loops, 0,
			"ALF-simple", MPMC, time_bench_single_enqueue_dequeue);

	alf_queue_free(MPMC);
	return passed_count;
}

static int __init alf_queue_bench_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_benchmark_tests() < 0) {
		return -ECANCELED;
	}


	return 0;
}
module_init(alf_queue_bench_module_init);

static void __exit alf_queue_bench_module_exit(void)
{
	// TODO: perform sanity checks, and free mem
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(alf_queue_bench_module_exit);

MODULE_DESCRIPTION("Benchmark of Array-based Lock-Free queue");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

/*
 * Benchmark module for page_pool.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time_bench.h>
#include <net/page_pool.h>

static int verbose=1;

#include <linux/interrupt.h>

/* Timing at the nanosec level, we need to know the overhead
 * introduced by the for loop itself */
static int time_bench_for_loop(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	int i;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier(); /* avoid compiler to optimize this loop */
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_bench_atomic_inc(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	atomic_t cnt;
	int i;

	atomic_set(&cnt, 0);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		atomic_inc(&cnt);
		barrier(); /* avoid compiler to optimize this loop */
	}
	loops_cnt = atomic_read(&cnt);
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_bench_lock(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	spinlock_t lock;
	int i;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		spin_lock(&lock);
		loops_cnt++;
		barrier(); /* avoid compiler to optimize this loop */
		spin_unlock(&lock);
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

/* Helper for filling some page's into ptr_ring */
static void pp_fill_ptr_ring(struct page_pool *pp, int elems)
{
	gfp_t gfp_mask = GFP_ATOMIC; /* GFP_ATOMIC needed when under run softirq */
	struct page **array;
	int i;

	array = kzalloc(sizeof(struct page*) * elems, gfp_mask);

	for (i = 0; i < elems; i++) {
		array[i] = page_pool_alloc_pages(pp, gfp_mask);
	}
	for (i = 0; i < elems; i++) {
		page_pool_put_page(pp, array[i], false);
	}

	kfree(array);
}

static int time_bench_page_pool01(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	gfp_t gfp_mask = GFP_ATOMIC; /* GFP_ATOMIC is not really needed */
	int i, err;

	struct page_pool *pp;
	struct page *page;

	struct page_pool_params pp_params = {
		.order = 0,
		.flags = 0,
		.pool_size = 1024,
		.nid = NUMA_NO_NODE,
		.dev = NULL, /* Only use for DMA mapping */
		.dma_dir = DMA_BIDIRECTIONAL,
	};

	pp = page_pool_create(&pp_params);
	if (IS_ERR(pp)) {
		err = PTR_ERR(pp);
		pr_warn("%s: Error(%d) creating page_pool\n", __func__, err);
		goto out;
	}
	pp_fill_ptr_ring(pp, 64);

	if (in_serving_softirq())
		pr_warn("%s(): in_serving_softirq fast-path\n", __func__);
	else
		pr_warn("%s(): Cannot use page_pool fast-path\n", __func__);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		page = page_pool_alloc_pages(pp, gfp_mask);
		if (!page)
			break;
		loops_cnt++;
		barrier(); /* avoid compiler to optimize this loop */

		/* Issue: this module is in_serving_softirq() and thus
		 * cannot test the fast-path return.
		 */
		page_pool_recycle_direct(pp, page);
	}
	time_bench_stop(rec, loops_cnt);
out:
	page_pool_destroy(pp);
	return loops_cnt;
}

static int time_bench_page_pool02(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	gfp_t gfp_mask = GFP_ATOMIC; /* GFP_ATOMIC is not really needed */
	int i, err;

	struct page_pool *pp;
	struct page *page;

	struct page_pool_params pp_params = {
		.order = 0,
		.flags = 0,
		.pool_size = 1024,
		.nid = NUMA_NO_NODE,
		.dev = NULL, /* Only use for DMA mapping */
		.dma_dir = DMA_BIDIRECTIONAL,
	};

	pp = page_pool_create(&pp_params);
	if (IS_ERR(pp)) {
		err = PTR_ERR(pp);
		pr_warn("%s: Error(%d) creating page_pool\n", __func__, err);
		goto out;
	}

	if (in_serving_softirq())
		pr_warn("%s(): in_serving_softirq fast-path\n", __func__);
	else
		pr_warn("%s(): Cannot use page_pool fast-path\n", __func__);

	/* Q: Will performance improve if we create some more pages in ptr_ring? */
	pp_fill_ptr_ring(pp, 64);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		page = page_pool_alloc_pages(pp, gfp_mask);
		if (!page)
			break;
		loops_cnt++;
		barrier(); /* avoid compiler to optimize this loop */

		/* Issue: this module is in_serving_softirq() and thus
		 * cannot test the fast-path return.
		 */
		page_pool_put_page(pp, page, false);
	}
	time_bench_stop(rec, loops_cnt);
out:
	page_pool_destroy(pp);
	return loops_cnt;
}

/* Testing page_pool requires running under softirq.
 *
 * Running under a taslket satisfy this, as tasklets are built on top of
 * softirq.
 */
static void pp_tasklet_handler(unsigned long data)
{
	uint32_t loops = 1000000;

	if (in_serving_softirq())
		pr_warn("%s(): in_serving_softirq fast-path\n", __func__); // True
	else
		pr_warn("%s(): Cannot use page_pool fast-path\n", __func__);

	time_bench_loop(loops, 0,
			"tasklet_page_pool01", NULL, time_bench_page_pool01);

	time_bench_loop(loops, 0,
			"tasklet_page_pool02", NULL, time_bench_page_pool02);

}
DECLARE_TASKLET_DISABLED(pp_tasklet, pp_tasklet_handler, 0);

static void run_tasklet_tests(void)
{
	tasklet_enable(&pp_tasklet);
	/* "Async" schedule tasklet, which runs on the CPU that schedule it */
	tasklet_schedule(&pp_tasklet);
}

static int run_benchmark_tests(void)
{
	uint32_t loops = 10000000;
	int passed_count = 0;

	/* Baseline tests */
	time_bench_loop(loops*10, 0,
			"for_loop", NULL, time_bench_for_loop);
	time_bench_loop(loops*10, 0,
			"atomic_inc", NULL, time_bench_atomic_inc);
	time_bench_loop(loops, 0,
			"lock", NULL, time_bench_lock);

	/* This test cannot activate correct code path, due to no-softirq ctx */
	time_bench_loop(loops, 0,
			"no-softirq-page_pool01", NULL, time_bench_page_pool01);
	time_bench_loop(loops, 0,
			"no-softirq-page_pool02", NULL, time_bench_page_pool02);

	return passed_count;
}

static int __init bench_page_pool_simple_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	run_benchmark_tests();
	run_tasklet_tests();

	return 0;
	// tasklet_kill(&pp_tasklet);
	// return -EAGAIN; // Trick to not fully load module
}
module_init(bench_page_pool_simple_module_init);

static void __exit bench_page_pool_simple_module_exit(void)
{
	tasklet_kill(&pp_tasklet);

	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(bench_page_pool_simple_module_exit);

MODULE_DESCRIPTION("Benchmark of page_pool simple cases");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

/*
 * Benchmarking page allocator execution time inside the kernel
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

static int verbose=1;

#define DEFAULT_ORDER 0
static int page_order = DEFAULT_ORDER;
module_param(page_order, uint, 0);
MODULE_PARM_DESC(page_order, "Parameter page order to use in bench");

static uint32_t loops = 100000;
module_param(loops, uint, 0);
MODULE_PARM_DESC(loops, "Iteration loops");

/* Temp store for "outstanding" pages */
#define MAX_STORE 8192
void *store[MAX_STORE];

/* Most simple case for comparison */
static int time_single_page_alloc_put(
	struct time_bench_record *rec, void *data)
{
	gfp_t gfp_mask = (GFP_ATOMIC | ___GFP_NORETRY);
	struct page *my_page;
	int i;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		my_page = alloc_page(gfp_mask);
		if (unlikely(my_page == NULL))
			return 0;
		__free_page(my_page);
	}
	time_bench_stop(rec, i);
	return i;
}

static int time_alloc_pages(
	struct time_bench_record *rec, void *data)
{
	/* Important to set: __GFP_COMP for compound pages
	 */
	gfp_t gfp_mask = (GFP_ATOMIC | __GFP_COLD | __GFP_COMP);
	struct page *my_page;
	int order = rec->step;
	int i;

	/* Drop WARN on failures, time_bench will invalidate test */
	gfp_mask |= __GFP_NOWARN;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		my_page = alloc_pages(gfp_mask, order);
		if (unlikely(my_page == NULL))
			return 0;
		__free_pages(my_page, order);
	}
	time_bench_stop(rec, i);

	if (verbose) {
		time_bench_calc_stats(rec);
		pr_info("alloc_pages order:%d(%luB/x%d) %llu cycles"
			" per-%luB %llu cycles\n",
			order, PAGE_SIZE << order, 1 << order,
			rec->tsc_cycles, PAGE_SIZE,
			rec->tsc_cycles >> order);
	}

	return i;
}

/* Benchmark what happens when allocating "step" pages before freeing
 * them again.  This simulates workloads which use/consumes several
 * pages before returning them again, in a short period of time.
 *
 * NICs will usually:
 *  - RX several packets
 *  - Send them for TX in a TX ring queue
 *  - Refill RX ring queue
 *  - At DMA TX completing free many pages
 *
 * This func does not simulate this exactly, but tries to at-least
 * simulate N outstanding pages.
 */
static int time_alloc_pages_outstanding(
	struct time_bench_record *rec, void *data)
{
	/* Important to set: __GFP_COMP for compound pages
	 */
//	gfp_t gfp_mask = (GFP_ATOMIC | __GFP_COLD | __GFP_COMP);
	gfp_t gfp_mask = (__GFP_COMP);
	struct page *page;
	int allocs_before_free = rec->step;
	int order = page_order; /* <-- GLOBAL variable */
	int i, j;

	if (allocs_before_free > MAX_STORE) {
		pr_warn("%s() allocs_before_free(%d) request too big >%d\n",
			__func__, allocs_before_free, MAX_STORE);
		return 0;
	}

	/* Drop WARN on failures, time_bench will invalidate test */
	gfp_mask |= __GFP_NOWARN;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; /* inc in loop */) {

		for (j = 0; j < allocs_before_free; j++) {
			page = alloc_pages(gfp_mask, order);
			if (unlikely(page == NULL))
				goto out;
			store[j] = page;
		}
		/* Might overshoot rec->loops */
		i += j;

		for (j = 0; j < allocs_before_free; j++)
			__free_pages(store[j], order);
	}
	time_bench_stop(rec, i);

	if (verbose) {
		time_bench_calc_stats(rec);
		pr_info("N=%d outstanding pages order:%d(%luB/x%d) %llu cycles"
			" per-%luB %llu cycles\n",
			allocs_before_free,
			order, PAGE_SIZE << order, 1 << order,
			rec->tsc_cycles, PAGE_SIZE,
			rec->tsc_cycles >> order);
	}

	return i;
out:
	/* Error handling: Free remaining objects */
	pr_info("FAILED N=%d outstanding pages order:%d i:%d j:%d\n",
		allocs_before_free, order, i, j);
	for (i = 0; i < j; i++)
		__free_pages(store[i], order);
	return 0;
}


int run_timing_tests(void)
{
	/* For comparison */
	time_bench_loop(loops, 0, "single_page_alloc_put",
			NULL, time_single_page_alloc_put);
	/* For comparison */
	time_bench_loop(loops, page_order, "alloc_pages_order", NULL,
			time_alloc_pages);

	/* More advanced use-cases */

	/* The basic question to answer here is whether allocating and
	 * keeping N number of pages outstanding, before free'ing them
	 * again effect the performance.
	 *
	 * This pattern of allocating some pages, and then freeing
	 * them later is slightly more realistic (than quick alloc+free
	 * pattern of the same page).
	 */
	time_bench_loop(loops,    1, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops,    2, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops,    4, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops,    8, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops,   16, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops,   32, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops,   64, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops,  128, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops,  512, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops, 1024, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops, 4096, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);
	time_bench_loop(loops, 8192, "step_outstanding_pages", NULL,
			time_alloc_pages_outstanding);


	return 0;
}

static int __init page_bench02_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

#ifdef CONFIG_DEBUG_PREEMPT
	pr_warn("WARN: CONFIG_DEBUG_PREEMPT is enabled: this affect results\n");
#endif
	if (run_timing_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(page_bench02_module_init);

static void __exit page_bench02_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(page_bench02_module_exit);

MODULE_DESCRIPTION("Benchmarking page alloactor execution time in kernel");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

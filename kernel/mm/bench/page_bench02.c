/*
 * Benchmarking page allocator execution time inside the kernel
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/mm.h>

static int verbose=1;

/* Quick and dirty way to unselect some of the benchmark tests, by
 * encoding this in a module parameter flag.  This is useful when
 * wanting to perf benchmark a specific benchmark test.
 *
 * Hint: Bash shells support writing binary number like: $((2#101010))
 * Use like:
 *  modprobe page_bench02 loops=$((10**7))  run_flags=$((2#010))
 */
static unsigned long run_flags = 0xFFFFFFFF;
module_param(run_flags, ulong, 0);
MODULE_PARM_DESC(run_flags, "Hack way to limit bench to run");
/* Count the bit number from the enum */
enum benchmark_bit {
	bit_run_bench_order0_compare,
	bit_run_bench_orderN,
	bit_run_bench_outstanding,
	bit_run_bench_outstanding_parallel_cpus
};
#define bit(b)	(1 << (b))
#define run_or_return(b) do { if (!(run_flags & (bit(b)))) return; } while (0)

#define DEFAULT_ORDER 0
static int page_order = DEFAULT_ORDER;
module_param(page_order, uint, 0);
MODULE_PARM_DESC(page_order, "Parameter page order to use in bench");

static uint32_t loops = 100000;
module_param(loops, uint, 0);
MODULE_PARM_DESC(loops, "Iteration loops");

static int parallel_cpus = 2;
module_param(parallel_cpus, uint, 0);
MODULE_PARM_DESC(parallel_cpus, "Parameter for number of parallel CPUs");

static int parallel_outstanding = 128;
module_param(parallel_outstanding, uint, 0);
MODULE_PARM_DESC(parallel_outstanding,
		 "Number of outstanding pagee in parallel test");


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
		put_page(my_page);
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
	int i = 0, j = 0;

	/* Need seperately allocated store to support parallel use */
	/* Temp store for "outstanding" pages */
#define MAX_STORE 8192
	void **store;

	store = kzalloc(sizeof(void *) * MAX_STORE, GFP_KERNEL);
	if (!store)
		goto out;

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
	kfree(store);
	return 0;
}

void noinline run_bench_order0_compare(uint32_t loops)
{
	run_or_return(bit_run_bench_order0_compare);
	/* For comparison: order-0 */
	time_bench_loop(loops, 0, "single_page_alloc_put",
			NULL, time_single_page_alloc_put);
}

void noinline run_bench_orderN(uint32_t loops)
{
	run_or_return(bit_run_bench_orderN);
	/* For comparison: single page specific order */
	time_bench_loop(loops, page_order, "alloc_pages_order", NULL,
			time_alloc_pages);
}

void noinline run_bench_bench_outstanding(uint32_t loops)
{
	run_or_return(bit_run_bench_outstanding);

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

}

void noinline bench_outstanding_parallel_cpus(uint32_t loops, int nr_cpus,
					      int outstanding_pages)
{
	const char *desc = "parallel_cpus";
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	struct cpumask my_cpumask;
	int i;

	/* Allocate records for CPUs */
	cpu_tasks = kzalloc(sizeof(*cpu_tasks) * nr_cpus, GFP_KERNEL);

	/* Reduce number of CPUs to run on */
	cpumask_clear(&my_cpumask);
	for (i = 0; i < nr_cpus ; i++) {
		cpumask_set_cpu(i, &my_cpumask);
	}
	pr_info("Limit to %d parallel CPUs\n", nr_cpus);
	time_bench_run_concurrent(loops, outstanding_pages, NULL,
				  &my_cpumask, &sync, cpu_tasks,
				  time_alloc_pages_outstanding);
	time_bench_print_stats_cpumask(desc, cpu_tasks, &my_cpumask);
	kfree(cpu_tasks);
}

void noinline run_bench_outstanding_parallel_cpus(uint32_t loops, int nr_cpus)
{
	run_or_return(bit_run_bench_outstanding_parallel_cpus);

	bench_outstanding_parallel_cpus(loops, nr_cpus, parallel_outstanding);
}


int run_timing_tests(void)
{
	run_bench_order0_compare(loops);
	run_bench_orderN(loops);
	run_bench_bench_outstanding(loops);
	run_bench_outstanding_parallel_cpus(loops, parallel_cpus);
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

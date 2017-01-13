/*
 * Benchmarking page allocator bulk API
 *  ***NOTICE***: not-upstream experimental patch by Mel Gorman
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/list.h>

static int verbose=1;

/* Quick and dirty way to unselect some of the benchmark tests, by
 * encoding this in a module parameter flag.  This is useful when
 * wanting to perf benchmark a specific benchmark test.
 *
 * Hint: Bash shells support writing binary number like: $((2#101010))
 * Use like:
 *  modprobe page_bench04_bulk loops=$((10**7))  run_flags=$((2#010))
 */
static unsigned long run_flags = 0xFFFFFFFF;
module_param(run_flags, ulong, 0);
MODULE_PARM_DESC(run_flags, "Hack way to limit bench to run");
/* Count the bit number from the enum */
enum benchmark_bit {
	bit_run_bench_order0_compare,
	bit_run_bench_page_bulking,
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

#define MAX_BULK 32768

static int time_bulk_page_alloc_free(
	struct time_bench_record *rec, void *data)
{
	gfp_t gfp = (GFP_ATOMIC | ___GFP_NORETRY);
	uint64_t loops_cnt = 0;
	int order=0;
	int i;

	/* Bulk size setup from "step" */
	size_t bulk = rec->step;

	if (bulk > MAX_BULK) {
		pr_warn("%s() bulk(%lu) request too big cap at %d\n",
			__func__, bulk, MAX_BULK);
		bulk = MAX_BULK;
	}
	/* loop count is limited to 32-bit due to div_u64_rem() use */
	if (((uint64_t)rec->loops * bulk *2) >= ((1ULL<<32)-1)) {
		pr_err("Loop cnt too big will overflow 32-bit\n");
		return 0;
	}

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		struct list_head list;
		unsigned long n;
		INIT_LIST_HEAD(&list);

		n = alloc_pages_bulk(gfp, order, bulk, &list);

		if (verbose && (n < bulk))
			pr_warn("%s(): got less pages: %lu/%lu\n",
				__func__, n, bulk);
		barrier();
		free_pages_bulk(&list);

		/* NOTICE THIS COUNTS (bulk) alloc+free together */
		loops_cnt+= n;
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}


void noinline run_bench_order0_compare(uint32_t loops)
{
	run_or_return(bit_run_bench_order0_compare);
	/* For comparison: order-0 */
	time_bench_loop(loops, 0, "single_page_alloc_put",
			NULL, time_single_page_alloc_put);
}

void noinline run_bench_page_bulking(uint32_t loops, int bulk)
{
	run_or_return(bit_run_bench_page_bulking);
	time_bench_loop(loops, bulk, "time_bulk_page_alloc_free",
			NULL,         time_bulk_page_alloc_free);
}


int run_timing_tests(void)
{
	run_bench_order0_compare(loops);

	run_bench_page_bulking(loops,  1);
	run_bench_page_bulking(loops,  2);
	run_bench_page_bulking(loops,  4);
	run_bench_page_bulking(loops,  8);
	run_bench_page_bulking(loops, 16);
	run_bench_page_bulking(loops, 32);
	run_bench_page_bulking(loops, 64);
	//run_bench_page_bulking(loops/100, 1024);
	return 0;
}

static int __init page_bench04_module_init(void)
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
module_init(page_bench04_module_init);

static void __exit page_bench04_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(page_bench04_module_exit);

MODULE_DESCRIPTION("Benchmarking page alloactor bulk API");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

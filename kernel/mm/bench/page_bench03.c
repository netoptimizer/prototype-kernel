/*
 * Benchmarking page allocator concurrency
 *  - parallel execution scalability
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/cpumask.h>

static int verbose=1;

#define DEFAULT_ORDER 0
static int page_order = DEFAULT_ORDER;
module_param(page_order, uint, 0);
MODULE_PARM_DESC(page_order, "Parameter page order to use in bench");

static int parallel_cpus = 2;
module_param(parallel_cpus, uint, 0);
MODULE_PARM_DESC(parallel_cpus, "Parameter for number of parallel CPUs");

/* Quick and dirty way to unselect some of the benchmark tests, by
 * encoding this in a module parameter flag.  This is useful when
 * wanting to perf benchmark a specific benchmark test.
 *
 * Hint: Bash shells support writing binary number like: $((2#101010))
 * Use like:
 *  modprobe page_bench03 page_order=1 parallel_cpus=4 run_flags=$((2#100))
 */
static unsigned long long run_flags = 0xFFFFFFFF;
module_param(run_flags, ullong, 0);
MODULE_PARM_DESC(run_flags, "Hack way to limit bench to run");
/* Count the bit number from the enum */
enum benchmark_bit {
	bit_run_bench_compare,
	bit_run_bench_parallel_all_cpus,
	bit_run_bench_limited_cpus
};
#define bit(b)	(1 << (b))
#define run_or_return(b) do { if (!(run_flags & (bit(b)))) return; } while (0)

/* Page specific stats */
/*
	order = step;
	pr_info("Parallel-CPUs:%d page order:%d(%luB/x%d) ave %llu cycles"
		" per-%luB %llu cycles\n",
		sum.records, order, PAGE_SIZE << order, 1 << order,
		average, PAGE_SIZE, average >> order);
*/

static int time_alloc_pages(
	struct time_bench_record *rec, void *data)
{
	/* Important to set: __GFP_COMP for compound pages
	 */
	gfp_t gfp_mask = (GFP_ATOMIC | __GFP_COLD | __GFP_COMP);
	struct page *page;
	int order = rec->step;
	int i;

	/* Drop WARN on failures, time_bench will invalidate test */
	gfp_mask |= __GFP_NOWARN;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		page = alloc_pages(gfp_mask, order);
		if (unlikely(page == NULL))
			return 0;
		__free_pages(page, order);
	}
	time_bench_stop(rec, i);
	return i;
}


void noinline run_bench_compare(uint32_t loops)
{
	run_or_return(bit_run_bench_compare);

	/* For comparison */
	time_bench_loop(loops, page_order, "alloc_pages_order_step", NULL,
			time_alloc_pages);
}

void noinline run_bench_parallel_all_cpus(uint32_t loops)
{
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	size_t size;

	run_or_return(bit_run_bench_parallel_all_cpus);

	/* Allocate records for every CPU */
	size = sizeof(*cpu_tasks) * num_possible_cpus();
	cpu_tasks = kzalloc(size, GFP_KERNEL);

	/* Run concurrently */
	time_bench_run_concurrent(loops, page_order, "parallel-test",
				  cpu_online_mask, &sync, cpu_tasks,
				  time_alloc_pages);
	kfree(cpu_tasks);
}

void noinline run_bench_limited_cpus(uint32_t loops, int nr_cpus)
{
	const char *desc = "limited-cpus";
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	struct cpumask my_cpumask;
	int i;

	run_or_return(bit_run_bench_limited_cpus);

	/* Allocate records for CPUs */
	cpu_tasks = kzalloc(sizeof(*cpu_tasks) * nr_cpus, GFP_KERNEL);

	/* Reduce number of CPUs to run on */
	cpumask_clear(&my_cpumask);
	for (i = 0; i < nr_cpus ; i++) {
		cpumask_set_cpu(i, &my_cpumask);
	}
	pr_info("Limit to %d parallel CPUs\n", parallel_cpus);
	time_bench_run_concurrent(loops, page_order, desc,
				  &my_cpumask, &sync, cpu_tasks,
				  time_alloc_pages);
	time_bench_print_stats_cpumask(desc, cpu_tasks, &my_cpumask);
	kfree(cpu_tasks);
}

int run_timing_tests(void)
{
	uint32_t loops = 100000;

	run_bench_compare(loops);
	run_bench_parallel_all_cpus(loops);
	run_bench_limited_cpus(loops, parallel_cpus);

	return 0;
}

static int __init page_bench03_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_timing_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(page_bench03_module_init);

static void __exit page_bench03_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(page_bench03_module_exit);

MODULE_DESCRIPTION("Benchmarking page alloactor concurrency");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

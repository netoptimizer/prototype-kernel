/*
 * Benchmark module for page_pool.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time_bench.h>
#include <net/page_pool.h>

static int verbose=1;

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

int run_benchmark_tests(void)
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



	return passed_count;
}

static int __init bench_page_pool_simple_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_benchmark_tests() < 0) {
		return -ECANCELED;
	}

	//return 0;
	return -EAGAIN; // Trick to not fully load module
}
module_init(bench_page_pool_simple_module_init);

static void __exit bench_page_pool_simple_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(bench_page_pool_simple_module_exit);

MODULE_DESCRIPTION("Benchmark of page_pool simple cases");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

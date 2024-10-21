/*
 * Benchmark module for traits - related to XDP-hints
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time_bench.h>
//#include <net/trait.h>

static int verbose=1;

/* Makes tests selectable. Useful for perf-record to analyze a single test.
 * Hint: Bash shells support writing binary number like: $((2#101010)
 *
 * # modprobe bench_page_pool_simple run_flags=$((2#100))
 */
static unsigned long run_flags = 0xFFFFFFFF;
module_param(run_flags, ulong, 0);
MODULE_PARM_DESC(run_flags, "Limit which bench test that runs");
/* Count the bit number from the enum */
enum benchmark_bit {
	bit_run_bench_baseline,
};
#define bit(b)		(1 << (b))
#define enabled(b)	((run_flags & (bit(b))))

/* notice time_bench is limited to U32_MAX nr loops */
static unsigned long loops = 10000000;
module_param(loops, ulong, 0);
MODULE_PARM_DESC(loops, "Specify loops bench will run");

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

static int run_benchmark_tests(void)
{
	uint32_t nr_loops = loops;

	/* Baseline tests */
	if (enabled(bit_run_bench_baseline)) {
		time_bench_loop(nr_loops*10, 0,
				"for_loop", NULL, time_bench_for_loop);
		time_bench_loop(nr_loops*10, 0,
				"atomic_inc", NULL, time_bench_atomic_inc);
	}

	return 0;
}

static int __init bench_traits_simple_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (loops > U32_MAX) {
		pr_err("Module param loops(%lu) exceeded U32_MAX(%u)\n",
		       loops, U32_MAX);
		return -ECHRNG;
	}

	run_benchmark_tests();

	// return 0;
	return -EAGAIN; // Trick to not fully load module
}
module_init(bench_traits_simple_module_init);

static void __exit bench_traits_simple_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(bench_traits_simple_module_exit);

MODULE_DESCRIPTION("Benchmark of traits");
MODULE_AUTHOR("Jesper Dangaard Brouer <hawk@kernel.org>");
MODULE_LICENSE("GPL");

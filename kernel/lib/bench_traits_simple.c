/*
 * Benchmark module for traits - related to XDP-hints
 *
 * NOTICE: Compiling this depend kernel changes under-development
 *  https://github.com/arthurfabre/linux/tree/afabre/traits-002-bounds-inline
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time_bench.h>
#include <net/xdp.h>
#include <net/trait.h>
#include <linux/mm.h>

static int verbose=1;

/* Makes tests selectable. Useful for perf-record to analyze a single test.
 * Hint: Bash shells support writing binary number like: $((2#101010)
 *
 * # perf record -g modprobe bench_traits_simple run_flags=$((2#10))
 */
static unsigned long run_flags = 0xFFFFFFFF;
module_param(run_flags, ulong, 0);
MODULE_PARM_DESC(run_flags, "Limit which bench test that runs");
/* Count the bit number from the enum */
enum benchmark_bit {
	bit_run_bench_baseline,
	bit_run_bench_trait_set,
	bit_run_bench_trait_get,
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

static void noinline measured_function(volatile int *var)
{
	(*var) = 1;
}
static int time_func(
	struct time_bench_record *rec, void *data)
{
	int i, tmp;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		measured_function(&tmp);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

struct func_ptr_ops {
	void (*func)(volatile int *var);
	unsigned int (*func2)(unsigned int count);
};
static struct func_ptr_ops my_func_ptr __read_mostly = {
	.func  = measured_function,
};
static int time_func_ptr(
	struct time_bench_record *rec, void *data)
{
	int i, tmp;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		my_func_ptr.func(&tmp);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

/* WORK AROUND for improper EXPORT_SYMBOL_GPL */
int bpf_xdp_trait_set(const struct xdp_buff *xdp, u64 key,
		      const void *val, u64 val__sz, u64 flags);
int bpf_xdp_trait_get(const struct xdp_buff *xdp, u64 key,
		      void *val, u64 val__sz);

static int time_trait_set(struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	int i;

	u64 key = 1;
	u64 val = 42;

	/* XDP create fake packet */
	gfp_t gfp_mask = (__GFP_ZERO);
	struct page *page;
	void *data_start;
	struct xdp_buff xdp_buff = {};
	struct xdp_buff *xdp = &xdp_buff;

	page = alloc_page(gfp_mask);
	if (!page)
		return 0;

	/* XDP setup fake packet */
	data_start = page_address(page);
	xdp_init_buff(xdp, PAGE_SIZE, NULL);
	xdp_prepare_buff(xdp, data_start, XDP_PACKET_HEADROOM, 1024, true);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		bpf_xdp_trait_set(xdp, key, &val, sizeof(val), 0);
		// bpf_xdp_trait_set(xdp, 2, &val, sizeof(val), 0);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	__free_page(page);

	return loops_cnt;
}

static int time_trait_get(struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	int i;

	u64 key = 1;
	u64 val = 42;
	u64 val2 = 0;

	/* XDP create fake packet */
	gfp_t gfp_mask = (__GFP_ZERO);
	struct page *page;
	void *data_start;
	struct xdp_buff xdp_buff = {};
	struct xdp_buff *xdp = &xdp_buff;

	page = alloc_page(gfp_mask);
	if (!page)
		return 0;

	/* XDP setup fake packet */
	data_start = page_address(page);
	xdp_init_buff(xdp, PAGE_SIZE, NULL);
	xdp_prepare_buff(xdp, data_start, XDP_PACKET_HEADROOM, 1024, true);

	bpf_xdp_trait_set(xdp, key, &val, sizeof(val), 0);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		bpf_xdp_trait_get(xdp, key, &val2, sizeof(val2));
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	__free_page(page);

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

		/*  cost for a local function call */
		time_bench_loop(loops, 0, "function_call_cost",
				NULL, time_func);

		/*  cost for a function pointer invocation */
		time_bench_loop(loops, 0, "func_ptr_call_cost",
				NULL, time_func_ptr);
	}

	if (enabled(bit_run_bench_trait_set)) {
		time_bench_loop(loops, 0, "trait_set",
				NULL, time_trait_set);
	}

	if (enabled(bit_run_bench_trait_get)) {
		time_bench_loop(loops, 0, "trait_get",
				NULL, time_trait_get);
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

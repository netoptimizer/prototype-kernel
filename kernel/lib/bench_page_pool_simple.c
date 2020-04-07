/*
 * Benchmark module for page_pool.
 *
 * The complication for benchmarking page_pool is that it depends on running
 * under softirq, and will choose a slower path if check in_serving_softirq
 * fails.  This module uses tasklet's to workaround this.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/time_bench.h>
#include <net/page_pool.h>

#include <linux/interrupt.h>
#include <linux/limits.h>
#include <linux/version.h>

static int verbose=1;
#define MY_POOL_SIZE	1024

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static inline
void _page_pool_put_page(struct page_pool *pool, struct page *page,
			 bool allow_direct)
{
	page_pool_put_page(pool, page, -1, allow_direct);
}
#else
static inline
void _page_pool_put_page(struct page_pool *pool, struct page *page,
			 bool allow_direct)
{
	page_pool_put_page(pool, page, allow_direct);
}
#endif

DEFINE_MUTEX(wait_for_tasklet);

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
	bit_run_bench_no_softirq01,
	bit_run_bench_no_softirq02,
	bit_run_bench_no_softirq03,
	bit_run_bench_tasklet01,
	bit_run_bench_tasklet02,
	bit_run_bench_tasklet03,
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

/* The ptr_ping in page_pool uses a spinlock. We need to know the minimum
 * overhead of taking+releasing a spinlock, to know the cycles that can be saved
 * by e.g. amortizing this via bulking.
 */
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
		_page_pool_put_page(pp, array[i], false);
	}

	kfree(array);
}

enum test_type {
	type_fast_path,
	type_ptr_ring,
	type_page_allocator
};

/* Depends on compile optimizing this function */
static __always_inline
int time_bench_page_pool(
	struct time_bench_record *rec, void *data,
	enum test_type type, const char *func)
{
	uint64_t loops_cnt = 0;
	gfp_t gfp_mask = GFP_ATOMIC; /* GFP_ATOMIC is not really needed */
	int i, err;

	struct page_pool *pp;
	struct page *page;

	struct page_pool_params pp_params = {
		.order = 0,
		.flags = 0,
		.pool_size = MY_POOL_SIZE,
		.nid = NUMA_NO_NODE,
		.dev = NULL, /* Only use for DMA mapping */
		.dma_dir = DMA_BIDIRECTIONAL,
	};

	pp = page_pool_create(&pp_params);
	if (IS_ERR(pp)) {
		err = PTR_ERR(pp);
		pr_warn("%s: Error(%d) creating page_pool\n", func, err);
		goto out;
	}
	pp_fill_ptr_ring(pp, 64);

	if (in_serving_softirq())
		pr_warn("%s(): in_serving_softirq fast-path\n", func);
	else
		pr_warn("%s(): Cannot use page_pool fast-path\n", func);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		/* Common fast-path alloc, that depend on in_serving_softirq() */
		page = page_pool_alloc_pages(pp, gfp_mask);
		if (!page)
			break;
		loops_cnt++;
		barrier(); /* avoid compiler to optimize this loop */

		/* The benchmarks purpose it to test different return paths.
		 * Compiler should inline optimize other function calls out
		 */
		if (type == type_fast_path) {
			/* Fast-path recycling e.g. XDP_DROP use-case */
			page_pool_recycle_direct(pp, page);

		} else if (type == type_ptr_ring ) {
			/* Normal return path */
			_page_pool_put_page(pp, page, false);

		} else if (type == type_page_allocator ) {
			/* Test if not pages are recycled, but instead
			 * returned back into systems page allocator
			 */
			page_pool_release_page(pp, page);
			put_page(page);
		} else {
			BUILD_BUG();
		}
	}
	time_bench_stop(rec, loops_cnt);
out:
	page_pool_destroy(pp);
	return loops_cnt;
}

int time_bench_page_pool01_fast_path(
	struct time_bench_record *rec, void *data)
{
	return time_bench_page_pool(rec, data, type_fast_path, __func__);
}

int time_bench_page_pool02_ptr_ring(
	struct time_bench_record *rec, void *data)
{
	return time_bench_page_pool(rec, data, type_ptr_ring, __func__);
}

int time_bench_page_pool03_slow(
	struct time_bench_record *rec, void *data)
{
	return time_bench_page_pool(rec, data, type_page_allocator, __func__);
}

/* Testing page_pool requires running under softirq.
 *
 * Running under a tasklet satisfy this, as tasklets are built on top of
 * softirq.
 */
static void pp_tasklet_handler(unsigned long data)
{
	uint32_t nr_loops = loops;

	if (in_serving_softirq())
		pr_warn("%s(): in_serving_softirq fast-path\n", __func__); // True
	else
		pr_warn("%s(): Cannot use page_pool fast-path\n", __func__);

	if (enabled(bit_run_bench_tasklet01))
		time_bench_loop(nr_loops, 0,
				"tasklet_page_pool01_fast_path", NULL,
				time_bench_page_pool01_fast_path);

	if (enabled(bit_run_bench_tasklet02))
		time_bench_loop(nr_loops, 0,
				"tasklet_page_pool02_ptr_ring", NULL,
				time_bench_page_pool02_ptr_ring);

	if (enabled(bit_run_bench_tasklet03))
		time_bench_loop(nr_loops, 0,
				"tasklet_page_pool03_slow", NULL,
				time_bench_page_pool03_slow);

	mutex_unlock(&wait_for_tasklet); /* Module __init waiting on unlock */
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
	uint32_t nr_loops = loops;
	int passed_count = 0;

	/* Baseline tests */
	if (enabled(bit_run_bench_baseline)) {
		time_bench_loop(nr_loops*10, 0,
				"for_loop", NULL, time_bench_for_loop);
		time_bench_loop(nr_loops*10, 0,
				"atomic_inc", NULL, time_bench_atomic_inc);
		time_bench_loop(nr_loops, 0,
				"lock", NULL, time_bench_lock);
	}

	/* This test cannot activate correct code path, due to no-softirq ctx */
	if (enabled(bit_run_bench_no_softirq01))
		time_bench_loop(nr_loops, 0,
				"no-softirq-page_pool01", NULL,
				time_bench_page_pool01_fast_path);
	if (enabled(bit_run_bench_no_softirq02))
		time_bench_loop(nr_loops, 0,
				"no-softirq-page_pool02", NULL,
				time_bench_page_pool02_ptr_ring);
	if (enabled(bit_run_bench_no_softirq03))
		time_bench_loop(nr_loops, 0,
				"no-softirq-page_pool03",
				NULL, time_bench_page_pool03_slow);

	return passed_count;
}

static int __init bench_page_pool_simple_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (loops > U32_MAX) {
		pr_err("Module param loops(%lu) exceeded U32_MAX(%u)\n",
		       loops, U32_MAX);
		return -ECHRNG;
	}

	run_benchmark_tests();

	mutex_lock(&wait_for_tasklet);
	run_tasklet_tests();
	/* Sleep on mutex, waiting for tasklet to release */
	mutex_lock(&wait_for_tasklet);

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

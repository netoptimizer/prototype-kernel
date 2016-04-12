/*
 * Micro-Benchmarking module for linux/qmempool.h usage
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/alf_queue.h>
#include <linux/slab.h>
#include <linux/time_bench.h>
#include <linux/skbuff.h>

#include <linux/qmempool.h>

static int verbose=1;

static void print_qstats(struct qmempool *pool,
			 const char *func, const char *msg)
{
	struct qmempool_percpu *cpu;
	int localq_sz, sharedq_sz;

	preempt_disable();
	cpu = this_cpu_ptr(pool->percpu);
	localq_sz  = alf_queue_count(cpu->localq);
	sharedq_sz = alf_queue_count(pool->sharedq);
	if (verbose >= 2)
		pr_info("%s() qstats localq:%d sharedq:%d (%s)\n", func,
			localq_sz, sharedq_sz, msg);
	preempt_enable();
}

/*** Benchmark code execution time tests ***/

struct my_elem {
	/* element used for benchmark testing */
	struct sk_buff skb;
};

enum behavior_type {
	NORMAL = 1,
	NORMAL_INLINE,
	SOFTIRQ,
	SOFTIRQ_INLINE,
};

/* For comparison benchmark against the fastpath of the
 * slab/kmem_cache allocator
 */
static int benchmark_kmem_cache_fastpath_reuse(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	int i;
	struct my_elem *elem;
	struct kmem_cache *slab;

	slab = kmem_cache_create("qmempool_test4", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* request new elem */
		elem = kmem_cache_alloc(slab, GFP_ATOMIC);
		if (elem == NULL)
			goto out;

		barrier(); /* compiler barrier */

		/* return elem */
		kmem_cache_free(slab, elem);
		loops_cnt++;
	}
out:
	time_bench_stop(rec, loops_cnt);
	/* cleanup */
	kmem_cache_destroy(slab);
	return loops_cnt;
}

static __always_inline int __benchmark_qmempool_fastpath_reuse(
	struct time_bench_record *rec, void *data,
	enum behavior_type type)
{
	uint64_t loops_cnt = 0;
	int i;
	struct my_elem *elem, *elem2;
	struct kmem_cache *slab;
	struct qmempool *pool;

	slab = kmem_cache_create("qmempool_test4", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);

	pool = qmempool_create(32, 128, 16, slab, GFP_ATOMIC);
	if (pool == NULL) {
		kmem_cache_destroy(slab);
		return false;
	}

	// "warm-up"
	elem  = qmempool_alloc(pool, GFP_ATOMIC);
	elem2 = qmempool_alloc(pool, GFP_ATOMIC);
	qmempool_free(pool, elem);
	qmempool_free(pool, elem2);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* request new elem */
		if (type == NORMAL) {
			elem = qmempool_alloc(pool, GFP_ATOMIC);
		} else if (type == NORMAL_INLINE) {
			elem = __qmempool_alloc(pool, GFP_ATOMIC);
		} else if (type == SOFTIRQ) {
			elem = qmempool_alloc_softirq(pool, GFP_ATOMIC);
		} else if (type == SOFTIRQ_INLINE) {
			elem = __qmempool_alloc_softirq(pool, GFP_ATOMIC);
		} else {
			BUILD_BUG();
		}
		if (elem == NULL)
			goto out;

		barrier(); /* compiler barrier */

		/* return elem */
		if (type == NORMAL) {
			qmempool_free(pool, elem);
		} else if (type == NORMAL_INLINE) {
			__qmempool_free(pool, elem);
		} else if (type == SOFTIRQ) {
			qmempool_free_softirq(pool, elem);
		} else if (type == SOFTIRQ_INLINE) {
			__qmempool_free_softirq(pool, elem);
		} else {
			BUILD_BUG();
		}
		loops_cnt++;
	}
out:
	time_bench_stop(rec, loops_cnt);
	/* cleanup */
	qmempool_destroy(pool);
	kmem_cache_destroy(slab);
	return loops_cnt;
}
/* Compiler should inline optimize other function "type" calls out */
int benchmark_qmempool_fastpath_reuse_BH(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_qmempool_fastpath_reuse(rec, data, NORMAL);
}
int benchmark_qmempool_fastpath_reuse_BH_inline(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_qmempool_fastpath_reuse(rec, data, NORMAL_INLINE);
}
int benchmark_qmempool_fastpath_reuse_softirq(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_qmempool_fastpath_reuse(rec, data, SOFTIRQ);
}
int benchmark_qmempool_fastpath_reuse_softirq_inline(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_qmempool_fastpath_reuse(rec, data, SOFTIRQ_INLINE);
}

/* Keeping elements in a simple array to avoid too much interference
 * with test */
//#define ARRAY_MAX_ELEMS 128
#define ARRAY_MAX_ELEMS 256
//#define ARRAY_MAX_ELEMS 256+128
//#define ARRAY_MAX_ELEMS 1024
struct my_elem *elems[ARRAY_MAX_ELEMS];

static int benchmark_kmem_cache_pattern(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	int i, n;
	//size_t elem_sz = sizeof(*elems[0]); // == 232 bytes
	struct kmem_cache *slab;

	slab = kmem_cache_create("qmempool_test", sizeof(*elems[0]),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!slab)
		return 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* alloc N new elems */
		for (n = 0; n < ARRAY_MAX_ELEMS; n++) {
			elems[n] = kmem_cache_alloc(slab, GFP_ATOMIC);
		}

		barrier(); /* compiler barrier */

		/* free N elems */
		for (n = 0; n < ARRAY_MAX_ELEMS; n++) {
			kmem_cache_free(slab, elems[n]);
			loops_cnt++;
		}
	}
	time_bench_stop(rec, loops_cnt);

	/* cleanup */
	kmem_cache_destroy(slab);
	return loops_cnt;
}

/* Compiler should inline optimize other function "type" calls out */
static __always_inline int __benchmark_qmempool_pattern(
	struct time_bench_record *rec, void *data,
	enum behavior_type type)
{
	uint64_t loops_cnt = 0;
	int i, n;
	struct kmem_cache *slab;
	struct qmempool *pool;

	slab = kmem_cache_create("qmempool_test", sizeof(*elems[0]),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!slab)
		return 0;
	//pool = qmempool_create(32, 128, 0, slab, GFP_ATOMIC);
	pool = qmempool_create(32, 256, 0, slab, GFP_ATOMIC);
	//pool = qmempool_create(32, 1024, 0, slab, GFP_ATOMIC);
	if (pool == NULL) {
		kmem_cache_destroy(slab);
		return 0;
	}

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* alloc N new elems */
		for (n = 0; n < ARRAY_MAX_ELEMS; n++) {
			if (type == NORMAL) {
				elems[n] = qmempool_alloc(pool, GFP_ATOMIC);
			} else if (type == NORMAL_INLINE) {
				elems[n] = __qmempool_alloc(pool, GFP_ATOMIC);
			} else if (type == SOFTIRQ) {
				elems[n] =
				qmempool_alloc_softirq(pool, GFP_ATOMIC);
			} else if (type == SOFTIRQ_INLINE) {
				elems[n] =
				__qmempool_alloc_softirq(pool, GFP_ATOMIC);
			} else {
				BUILD_BUG();
			}
			barrier(); /* compiler barrier */
		}

		barrier(); /* compiler barrier */

		/* free N elems */
		for (n = 0; n < ARRAY_MAX_ELEMS; n++) {
			if (type == NORMAL) {
				qmempool_free(pool, elems[n]);
			} else if (type == NORMAL_INLINE) {
				__qmempool_free(pool, elems[n]);
			} else if (type == SOFTIRQ) {
				qmempool_free_softirq(pool, elems[n]);
			} else if (type == SOFTIRQ_INLINE) {
				__qmempool_free_softirq(pool, elems[n]);
			} else {
				BUILD_BUG();
			}
			barrier(); /* compiler barrier */
			loops_cnt++;
		}
	}
	time_bench_stop(rec, loops_cnt);

	print_qstats(pool, __func__, "ZZZ");

	/* cleanup */
	qmempool_destroy(pool);
	kmem_cache_destroy(slab);
	return loops_cnt;
}
int benchmark_qmempool_pattern(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_qmempool_pattern(rec, data, NORMAL);
}
int benchmark_qmempool_pattern_inline(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_qmempool_pattern(rec, data, NORMAL_INLINE);
}
int benchmark_qmempool_pattern_softirq(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_qmempool_pattern(rec, data, SOFTIRQ);
}
int benchmark_qmempool_pattern_softirq_inline(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_qmempool_pattern(rec, data, SOFTIRQ_INLINE);
}


bool run_micro_benchmark_tests(void)
{
	uint32_t loops = 1000000;

	/* Results listed below for a E5-2695 CPU */
	pr_info("Measured cost of doing alloc+free:\n");

	/* Results:
	 *  17.939 ns with SUB   CONFIG_PREEMPT=n
	 *  19.313 ns with SLUB  CONFIG_PREEMPT=n PREEMPT_COUNT=y
	 *  20.756 ns with SLUB  CONFIG_PREEMPT=y
	 */
	time_bench_loop(loops*30, 0, "kmem fastpath reuse", NULL,
			benchmark_kmem_cache_fastpath_reuse);

	/* Qmempool fastpath */
	time_bench_loop(loops*30, 0, "qmempool fastpath BH-disable", NULL,
			benchmark_qmempool_fastpath_reuse_BH);
	time_bench_loop(loops*30, 0, "qmempool fastpath BH-disable+inline",
			NULL, benchmark_qmempool_fastpath_reuse_BH_inline);
	time_bench_loop(loops*30, 0, "qmempool fastpath SOFTIRQ", NULL,
			benchmark_qmempool_fastpath_reuse_softirq);
	time_bench_loop(loops*30, 0, "qmempool fastpath SOFTIRQ+inline", NULL,
			benchmark_qmempool_fastpath_reuse_softirq_inline);

	pr_info("N-pattern with %d elements\n", ARRAY_MAX_ELEMS);

	/* Results:
	 *  38.046 ns N=256 with SLUB  CONFIG_PREEMPT=n
	 *  37.570 ns N=256 with SLUB  CONFIG_PREEMPT=n PREEMPT_COUNT=y
	 *  39.364 ns N=256 with SLUB  CONFIG_PREEMPT=y
	 */
	time_bench_loop(loops/10, 0, "kmem alloc+free N-pattern", NULL,
			benchmark_kmem_cache_pattern);

	time_bench_loop(loops/10, 0, "qmempool N-pattern",
			NULL, benchmark_qmempool_pattern);
	time_bench_loop(loops/10, 0, "qmempool N-pattern+inline",
			NULL, benchmark_qmempool_pattern_inline);
	time_bench_loop(loops/10, 0, "qmempool N-pattern softirq",
			NULL, benchmark_qmempool_pattern_softirq);
	time_bench_loop(loops/10, 0, "qmempool N-pattern softirq+inline",
			NULL, benchmark_qmempool_pattern_softirq_inline);

	return true;
}

static int __init qmempool_bench_module_init(void)
{
	preempt_disable();
	pr_info("DEBUG: cpu:%d\n", smp_processor_id());
	preempt_enable();

	if (verbose)
		pr_info("Loaded\n");

	run_micro_benchmark_tests();

	return 0;
}
module_init(qmempool_bench_module_init);

static void __exit qmempool_bench_module_exit(void)
{
	// TODO: perform sanity checks, and free mem
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(qmempool_bench_module_exit);

MODULE_DESCRIPTION("Micro Benchmarking of qmempool");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

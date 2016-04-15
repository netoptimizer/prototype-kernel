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

static int parallel_cpus = 0;
module_param(parallel_cpus, uint, 0);
MODULE_PARM_DESC(parallel_cpus, "Number of parallel CPUs (default ALL)");

/* Quick and dirty way to unselect some of the benchmark tests, by
 * encoding this in a module parameter flag.  This is useful when
 * wanting to perf benchmark a specific benchmark test.
 *
 * Hint: Bash shells support writing binary number like: $((2#101010))
 * Use like:
 *  modprobe $MODULE parallel_cpus=4 run_flags=$((2#101))
 */
static unsigned long long run_flags = 0xFFFFFFFF;
module_param(run_flags, ullong, 0);
MODULE_PARM_DESC(run_flags, "Hack way to limit bench to run");
/* Count the bit number from the enum */
enum benchmark_bit {
	bit_run_bench_fastpath_slab,
	bit_run_bench_fastpath_qmempool,
	bit_run_bench_N_pattern_slab,
	bit_run_bench_N_pattern_qmempool,
};
#define bit(b)	(1 << (b))
#define run_or_return(b) do { if (!(run_flags & (bit(b)))) return; } while (0)

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
	struct kmem_cache *slab = data;

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
	return loops_cnt;
}

static __always_inline int __benchmark_qmempool_fastpath_reuse(
	struct time_bench_record *rec, void *data,
	enum behavior_type type)
{
	uint64_t loops_cnt = 0;
	int i;
	struct my_elem *elem, *elem2;
	struct qmempool *pool = data;

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
///#define ARRAY_MAX_ELEMS 256
//#define ARRAY_MAX_ELEMS 256+128
#define ARRAY_MAX_ELEMS 1024
// struct my_elem *elems[ARRAY_MAX_ELEMS];

static int benchmark_kmem_cache_pattern(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	int i, n;
	//size_t elem_sz = sizeof(*elems[0]); // == 232 bytes
	struct kmem_cache *slab = data;
	struct my_elem **elems;

	elems = kzalloc(sizeof(void*) * ARRAY_MAX_ELEMS, GFP_KERNEL);

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
	kfree(elems);
	return loops_cnt;
}

/* Compiler should inline optimize other function "type" calls out */
static __always_inline int __benchmark_qmempool_pattern(
	struct time_bench_record *rec, void *data,
	enum behavior_type type)
{
	struct qmempool *pool = data;
	struct my_elem **elems;
	uint64_t loops_cnt = 0;
	int i, n;

	elems = kzalloc(sizeof(void*) * ARRAY_MAX_ELEMS, GFP_KERNEL);

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
	kfree(elems);
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

int run_parallel(const char *desc, uint32_t loops, const cpumask_t *cpumask,
		 int step, void *data,
		 int (*func)(struct time_bench_record *record, void *data)
	)
{
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	size_t size;

	/* Allocate records for every CPU */
	size = sizeof(*cpu_tasks) * num_possible_cpus();
	cpu_tasks = kzalloc(size, GFP_KERNEL);

	time_bench_run_concurrent(loops, step, data,
				  cpumask, &sync, cpu_tasks, func);
	time_bench_print_stats_cpumask(desc, cpu_tasks, cpumask);

	kfree(cpu_tasks);
	return 1;
}

void noinline run_bench_fastpath_slab(uint32_t loops, cpumask_t cpumask)
{
	struct kmem_cache *slab;
	run_or_return(bit_run_bench_fastpath_slab);

	slab = kmem_cache_create("qmempool_test4", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);

	run_parallel("benchmark_kmem_cache_fastpath_reuse", loops, &cpumask,
		     0, slab,
		      benchmark_kmem_cache_fastpath_reuse);
	/* Single CPU comparison */
	time_bench_loop(loops*30, 0, "kmem fastpath reuse", slab,
			benchmark_kmem_cache_fastpath_reuse);

	kmem_cache_destroy(slab);
}

void noinline run_bench_fastpath_qmempool(uint32_t loops, cpumask_t cpumask)
{
	struct kmem_cache *slab;
	struct qmempool *pool;

	run_or_return(bit_run_bench_fastpath_qmempool);

	slab = kmem_cache_create("qmempool_test4", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!slab)
		return;

	pool = qmempool_create(32, 128, 16, slab, GFP_ATOMIC);
	if (pool == NULL) {
		kmem_cache_destroy(slab);
		return;
	}

	/* Qmempool fastpath */
	run_parallel("parallel_qmempool_fastpath_reuse_softirq_inline",
		     loops, &cpumask, 0, pool,
		     benchmark_qmempool_fastpath_reuse_softirq_inline);

	/* For comparison */
	time_bench_loop(loops*30, 0, "qmempool fastpath BH-disable", pool,
			benchmark_qmempool_fastpath_reuse_BH);
	time_bench_loop(loops*30, 0, "qmempool fastpath BH-disable+inline",
			pool, benchmark_qmempool_fastpath_reuse_BH_inline);
	time_bench_loop(loops*30, 0, "qmempool fastpath SOFTIRQ", pool,
			benchmark_qmempool_fastpath_reuse_softirq);
	time_bench_loop(loops*30, 0, "qmempool fastpath SOFTIRQ+inline", pool,
			benchmark_qmempool_fastpath_reuse_softirq_inline);
	/* cleanup */
	qmempool_destroy(pool);
	kmem_cache_destroy(slab);
}

void noinline run_bench_N_pattern_slab(uint32_t loops, cpumask_t cpumask)
{
	struct kmem_cache *slab;
	run_or_return(bit_run_bench_N_pattern_slab);

	slab = kmem_cache_create("qmempool_test", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!slab)
		return;

	run_parallel("parallel_kmem_cache_pattern",
		     loops, &cpumask, 0, slab,
		     benchmark_kmem_cache_pattern);

	time_bench_loop(loops/10, 0, "benchmark_kmem_cache_pattern", slab,
			benchmark_kmem_cache_pattern);
	/* cleanup */
	kmem_cache_destroy(slab);
}

void noinline run_bench_N_pattern_qmempool(uint32_t loops, cpumask_t cpumask)
{
	struct kmem_cache *slab;
	struct qmempool *pool;
	run_or_return(bit_run_bench_N_pattern_qmempool);

	slab = kmem_cache_create("qmempool_test", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!slab)
		return;
	//pool = qmempool_create(32, 256, 0, slab, GFP_ATOMIC);
	//pool = qmempool_create(32, 256*8, 0, slab, GFP_ATOMIC);

	/* For now, only test qmempool part, don't hit slab */
	//pool = qmempool_create(64,256*num_possible_cpus(),
	pool = qmempool_create(64, ARRAY_MAX_ELEMS *num_possible_cpus(),
			       0, slab, GFP_ATOMIC);
	if (pool == NULL) {
		kmem_cache_destroy(slab);
		return;
	}

	run_parallel("parallel_qmempool_pattern_softirq_inline",
		     loops, &cpumask, 0, pool,
		     benchmark_qmempool_pattern_softirq_inline);

	time_bench_loop(loops/10, 0, "qmempool N-pattern",
			pool, benchmark_qmempool_pattern);
	time_bench_loop(loops/10, 0, "qmempool N-pattern+inline",
			pool, benchmark_qmempool_pattern_inline);
	time_bench_loop(loops/10, 0, "qmempool N-pattern softirq",
			pool, benchmark_qmempool_pattern_softirq);
	time_bench_loop(loops/10, 0, "qmempool N-pattern softirq+inline",
			pool, benchmark_qmempool_pattern_softirq_inline);

	/* cleanup */
	qmempool_destroy(pool);
	kmem_cache_destroy(slab);
}

bool run_micro_benchmark_tests(void)
{
	uint32_t loops = 100000;
	cpumask_t cpumask;
	int i;

	/* Default run on all (online) CPUs */
	cpumask_copy(&cpumask, cpu_online_mask);

	/* Reduce CPUs to run on, via module parameter parallel_cpus */
	if (parallel_cpus != 0) {
		if (verbose)
			pr_info("Limit to %d parallel CPUs\n", parallel_cpus);
		cpumask_clear(&cpumask);
		for (i = 0; i < parallel_cpus ; i++) {
			cpumask_set_cpu(i, &cpumask);
		}
	}

	/* Selectable test types, see run_flags module parameter */
	run_bench_fastpath_slab(loops, cpumask);
	run_bench_fastpath_qmempool(loops, cpumask);

	pr_info("N-pattern with %d elements\n", ARRAY_MAX_ELEMS);
	run_bench_N_pattern_slab(loops, cpumask);
	run_bench_N_pattern_qmempool(loops, cpumask);

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

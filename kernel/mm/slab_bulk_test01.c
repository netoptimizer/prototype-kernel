/*
 * Synthetic micro-benchmarking of slab bulk
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/skbuff.h>

static int verbose=1;

/* If SLAB debugging is enabled the per object cost is approx a factor
 * between 500 - 1000 times slower.  Thus, adjust the default number
 * of loops in case CONFIG_SLUB_DEBUG_ON=y
 */
#ifdef CONFIG_SLUB_DEBUG_ON
# define DEFAULT_LOOPS 10000
#else
# define DEFAULT_LOOPS 10000000
#endif
static uint32_t loops = DEFAULT_LOOPS;
module_param(loops, uint, 0);
MODULE_PARM_DESC(loops, "Parameter for loops in bench");

struct my_elem {
	/* element used for benchmark testing */
	struct sk_buff skb;
};

/* Timing at the nanosec level, we need to know the overhead
 * introduced by the for loop itself */
static int time_bench_for_loop(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier(); /* avoid compiler to optimize this loop */
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

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

	slab = kmem_cache_create("slab_bench_test1", sizeof(struct my_elem),
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

		/* NOTICE THIS COUNTS alloc+free together*/
		loops_cnt++;
	}
out:
	time_bench_stop(rec, loops_cnt);
	/* cleanup */
	kmem_cache_destroy(slab);
	return loops_cnt;
}

/* Fallback versions copy-pasted here, as they are defined in
 * slab_common that we cannot link with.
 *
 * Force them to be "noinlined" as current patch for slab_common cause
 * them to be a function call.  To keep comparison the same.
 */
noinline
void my__kmem_cache_free_bulk(struct kmem_cache *s, size_t nr, void **p)
{
	size_t i;

	for (i = 0; i < nr; i++)
		kmem_cache_free(s, p[i]);
}
noinline
bool my__kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t nr,
								void **p)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		void *x = p[i] = kmem_cache_alloc(s, flags);
		if (!x) {
			my__kmem_cache_free_bulk(s, i, p);
			return false;
		}
	}
	return true;
}

static int benchmark_slab_fallback_bulk(
	struct time_bench_record *rec, void *data)
{
#define MAX_BULK 250
	void *objs[MAX_BULK];
	uint64_t loops_cnt = 0;
	int i;
	bool success;
	struct kmem_cache *slab;
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

	slab = kmem_cache_create("slab_bench_test2", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* request bulk elems */
		success = my__kmem_cache_alloc_bulk(slab, GFP_ATOMIC, bulk, objs);
		if (!success)
			goto out;

		barrier(); /* compiler barrier */

		/* bulk return elems */
		my__kmem_cache_free_bulk(slab, bulk, objs);

		/* NOTICE THIS COUNTS (bulk) alloc+free together*/
		loops_cnt+= bulk;
	}
out:
	time_bench_stop(rec, loops_cnt);
	/* cleanup */
	kmem_cache_destroy(slab);
	return loops_cnt;
#undef MAX_BULK
}

static int benchmark_slab_bulk01(
	struct time_bench_record *rec, void *data)
{
#define MAX_BULK 250
	void *objs[MAX_BULK];
	uint64_t loops_cnt = 0;
	int i;
	bool success;
	struct kmem_cache *slab;
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

	slab = kmem_cache_create("slab_bench_test3", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* request bulk elems */
		success = kmem_cache_alloc_bulk(slab, GFP_ATOMIC, bulk, objs);
		if (!success)
			goto out;

		barrier(); /* compiler barrier */

		/* bulk return elems */
		kmem_cache_free_bulk(slab, bulk, objs);

		/* NOTICE THIS COUNTS (bulk) alloc+free together*/
		loops_cnt+= bulk;
	}
out:
	time_bench_stop(rec, loops_cnt);
	/* cleanup */
	kmem_cache_destroy(slab);
	return loops_cnt;
#undef MAX_BULK
}

void bulk_test(int bulk)
{
	time_bench_loop(loops/bulk, bulk, "kmem bulk_fallback", NULL,
			benchmark_slab_fallback_bulk);
	cond_resched();

	time_bench_loop(loops/bulk, bulk, "kmem bulk_quick_reuse", NULL,
			benchmark_slab_bulk01);
	cond_resched();
}

int run_timing_tests(void)
{
	time_bench_loop(loops*10, 0, "for_loop",
			NULL, time_bench_for_loop);

	time_bench_loop(loops, 0, "kmem fastpath reuse", NULL,
			benchmark_kmem_cache_fastpath_reuse);

	bulk_test(1);
	bulk_test(2);
	bulk_test(3);
	bulk_test(4);

	bulk_test(8);
	bulk_test(16);
	bulk_test(30);
	bulk_test(32);
	bulk_test(34);
	bulk_test(48);
	bulk_test(64);
	bulk_test(128);
	bulk_test(128+30);
	bulk_test(250);

	return 0;
}


static int __init slab_bulk_test01_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	preempt_disable();
	pr_info("DEBUG: cpu:%d\n", smp_processor_id());
	preempt_enable();

#ifdef CONFIG_DEBUG_PREEMPT
	pr_warn("WARN: CONFIG_DEBUG_PREEMPT is enabled: this affect results\n");
#endif
#ifdef CONFIG_PREEMPT
	pr_warn("INFO: CONFIG_PREEMPT is enabled\n");
#endif
#ifdef CONFIG_PREEMPT_COUNT
	pr_warn("INFO: CONFIG_PREEMPT_COUNT is enabled\n");
#endif

	if (run_timing_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(slab_bulk_test01_module_init);

static void __exit slab_bulk_test01_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(slab_bulk_test01_module_exit);

MODULE_DESCRIPTION("Synthetic micro-benchmarking of slab bulk");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

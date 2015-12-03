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

static unsigned int bulksz = 16;
module_param(bulksz, uint, 0);
MODULE_PARM_DESC(bulksz, "Parameter for setting bulk size to bench");

#if defined(CONFIG_SLUB_DEBUG_ON) || defined(CONFIG_DEBUG_SLAB)
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

#define MAX_BULK 32768
void *GLOBAL_OBJS[MAX_BULK];

static int benchmark_slab_bulk(
	struct time_bench_record *rec, void *data)
{
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

	slab = kmem_cache_create("slab_bulk_test02", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* request bulk elems */
		success = kmem_cache_alloc_bulk(slab, GFP_ATOMIC, bulk,
						GLOBAL_OBJS);
		if (!success)
			goto out;

		barrier(); /* compiler barrier */

		/* bulk return elems */
		kmem_cache_free_bulk(slab, bulk, GLOBAL_OBJS);

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
	time_bench_loop(loops, bulk, "kmem_cache_(free+alloc)_bulk", NULL,
			benchmark_slab_bulk);
}

int run_timing_tests(void)
{
	pr_info("Bench bulk size:%d\n", bulksz);
	bulk_test(bulksz);

	return 0;
}


static int __init slab_bulk_test02_module_init(void)
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
module_init(slab_bulk_test02_module_init);

static void __exit slab_bulk_test02_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(slab_bulk_test02_module_exit);

MODULE_DESCRIPTION("Synthetic micro-benchmarking of slab bulk");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

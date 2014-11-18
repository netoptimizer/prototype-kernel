/*
 * Benchmarking kmem_cache slab/slub
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time_bench.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/mm.h>

static int verbose=1;

struct my_elem {
	/* element used for benchmark testing */
	struct sk_buff skb;
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

	slab = kmem_cache_create("time_bench_test1", sizeof(struct my_elem),
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

int run_timing_tests(void)
{
	uint32_t loops = 100000000;

	time_bench_loop(loops, 0, "kmem fastpath reuse", NULL,
			benchmark_kmem_cache_fastpath_reuse);

	return 0;
}

static int __init time_bench_sample_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_timing_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(time_bench_sample_module_init);

static void __exit time_bench_sample_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(time_bench_sample_module_exit);

MODULE_DESCRIPTION("Benchmark kmem_cache/slab/slub");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

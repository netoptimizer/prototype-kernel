/*
 * Benchmark module for page_pool.
 *
 * Cross CPU tests.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/time_bench.h>
#include <net/page_pool.h>

#include <linux/interrupt.h>
#include <linux/limits.h>

/* notice time_bench is limited to U32_MAX nr loops */
static unsigned long loops = 10000000;
module_param(loops, ulong, 0);
MODULE_PARM_DESC(loops, "Specify loops bench will run");

static int verbose=1;
#define MY_POOL_SIZE	1024

/*
 * Benchmark idea:
 *
 * One process simulate NIC-RX, which needs to allocate page to refill it's
 * RX-ring. This needs to run under softirq (here as taskset).
 *
 * Multiple other processes, running on remote CPUs, will return pages into
 * the page_pool. Simulation a page getting freed from a remote CPU, being
 * returned to page_pool.  These should runs under softirq, as this usually
 * happens during DMA TX completion, but we can live with not using softirq.
 *
 * Issue #1: Real struct "page".
 *
 * The object used in the benchmark needs to be real struct page'es, due to
 * functions used by page_pool, like page_to_nid(page), page_ref_count(page),
 * page_is_pfmemalloc().  It would have been easier if we could use dummy
 * pointers.
 *
 * Issue #2:
 *
 * The objects/pages need to be returned from a remote CPU, obviously need to
 * be transfered from the originating CPU first.  This step must not be the
 * bottleneck, as it is the page_pool return-bottleneck that we want to
 * benchmark.
 *
 * Solution idea for Issue #2"
 *
 * Create multiple ptr_ring's one for each remote-CPU, in-practis creating
 * SPSC queues, which should be faster than the page_pool MPSC ptr_ring
 * setup.  These queues will have a bounded size, which will be the limiting
 * factor for refill-simulator CPU.
 *
 */


static int __init bench_page_pool_cross_cpu_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (loops > U32_MAX) {
		pr_err("Module param loops(%lu) exceeded U32_MAX(%u)\n",
		       loops, U32_MAX);
		return -ECHRNG;
	}

	return 0;
}
module_init(bench_page_pool_cross_cpu_module_init);

static void __exit bench_page_pool_cross_cpu_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(bench_page_pool_cross_cpu_module_exit);

MODULE_DESCRIPTION("Benchmark of page_pool cross-CPU cases");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

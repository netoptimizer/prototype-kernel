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

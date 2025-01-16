/*
 * Benchmarking page allocator execution time inside the kernel
 *  - PoC for walking all pages in the kernel
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include <linux/mmzone.h>
#include <linux/memory_hotplug.h>

static int verbose=1;

static uint32_t loops = 100000;
module_param(loops, uint, 0);
MODULE_PARM_DESC(loops, "Iteration loops");

static int time_single_page_alloc_free(
	struct time_bench_record *rec, void *data)
{
	gfp_t gfp_mask = (GFP_ATOMIC | ___GFP_NORETRY);
	struct page *my_page;
	int i;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		my_page = alloc_page(gfp_mask);
		if (unlikely(my_page == NULL))
			return 0;
		__free_page(my_page);
	}
	time_bench_stop(rec, i);
	return i;
}


#define _pfn_to_online_page(pfn)			\
({						\
	struct page *___page = NULL;		\
	if (pfn_valid(pfn))			\
		___page = pfn_to_page(pfn);	\
	___page;				\
 })

static int time_walk_all_pages(
	struct time_bench_record *rec, void *data)
{
	struct zone *zone;
	int i = 0;

#ifdef CONFIG_MEMORY_HOTPLUG
#warning "Incorrect locking for CONFIG_MEMORY_HOTPLUG"
#endif
	pr_info("%s(): start\n", __func__);

	time_bench_start(rec);

	/** Loop to measure **/
	/*
	 * Struct page scanning for each node.
	 */
	// get_online_mems(); // ignore CONFIG_MEMORY_HOTPLUG locking
	for_each_populated_zone(zone) {
		unsigned long start_pfn = zone->zone_start_pfn;
		unsigned long end_pfn = zone_end_pfn(zone);
		unsigned long pfn;

		for (pfn = start_pfn; pfn < end_pfn; pfn++) {
			struct page *page = pfn_to_online_page(pfn);

//			if (!(pfn & 63))
//				cond_resched();

			if (!page)
				continue;

			/* only scan pages belonging to this zone */
			if (page_zone(page) != zone)
				continue;

			i++;

			/* only scan if page is in use */
			if (page_count(page) == 0)
				continue;
			// scan_block(page, page + 1, NULL);
			if ((page->pp_magic & ~0x3UL) == PP_SIGNATURE) {
				// Do something
				barrier();
			}
		}
	}
	// put_online_mems(); // ignore CONFIG_MEMORY_HOTPLUG locking

	time_bench_stop(rec, i);

	pr_info("%s(): i=%d\n", __func__, i);
	return i;
}

int run_timing_tests(void)
{
	time_bench_loop(loops*10, 0, "single_page_alloc_free",
			NULL, time_single_page_alloc_free);

	time_bench_loop(loops, 0, "walk_all_pages",
			NULL, time_walk_all_pages);

	return 0;
}

static int __init page_bench06_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_timing_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(page_bench06_module_init);

static void __exit page_bench06_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(page_bench06_module_exit);

MODULE_DESCRIPTION("Benchmarking full page tabel walk time in kernel");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

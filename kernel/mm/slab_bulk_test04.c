/*
 * Slab memory exhaustion test, alloc lots of memory to get failures
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/delay.h>

static int progress_every_n=100000;

#define MAX_BULK 128
static unsigned int bulksz = 16;
module_param(bulksz, uint, 0);
MODULE_PARM_DESC(bulksz, "Parameter for setting bulk size to test");

static int verbose=1;
module_param(verbose, uint, 0);
MODULE_PARM_DESC(verbose, "How verbose a test run");

struct kmem_cache *slab;
LIST_HEAD(global_list);

static uint32_t max_objects = 2000000;
module_param(max_objects, uint, 0);
MODULE_PARM_DESC(max_objects, "max_objects in test");

static uint32_t msdelay = 200;
module_param(msdelay, uint, 0);
MODULE_PARM_DESC(msdelay, "delay in N ms after memory exhausted");

static uint32_t retries = 0;
module_param(retries, uint, 0);
MODULE_PARM_DESC(retries, "Number of retries af memory");

struct my_elem {
	struct list_head list;
	/* element used for testing */
	char pad[1024-16];
};

struct my_queue {
	struct list_head list;
	u64 len;
	//u64 max_count;
} global_q;

bool obj_bulk_alloc_and_list_add(struct kmem_cache *s, struct my_queue *q)
{
//	struct my_elem *objs[MAX_BULK];
	void *objs[MAX_BULK];
	bool success;
	int i;

	success = kmem_cache_alloc_bulk(s, GFP_KERNEL, bulksz, objs);
	if (!success) {
		pr_err("Could not bulk(%d) alloc more objects\n", bulksz);
		return false;
	}

	for (i = 0; i < bulksz; i++) {
		struct my_elem *object = objs[i];

//		INIT_LIST_HEAD(&object->list);
		list_add_tail(&object->list, &q->list);
		q->len++;
	}
	return true;
}

bool run_loop(struct kmem_cache *s, struct my_queue *q)
{
	bool success = true;
	struct my_elem *obj, *obj_tmp;
	u64 still_retry = retries;
	u64 cnt = 0;

	/* BULK alloc loop */
	while ((success || still_retry--) && q->len < max_objects) {
		success = obj_bulk_alloc_and_list_add(s, q);
		if (verbose > 1 && ((q->len % progress_every_n)==0))
			pr_info("Progress allocated: %llu objects\n", q->len);
	}
	if (verbose)
		pr_info("Allocated: %llu objects (last success:%d)\n",
			q->len, success);

	msleep(msdelay);

	/* Free all again: Single free, as bulk free cannot fail and
	 * it is only alloc_bulk error handling what we want to test...
	 */
	cnt = 0;
	list_for_each_entry_safe(obj, obj_tmp, &q->list, list) {
		list_del(&obj->list);
		q->len--;
		kmem_cache_free(s, obj);
		cnt++;
		if (verbose > 1 && ((cnt % progress_every_n)==0))
			pr_info("Progress free'ed: %llu objects\n", cnt);
	}
	if (verbose)
		pr_info("Free: %llu objects\n", cnt);

	return success;
}

static int __init slab_bulk_test04_module_init(void)
{
	struct my_elem *object;

	INIT_LIST_HEAD(&global_q.list);
	global_q.len = 0;

	if (verbose)
		pr_info("Loaded\n");

	if (bulksz > MAX_BULK) {
		pr_warn("ERROR: bulksz(%d) too large (> %d)\n",
			bulksz, MAX_BULK);
		return -EINVAL;
	}

	/* Create kmem_cache */
	slab = kmem_cache_create("slab_bulk_test04", sizeof(struct my_elem),
				 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!slab) {
		pr_err("ERROR: could not create slab (kmem_cache_create)\n");
		return -ENOBUFS;
	}

	/* Alloc and free an object from this kmem_cache */
	object = kmem_cache_alloc(slab, GFP_ATOMIC);
	if (!object) {
		pr_err("ERROR: could not alloc object (kmem_cache_alloc)\n");
		return -ENOBUFS;
	}
	kmem_cache_free(slab, object);

	if (!run_loop(slab, &global_q)) {
		pr_info("Successful test: Alloc exceeded memory limit");
	} else {
		pr_err("Invalid test: not exceeded memory limit");
	}

	if (global_q.len != 0) {
		pr_err("ERROR: some objects remain in the global queue");
	}

	return 0;
}
module_init(slab_bulk_test04_module_init);

static void __exit slab_bulk_test04_module_exit(void)
{
	/* Cleanup, destroy the kmem_cache*/
	kmem_cache_destroy(slab);

	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(slab_bulk_test04_module_exit);

MODULE_DESCRIPTION("Slab mem exhaustion test, alloc memory until failure");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

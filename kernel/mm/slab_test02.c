/*
 * Basic slab test of create and destroy of a kmem_cache
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/skbuff.h>

static int verbose=1;
struct kmem_cache *slab;

struct my_elem {
	/* element used for testing */
	struct sk_buff skb;
};

static int __init slab_test02_module_init(void)
{
	struct my_elem *object;

	if (verbose)
		pr_info("Loaded\n");

	/* Create kmem_cache */
	slab = kmem_cache_create("slab_test02", sizeof(struct my_elem),
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

	/* Destroying the kmem_cache really quickly after creating,
	 * could provoke a bug for kmem cgroups in memcg_create_kmem_cache
	 */
	kmem_cache_destroy(slab);
	return 0;
}
module_init(slab_test02_module_init);

static void __exit slab_test02_module_exit(void)
{
	/* Cleanup, destroy the kmem_cache*/
//	kmem_cache_destroy(slab);

	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(slab_test02_module_exit);

MODULE_DESCRIPTION("Basic slab test of create and destroy");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

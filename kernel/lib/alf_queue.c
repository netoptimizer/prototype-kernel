/*
 * lib/alf_queue.c
 *
 * ALF: Array-based Lock-Free queue
 *  - Main implementation in: include/linux/alf_queue.h
 *
 * Copyright (C) 2014, Red Hat, Inc.,
 *  by Jesper Dangaard Brouer and Hannes Frederic Sowa
 *  for licensing details see kernel-base/COPYING
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h> /* kzalloc */
#include <linux/alf_queue.h>

static int verbose=0;

/* True if x is a power of 2 */
#define POWEROF2(x) (((x) & ((x) - 1)) == 0)

struct alf_queue * alf_queue_alloc(u32 size, gfp_t gfp)
{
	struct alf_queue *q;
	size_t mem_size;

	if (!(POWEROF2(size)) || size > 65536)
		return ERR_PTR(-EINVAL);

	/* The ring array is allocated together with the queue struct
	 */
	mem_size = size * sizeof(void *) + sizeof(struct alf_queue);
	q = kzalloc(mem_size, gfp);
	if (!q)
		return ERR_PTR(-ENOMEM);

	q->size = size;
	q->mask = size - 1;

	if (verbose)
		pr_info("%s() queue mem usage: %lu\n", __func__, mem_size);

	return q;
}
EXPORT_SYMBOL_GPL(alf_queue_alloc);

void alf_queue_free(struct alf_queue *q)
{
	kfree(q);
}
EXPORT_SYMBOL_GPL(alf_queue_free);

static int __init alf_queue_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");
	return 0;
}
module_init(alf_queue_module_init);

static void __exit alf_queue_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(alf_queue_module_exit);

MODULE_DESCRIPTION("ALF: Array-based Lock-Free queue");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

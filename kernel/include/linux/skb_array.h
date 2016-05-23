/*
 *	Author:
 *		Michael S. Tsirkin <mst@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	This is a limited-size FIFO maintaining sk_buff structures in FIFO
 *	order, with one CPU producing entries and another consuming entries
 *	from a FIFO.
 *
 *	This implementation tries to minimize cache-contention when there is a
 *	single producer and a single consumer CPU.
 */

#ifndef _LINUX_SKB_ARRAY_H
#define _LINUX_SKB_ARRAY_H 1

#ifdef __KERNEL__
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include <asm/errno.h>
#endif

struct sk_buff;

struct skb_array {
	int producer ____cacheline_aligned_in_smp;
	spinlock_t producer_lock;
	int consumer ____cacheline_aligned_in_smp;
	spinlock_t consumer_lock;
	/* Shared consumer/producer data */
	int size ____cacheline_aligned_in_smp; /* max entries in queue */
	struct sk_buff **queue;
};

/* Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax().
 */
static inline bool __skb_array_full(struct skb_array *a)
{
	return a->queue[a->producer];
}

/* Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax().
 */
static inline int __skb_array_produce(struct skb_array *a,
				       struct sk_buff *skb)
{
	if (__skb_array_full(a))
		return -ENOSPC;

	a->queue[a->producer++] = skb;
	if (unlikely(a->producer >= a->size))
		a->producer = 0;
	return 0;
}

static inline int skb_array_produce_bh(struct skb_array *a,
				       struct sk_buff *skb)
{
	int ret;

	spin_lock_bh(&a->producer_lock);
	ret = __skb_array_produce(a, skb);
	spin_unlock_bh(&a->producer_lock);

	return ret;
}

/* Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax().
 */
static inline struct sk_buff *__skb_array_peek(struct skb_array *a)
{
	return a->queue[a->consumer];
}

/* Must only be called after __skb_array_peek returned !NULL */
static inline void __skb_array_consume(struct skb_array *a)
{
	a->queue[a->consumer++] = NULL;
	if (unlikely(a->consumer >= a->size))
		a->consumer = 0;
}

static inline struct sk_buff *skb_array_consume_bh(struct skb_array *a)
{
	struct sk_buff *skb;

	spin_lock_bh(&a->consumer_lock);
	skb = __skb_array_peek(a);
	if (skb)
		__skb_array_consume(a);
	spin_unlock_bh(&a->consumer_lock);

	return skb;
}

static inline int skb_array_init(struct skb_array *a, int size, gfp_t gfp)
{
	a->queue = kzalloc(ALIGN(size * sizeof *(a->queue), SMP_CACHE_BYTES),
			   gfp);
	if (!a->queue)
		return -ENOMEM;

	a->size = size;
	a->producer = a->consumer = 0;
	spin_lock_init(&a->producer_lock);
	spin_lock_init(&a->consumer_lock);

	return 0;
}

static inline void skb_array_cleanup(struct skb_array *a)
{
	kfree(a->queue);
}

#endif /* _LINUX_SKB_ARRAY_H  */

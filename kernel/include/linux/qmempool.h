/*
 * qmempool - a quick queue based mempool
 *
 * A quick queue-based memory pool, that functions as a cache in-front
 * of kmem_cache SLAB/SLUB allocators.  Which allows faster than
 * SLAB/SLUB reuse/caching of fixed size memory elements
 *
 * The speed gain comes from, the shared storage, using a Lock-Free
 * queue that supports bulk refilling elements (to a percpu cache)
 * with a single cmpxchg.  Thus, the lock-prefixed cmpxchg cost is
 * amortize over the bulk size.
 *
 * The Lock-Free queue is based on an array (of pointer to elements).
 * This make access more cache optimal, as e.g. on 64bit 8 pointers
 * can be stored per cache-line (which is superior to a linked list
 * approach).  Only storing the pointers to elements, is also
 * beneficial as we don't touch the elements data.
 *
 * Qmempool cannot easily replace all kmem_cache usage, because it is
 * restricted in which contexts is can be used in, as the Lock-Free
 * queue is not preemption safe.  This version is optimized for usage
 * from softirq context, and cannot be used from hardirq context.
 *
 * Only support GFP_ATOMIC allocations from SLAB.
 *
 * Copyright (C) 2014, Red Hat, Inc., Jesper Dangaard Brouer
 *  for licensing details see kernel-base/COPYING
 */

#ifndef _LINUX_QMEMPOOL_H
#define _LINUX_QMEMPOOL_H

#include <linux/alf_queue.h>
#include <linux/prefetch.h>
#include <linux/hardirq.h>

/* Bulking is an essential part of the performance gains as this
 * amortize the cost of cmpxchg ops used when accessing sharedq
 */
#define QMEMPOOL_BULK 16
#define QMEMPOOL_REFILL_MULTIPLIER 2

struct qmempool_percpu {
	struct alf_queue *localq;
};

struct qmempool {
	/* The shared queue (sharedq) is a Multi-Producer-Multi-Consumer
	 *  queue where access is protected by an atomic cmpxchg operation.
	 *  The queue support bulk transfers, which amortize the cost
	 *  of the atomic cmpxchg operation.
	 */
	struct alf_queue	*sharedq;

	/* Per CPU local "cache" queues for faster atomic free access.
	 * The local queues (localq) are Single-Producer-Single-Consumer
	 * queues as they are per CPU.
	 */
	struct qmempool_percpu __percpu *percpu;

	/* Backed by some SLAB kmem_cache */
	struct kmem_cache	*kmem;

	/* Setup */
	uint32_t prealloc;
	gfp_t gfp_mask;
};

extern void qmempool_destroy(struct qmempool *pool);
extern struct qmempool* qmempool_create(
	uint32_t localq_sz, uint32_t sharedq_sz, uint32_t prealloc,
	struct kmem_cache *kmem, gfp_t gfp_mask);

extern void* __qmempool_alloc_from_sharedq(
	struct qmempool *pool, gfp_t gfp_mask, struct alf_queue *localq);
extern void* __qmempool_alloc_from_slab(struct qmempool *pool, gfp_t gfp_mask);
extern bool __qmempool_free_to_slab(struct qmempool *pool, void **elems, int n);
extern void __qmempool_free_to_sharedq(void *elem, struct qmempool *pool,
				       struct alf_queue *localq);

/* The percpu variables (SPSC queues) needs preempt protection, and
 * the shared MPMC queue also needs protection against the same CPU
 * access the same queue.
 *
 * Specialize and optimize the qmempool to run from softirq.
 * Don't allow qmempool to be used from interrupt context.
 *
 * IDEA: When used from softirq, take advantage of the protection
 * softirq gives.  A softirq will never preempt another softirq,
 * running on the same CPU.  The only event that can preempt a softirq
 * is an interrupt handler (and perhaps we don't need to support
 * calling qmempool from an interrupt).  Another softirq, even the
 * same one, can run on another CPU however, but these helpers are
 * only protecting our percpu variables.
 *
 * Thus, our percpu variables are safe if current the CPU is the one
 * serving the softirq (tested via in_serving_softirq()), like:
 *
 *  if (!in_serving_softirq())
 *		local_bh_disable();
 *
 * This makes qmempool very fast, when accesses from softirq, but
 * slower when accessed outside softirq.  The other contexts need to
 * disable bottom-halves "bh" via local_bh_{disable,enable} (which on
 * have been measured add cost if 7.5ns on CPU E5-2695).
 *
 * MUST not be used from interrupt context, when relying on softirq usage.
 */
static inline int __qmempool_preempt_disable(void)
{
	int in_serving_softirq = in_serving_softirq();
	if (!in_serving_softirq)
		local_bh_disable();

	return in_serving_softirq;
}

static inline void __qmempool_preempt_enable(int in_serving_softirq)
{
	if (!in_serving_softirq)
		local_bh_enable();
}

/* Elements - alloc and free functions are inlined here for
 * performance reasons, as the per CPU lockless access should be as
 * fast as possible.
 */

/* Main allocation function
 *
 * Caller must make sure this is called from a preemptive safe context
 */
static __always_inline void *
main_qmempool_alloc(struct qmempool *pool, gfp_t gfp_mask)
{
	/* NUMA considerations, for now the numa node is not handles,
	 * this could be handled via e.g. numa_mem_id()
	 */
	void *elem;
	struct qmempool_percpu *cpu;
	int num;

	/* 1. attempt get element from local per CPU queue */
	cpu = this_cpu_ptr(pool->percpu);
	num = alf_sc_dequeue(cpu->localq, (void **)&elem, 1);
	if (num == 1) /* Succes: alloc elem by deq from localq cpu cache */
		return elem;

	/* 2. attempt get element from shared queue.  This involves
	 * refilling the localq for next round. Side-effect can be
	 * alloc from SLAB.
	 */
	elem = __qmempool_alloc_from_sharedq(pool, gfp_mask, cpu->localq);
	return elem;
}

static inline void* __qmempool_alloc(struct qmempool *pool, gfp_t gfp_mask)
{
	void *elem;
	int state;

	state = __qmempool_preempt_disable();
	elem  = main_qmempool_alloc(pool, gfp_mask);
	__qmempool_preempt_enable(state);
	return elem;
}

static inline void* qmempool_alloc_softirq(struct qmempool *pool,
					     gfp_t gfp_mask)
{
	return main_qmempool_alloc(pool, gfp_mask);
}

/* Main free function */
static inline void __qmempool_free(struct qmempool *pool, void *elem)
{
	struct qmempool_percpu *cpu;
	int num;
	int state;

	/* NUMA considerations, how do we make sure to avoid caching
	 * elements from a different NUMA node.
	 */
	state = __qmempool_preempt_disable();

	/* 1. attempt to free/return element to local per CPU queue */
	cpu = this_cpu_ptr(pool->percpu);
	num = alf_sp_enqueue(cpu->localq, &elem, 1);
	if (num == 1) /* success: element free'ed by enqueue to localq */
		goto done;

	/* 2. localq cannot store more elements, need to return some
	 * from localq to sharedq, to make room. Side-effect can be
	 * free to SLAB.
	 */
	__qmempool_free_to_sharedq(elem, pool, cpu->localq);

done:
	__qmempool_preempt_enable(state);
}

/* Allow users control over whether it is optimal to inline qmempool */
#ifdef CONFIG_QMEMPOOL_NOINLINE
extern void* qmempool_alloc(struct qmempool *pool, gfp_t gfp_mask);
extern void qmempool_free(struct qmempool *pool, void *elem);

#else /* !CONFIG_QMEMPOOL_NOINLINE */
static inline void* qmempool_alloc(struct qmempool *pool, gfp_t gfp_mask)
{
	return __qmempool_alloc(pool, gfp_mask);
}
static inline void qmempool_free(struct qmempool *pool, void *elem)
{
	return __qmempool_free(pool, elem);
}
#endif /* CONFIG_QMEMPOOL_NOINLINE */

#endif /* _LINUX_QMEMPOOL_H */

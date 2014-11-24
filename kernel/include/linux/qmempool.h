/*
 * qmempool - a quick queue based mempool
 *
 * A quick queue-based memory pool, that functions as a cache in-front
 * of kmem_cache SLAB/SLUB allocators.  Which allows faster than
 * SLAB/SLUB reuse/caching of fixed size memory elements
 *
 * The speed gain comes from, the shared storage, using a Lock-Free
 * queue that supports bulking elements with a single cmpxchg.
 *
 * The Lock-Free queue is based on an array (of pointer to elements).
 * This make access more cache optimal, as e.g. on 64bit 8 pointers
 * can be stored per cache-line (which is superior to a linked list
 * approach).  Only storing the pointers to elements, is also
 * beneficial as we don't touch the elements data.
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
	/* room for percpu stats */
	uint64_t refill_cnt;
	uint64_t full_cnt; /* push back (to shared) elements when full */
	int owner_cpu;
};

struct qmempool {
	/* The shared queue (sharedq) is a Multi-Producer-Multi-Consumer
	 *  queue where access is protected by an atomic cmpxchg operation.
	 *  The queue support bulk transfers, which amortize the cost
	 *  of the atomic cmpxchg operation.
	 */
	struct alf_queue	*sharedq;

	/* Per CPU local "cache" queues for faster lockfree access.
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

//#define DEBUG_PERCPU 1
static inline void debug_percpu(struct qmempool_percpu *cpu)
{
#ifdef DEBUG_PERCPU
	if (unlikely(cpu->owner_cpu == -1)) {
		cpu->owner_cpu = smp_processor_id();
		return;
	}
	if (cpu->owner_cpu != smp_processor_id()) {
		WARN_ON(1);
		pr_err("BUG localq changed CPU %d -> %d\n",
		       cpu->owner_cpu, smp_processor_id());
	}
#endif /* DEBUG_PERCPU */
}

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
 */
static inline void __qmempool_preempt_disable(void)
{
	if (!in_serving_softirq())
		local_bh_disable();

	/* Cannot be used from interrupt context */
	BUG_ON(in_irq());
}

static inline void __qmempool_preempt_enable(void)
{
	if (!in_serving_softirq())
		local_bh_enable();
}

/* Elements - alloc and free functions are inlined here for
 * performance reasons, as the per CPU lockless access should be as
 * fast as possible.
 */

/* Main allocation function */
static __always_inline void *
__qmempool_alloc_node(struct qmempool *pool, gfp_t gfp_mask, int node)
{
	//TODO: handle numa node stuff e.g. numa_mem_id()
	void *element;
	struct qmempool_percpu *cpu;
	int num;

	__qmempool_preempt_disable();

	/* 1. attempt get element from local per CPU queue */
	cpu = this_cpu_ptr(pool->percpu);
	num = alf_sc_dequeue(cpu->localq, (void **)&element, 1);
	if (num == 1) {
		/* Succesfully alloc elem by deq from localq cpu cache */
		__qmempool_preempt_enable();
		return element;
	}

	/* 2. attempt get element from shared queue.  This involves
	 * refilling the localq for next round.
	 */
	element = __qmempool_alloc_from_sharedq(pool, gfp_mask, cpu->localq);
	if (element) {
		__qmempool_preempt_enable();
		return element;
	}

	/* 3. handle if sharedq runs out of elements (element == NULL)
	 * slab can can sleep if (gfp_mask & __GFP_WAIT), thus must
	 * not run with preemption disabled.
	 */
	__qmempool_preempt_enable();
	element = __qmempool_alloc_from_slab(pool, gfp_mask);
	return element;
}

static inline void* __qmempool_alloc(struct qmempool *pool, gfp_t gfp_mask)
{
	return __qmempool_alloc_node(pool, gfp_mask, -1);
}


/* This func could be defined in qmempool.c, but is kept in header,
 * because all users of __qmempool_preempt_*() are kept in here to
 * easier see how preemption protection is used.
 *
 * MUST be called with __qmempool_preempt_disable()
 *
 * Noinlined because it uses a lot of stack.
 */
static noinline_for_stack bool
__qmempool_free_to_sharedq(struct qmempool *pool, struct alf_queue *localq)
{
	void *elems[QMEMPOOL_BULK]; /* on stack variable */
	int num_enq, num_deq;

	/* This function is called when the localq is full. Thus,
	 * elements from localq needs to be returned to sharedq (or if
	 * shared is full, need to be free'ed for real)
	 */

	/* Make room in localq */
	num_deq = alf_sc_dequeue(localq, elems, QMEMPOOL_BULK);
	if (unlikely(num_deq == 0))
		goto failed;

        /* Successful dequeued 'num_deq' elements from localq, "free"
	 * these elems by enqueuing to sharedq
	 */
	num_enq = alf_mp_enqueue(pool->sharedq, elems, num_deq);
	if (num_enq == num_deq) /* Success enqueued to sharedq */
		return true;

	/* If sharedq is full (num_enq == 0) dequeue elements will be
	 * returned directly to the SLAB allocator.
	 *
	 * Catch if enq API change to allow flexible enq */
	BUG_ON(num_enq > 0);

	/* Allow slab kmem_cache_free() to run with preemption */
	__qmempool_preempt_enable();
	__qmempool_free_to_slab(pool, elems, num_deq);
	return false;
failed:
	/* dequeing from a full localq should always be possible */
	BUG();
	return false;
}

/* Main free function */
static inline void __qmempool_free(struct qmempool *pool, void *elem)
{
	struct qmempool_percpu *cpu;
	int num;
	bool used_sharedq;

	__qmempool_preempt_disable();
	/* NUMA considerations, how do we make sure to avoid caching
	 * elements from a different NUMA node.
	 */

	/* 1. attempt to free/return element to local per CPU queue */
	cpu = this_cpu_ptr(pool->percpu);
	debug_percpu(cpu);
	num = alf_sp_enqueue(cpu->localq, &elem, 1);
	if (num == 1) /* success: element free'ed by enqueue to localq */
		goto done;

	/* IDEA: use a watermark feature, to allow enqueue of
	 * this cache-hot elem, and then return some to sharedq.
	 */

	/* 2. localq cannot store more elements, need to return some
	 * from localq to sharedq, to make room.
	 */
	used_sharedq = __qmempool_free_to_sharedq(pool, cpu->localq);
	if (!used_sharedq) {
		/* preempt got enabled when using real SLAB */
		__qmempool_preempt_disable();
		cpu = this_cpu_ptr(pool->percpu); /* have to reload "cpu" ptr */
	}

	/* 3. this elem is more cache hot, keep it in localq */
	debug_percpu(cpu);
	num = alf_sp_enqueue(cpu->localq, &elem, 1);
	if (unlikely(num == 0)) { /* should have been be room in localq!?! */
		WARN_ON(1);
		pr_err("%s() Why could this happen? localq:%d sharedq:%d"
		       " irqs_disabled:%d in_softirq:%lu cpu:%d\n",
		       __func__, alf_queue_count(cpu->localq),
		       alf_queue_count(pool->sharedq), irqs_disabled(),
		       in_softirq(), smp_processor_id());
		kmem_cache_free(pool->kmem, elem);
	}
done:
	__qmempool_preempt_enable();
}

/* Debug hack to ease profiling functions when not inlined
 */
//#define QMEMPOOL_DEBUG_PROFILING 1
#ifdef QMEMPOOL_DEBUG_PROFILING
extern void* qmempool_alloc(struct qmempool *pool, gfp_t gfp_mask);
extern void* qmempool_alloc_node(struct qmempool *pool, gfp_t gfp_mask, int node);
extern void qmempool_free(struct qmempool *pool, void *elem);
#else /* !QMEMPOOL_DEBUG_PROFILING */
static inline void* qmempool_alloc(struct qmempool *pool, gfp_t gfp_mask)
{
	return __qmempool_alloc_node(pool, gfp_mask, -1);
}
static inline void* qmempool_alloc_node(struct qmempool *pool, gfp_t gfp_mask, int node)
{
	return __qmempool_alloc_node(pool, gfp_mask, -1);
}
static inline void qmempool_free(struct qmempool *pool, void *elem)
{
	return __qmempool_free(pool, elem);
}
#endif /* QMEMPOOL_DEBUG_PROFILING */

#endif /* _LINUX_QMEMPOOL_H */

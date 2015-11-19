/*
 * qmempool - a quick queue based mempool
 *
 * Copyright (C) 2014, Red Hat, Inc., Jesper Dangaard Brouer
 *  for licensing details see kernel-base/COPYING
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/qmempool.h>
#include <linux/log2.h>

/* Due to hotplug CPU support, we need access to all qmempools
 * in-order to cleanup elements in localq for the CPU going offline.
 *
 * TODO: implement HOTPLUG_CPU
#ifdef CONFIG_HOTPLUG_CPU
static LIST_HEAD(qmempool_list);
static DEFINE_SPINLOCK(qmempool_list_lock);
#endif
 */

void qmempool_destroy(struct qmempool *pool)
{
	void *elem = NULL;
	int j;

	if (pool->percpu) {
		for_each_possible_cpu(j) {
			struct qmempool_percpu *cpu =
				per_cpu_ptr(pool->percpu, j);

			while (alf_mc_dequeue(cpu->localq, &elem, 1) == 1)
				kmem_cache_free(pool->kmem, elem);
			BUG_ON(!alf_queue_empty(cpu->localq));
			alf_queue_free(cpu->localq);
		}
		free_percpu(pool->percpu);
	}

	if (pool->sharedq) {
		while (alf_mc_dequeue(pool->sharedq, &elem, 1) == 1)
			kmem_cache_free(pool->kmem, elem);
		BUG_ON(!alf_queue_empty(pool->sharedq));
		alf_queue_free(pool->sharedq);
	}

	kfree(pool);
}
EXPORT_SYMBOL(qmempool_destroy);

struct qmempool *
qmempool_create(uint32_t localq_sz, uint32_t sharedq_sz, uint32_t prealloc,
		struct kmem_cache *kmem, gfp_t gfp_mask)
{
	struct qmempool *pool;
	int i, j, num;
	void *elem;

	/* Validate constraints, e.g. due to bulking */
	if (localq_sz < QMEMPOOL_BULK) {
		pr_err("%s() localq size(%d) too small for bulking\n",
		       __func__, localq_sz);
		return NULL;
	}
	if (sharedq_sz < (QMEMPOOL_BULK * QMEMPOOL_REFILL_MULTIPLIER)) {
		pr_err("%s() sharedq size(%d) too small for bulk refill\n",
		       __func__, sharedq_sz);
		return NULL;
	}
	if (!is_power_of_2(localq_sz) || !is_power_of_2(sharedq_sz)) {
		pr_err("%s() queue sizes (%d/%d) must be power-of-2\n",
		       __func__, localq_sz, sharedq_sz);
		return NULL;
	}
	if (prealloc > sharedq_sz) {
		pr_err("%s() prealloc(%d) req > sharedq size(%d)\n",
		       __func__, prealloc, sharedq_sz);
		return NULL;
	}
	if ((prealloc % QMEMPOOL_BULK) != 0) {
		pr_warn("%s() prealloc(%d) should be div by BULK size(%d)\n",
			__func__, prealloc, QMEMPOOL_BULK);
	}
	if (!kmem) {
		pr_err("%s() kmem_cache is a NULL ptr\n",  __func__);
		return NULL;
	}

	pool = kzalloc(sizeof(*pool), gfp_mask);
	if (!pool)
		return NULL;
	pool->kmem     = kmem;
	pool->gfp_mask = gfp_mask;

	/* MPMC (Multi-Producer-Multi-Consumer) queue */
	pool->sharedq = alf_queue_alloc(sharedq_sz, gfp_mask);
	if (IS_ERR_OR_NULL(pool->sharedq)) {
		pr_err("%s() failed to create shared queue(%d) ERR_PTR:0x%p\n",
		       __func__, sharedq_sz, pool->sharedq);
		qmempool_destroy(pool);
		return NULL;
	}

	pool->prealloc = prealloc;
	for (i = 0; i < prealloc; i++) {
		elem = kmem_cache_alloc(pool->kmem, gfp_mask);
		if (!elem) {
			pr_err("%s() kmem_cache out of memory?!\n",  __func__);
			qmempool_destroy(pool);
			return NULL;
		}
		/* Could use the SP version given it is not visible yet */
		num = alf_mp_enqueue(pool->sharedq, &elem, 1);
		BUG_ON(num <= 0);
	}

	pool->percpu = alloc_percpu(struct qmempool_percpu);
	if (pool->percpu == NULL) {
		pr_err("%s() failed to alloc percpu\n", __func__);
		qmempool_destroy(pool);
		return NULL;
	}

	/* SPSC (Single-Consumer-Single-Producer) queue per CPU */
	for_each_possible_cpu(j) {
		struct qmempool_percpu *cpu = per_cpu_ptr(pool->percpu, j);

		cpu->localq = alf_queue_alloc(localq_sz, gfp_mask);
		if (IS_ERR_OR_NULL(cpu->localq)) {
			pr_err("%s() failed alloc localq(sz:%d) on cpu:%d\n",
			       __func__, localq_sz, j);
			qmempool_destroy(pool);
			return NULL;
		}
	}

	return pool;
}
EXPORT_SYMBOL(qmempool_create);

/* Element handling
 */

/* This function is called when sharedq runs-out of elements.
 * Thus, sharedq needs to be refilled (enq) with elems from slab.
 *
 * Caller must assure this is called in an preemptive safe context due
 * to alf_mp_enqueue() call.
 */
void *__qmempool_alloc_from_slab(struct qmempool *pool, gfp_t gfp_mask)
{
	void *elems[QMEMPOOL_BULK]; /* on stack variable */
	void *elem;
	int num, i, j;

	/* Cannot use SLAB that can sleep if (gfp_mask & __GFP_WAIT),
	 * else preemption disable/enable scheme becomes too complicated
	 */
#ifdef __GFP_WAIT
	BUG_ON(gfp_mask & __GFP_WAIT);
#else
	/* 71baba4b92d ("mm, page_alloc: rename __GFP_WAIT to __GFP_RECLAIM") */
	BUG_ON(gfp_mask & __GFP_RECLAIM);
#endif

	elem = kmem_cache_alloc(pool->kmem, gfp_mask);
	if (elem == NULL) /* slab depleted, no reason to call below allocs */
		return NULL;

	/* SLAB considerations, we need a kmem_cache interface that
	 * supports allocating a bulk of elements.
	 */

	for (i = 0; i < QMEMPOOL_REFILL_MULTIPLIER; i++) {
		for (j = 0; j < QMEMPOOL_BULK; j++) {
			elems[j] = kmem_cache_alloc(pool->kmem, gfp_mask);
			/* Handle if slab gives us NULL elem */
			if (elems[j] == NULL) {
				pr_err("%s() ARGH - slab returned NULL",
				       __func__);
				num = alf_mp_enqueue(pool->sharedq, elems, j-1);
				BUG_ON(num == 0); //FIXME handle
				return elem;
			}
		}
		num = alf_mp_enqueue(pool->sharedq, elems, QMEMPOOL_BULK);
		/* FIXME: There is a theoretical chance that multiple
		 * CPU enter here, refilling sharedq at the same time,
		 * thus we must handle "full" situation, for now die
		 * hard so someone will need to fix this.
		 */
		BUG_ON(num == 0); /* sharedq should have room */
	}

	/* What about refilling localq here? (else it will happen on
	 * next cycle, and will cost an extra cmpxchg).
	 */
	return elem;
}

/* This function is called when the localq runs out-of elements.
 * Thus, localq is refilled (enq) with elements (deq) from sharedq.
 *
 * Caller must assure this is called in an preemptive safe context due
 * to alf_mp_dequeue() call.
 */
void *__qmempool_alloc_from_sharedq(struct qmempool *pool, gfp_t gfp_mask,
				    struct alf_queue *localq)
{
	void *elems[QMEMPOOL_BULK]; /* on stack variable */
	void *elem;
	int num;

	/* Costs atomic "cmpxchg", but amortize cost by bulk dequeue */
	num = alf_mc_dequeue(pool->sharedq, elems, QMEMPOOL_BULK);
	if (likely(num > 0)) {
		/* Consider prefetching data part of elements here, it
		 * should be an optimal place to hide memory prefetching.
		 * Especially given the localq is known to be an empty FIFO
		 * which guarantees the order objs are accessed in.
		 */
		elem = elems[0]; /* extract one element */
		if (num > 1) {
			num = alf_sp_enqueue(localq, &elems[1], num-1);
			/* Refill localq, should be empty, must succeed */
			BUG_ON(num == 0);
		}
		return elem;
	}
	/* Use slab if sharedq runs out of elements */
	elem = __qmempool_alloc_from_slab(pool, gfp_mask);
	return elem;
}
EXPORT_SYMBOL(__qmempool_alloc_from_sharedq);

/* Called when sharedq is full. Thus also make room in sharedq,
 * besides also freeing the "elems" given.
 */
bool __qmempool_free_to_slab(struct qmempool *pool, void **elems, int n)
{
	int num, i, j;
	/* SLAB considerations, we could use kmem_cache interface that
	 * supports returning a bulk of elements.
	 */

	/* free these elements for real */
	for (i = 0; i < n; i++)
		kmem_cache_free(pool->kmem, elems[i]);

	/* Make room in sharedq for next round */
	for (i = 0; i < QMEMPOOL_REFILL_MULTIPLIER; i++) {
		num = alf_mc_dequeue(pool->sharedq, elems, QMEMPOOL_BULK);
		for (j = 0; j < num; j++)
			kmem_cache_free(pool->kmem, elems[j]);
	}
	return true;
}

/* This function is called when the localq is full. Thus, elements
 * from localq needs to be (dequeued) and returned (enqueued) to
 * sharedq (or if shared is full, need to be free'ed to slab)
 *
 * MUST be called from a preemptive safe context.
 */
void __qmempool_free_to_sharedq(void *elem, struct qmempool *pool,
				struct alf_queue *localq)
{
	void *elems[QMEMPOOL_BULK]; /* on stack variable */
	int num_enq, num_deq;

	elems[0] = elem;
	/* Make room in localq */
	num_deq = alf_sc_dequeue(localq, &elems[1], QMEMPOOL_BULK-1);
	if (unlikely(num_deq == 0))
		goto failed;
	num_deq++; /* count first 'elem' */

	/* Successful dequeued 'num_deq' elements from localq, "free"
	 * these elems by enqueuing to sharedq
	 */
	num_enq = alf_mp_enqueue(pool->sharedq, elems, num_deq);
	if (likely(num_enq == num_deq)) /* Success enqueued to sharedq */
		return;

	/* If sharedq is full (num_enq == 0) dequeue elements will be
	 * returned directly to the SLAB allocator.
	 *
	 * Note: This usage of alf_queue API depend on enqueue is
	 * fixed, by only enqueueing if all elements could fit, this
	 * is an API that might change.
	 */

	__qmempool_free_to_slab(pool, elems, num_deq);
	return;
failed:
	/* dequeing from a full localq should always be possible */
	BUG();
}
EXPORT_SYMBOL(__qmempool_free_to_sharedq);

/* API users can choose to use "__" prefixed versions for inlining */
void *qmempool_alloc(struct qmempool *pool, gfp_t gfp_mask)
{
	return __qmempool_alloc(pool, gfp_mask);
}
EXPORT_SYMBOL(qmempool_alloc);

void *qmempool_alloc_softirq(struct qmempool *pool, gfp_t gfp_mask)
{
	return __qmempool_alloc_softirq(pool, gfp_mask);
}
EXPORT_SYMBOL(qmempool_alloc_softirq);

void qmempool_free(struct qmempool *pool, void *elem)
{
	return __qmempool_free(pool, elem);
}
EXPORT_SYMBOL(qmempool_free);

void qmempool_free_softirq(struct qmempool *pool, void *elem)
{
	return __qmempool_free(pool, elem);
}
EXPORT_SYMBOL(qmempool_free_softirq);

MODULE_DESCRIPTION("Quick queue based mempool (qmempool)");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

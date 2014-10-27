#ifndef _LINUX_ALF_QUEUE_H
#define _LINUX_ALF_QUEUE_H
/* linux/alf_queue.h
 *
 * ALF: Array-based Lock-Free queue
 *
 * Queue properties
 *  - Array based for cache-line optimization
 *  - Bounded by the array size
 *  - FIFO Producer/Consumer queue, no queue traversal supported
 *  - Very fast
 *  - Designed as a queue for pointers to objects
 *  - Bulk enqueue and dequeue support
 *  - TODO: supports combinations of Multi and Single Producer/Consumer
 *
 * Copyright (C) 2014, Red Hat, Inc.,
 *  by Jesper Dangaard Brouer and Hannes Frederic Sowa
 *  for licensing details see kernel-base/COPYING
 */
#include <linux/compiler.h>
#include <linux/kernel.h>

struct alf_actor {
	u32 head;
	u32 tail;
};

struct alf_queue {
	u32 size;
	u32 mask;
	u32 flags;
	struct alf_actor producer ____cacheline_aligned_in_smp;
	struct alf_actor consumer ____cacheline_aligned_in_smp;
	void *ring[0] ____cacheline_aligned_in_smp;
};

struct alf_queue * alf_queue_alloc(u32 size, gfp_t gfp);
void               alf_queue_free(struct alf_queue *q);

/* Helpers for LOAD and STORE of elements, have been split-out because:
 *  1. They can be reused for both "Single" and "Multi" variants
 *  2. Allow us to experiment with (pipeline) optimizations in this area.
 */

static inline void
__helper_alf_enqueue_store_simple(u32 p_head, u32 c_tail,
				  struct alf_queue *q, void **ptr, const u32 n)
{
	int i, index = p_head;

	for (i = 0; i < n; i++, index++) {
		q->ring[index] = ptr[i];
		if (index == q->size) /* handle array wrap */
			index = 0;
	}
}

static inline void
__helper_alf_enqueue_store_memcpy(u32 p_head, u32 c_tail,
				  struct alf_queue *q, void **ptr, const u32 n)
{
	// CONTAINS SOME BUG
	if (p_head >= c_tail) {
		memcpy(&q->ring[p_head], ptr, n * sizeof(ptr[0]));
	} else {
		unsigned int dif = q->size - c_tail;

		if (dif)
			memcpy(&q->ring[p_head], ptr, dif * sizeof(ptr[0]));
		memcpy(&q->ring[0], ptr + dif, (c_tail - dif) * sizeof(ptr[0]));
	}
}

static inline void
__helper_alf_dequeue_load_simple(u32 c_head, u32 p_tail,
				 struct alf_queue *q, void **ptr, const u32 elems)
{
	int i, index = c_head;

	for (i = 0; i < elems; i++, index++) {
		ptr[i] = q->ring[index];
		if (index == q->size) /* handle array wrap */
			index = 0;
	}
}


static inline void
__helper_alf_dequeue_load_memcpy(u32 c_head, u32 p_tail,
				 struct alf_queue *q, void **ptr, const u32 elems)
{
	// CONTAINS SOME BUG
	if (p_tail > c_head) {
		memcpy(ptr, &q->ring[c_head], elems * sizeof(ptr[0]));
	} else {
		unsigned int dif = min(q->size - c_head, elems);

		if (dif)
			memcpy(ptr, &q->ring[c_head], dif * sizeof(ptr[0]));
		memcpy(ptr + dif, &q->ring[0], (p_tail - dif) * sizeof(ptr[0]));
	}
}


static inline bool
alf_queue_empty(struct alf_queue *q)
{
	u32 c_tail = ACCESS_ONCE(q->consumer.tail);
	u32 p_tail = ACCESS_ONCE(q->producer.tail);

	/* The empty (and initial state) is when consumer have reached
	 * up with producer.
	 *
	 * DOUBLE-CHECK: Should we use producer.head, as this indicate
	 * a producer is in-progress(?)
	 */
	return c_tail == p_tail;
}

static inline int
alf_queue_count(struct alf_queue *q)
{
	u32 c_head = ACCESS_ONCE(q->consumer.head);
	u32 p_tail = ACCESS_ONCE(q->producer.tail);
	u32 elems;

	elems = (p_tail - c_head) & q->mask;
	return elems;
}

static inline int
alf_queue_avail_space(struct alf_queue *q)
{
	u32 p_head = ACCESS_ONCE(q->producer.head);
	u32 c_tail = ACCESS_ONCE(q->consumer.tail);
	u32 space;

	/* The max avail space is (q->size-1) because the empty state
	 * is when (consumer == producer)
	 */
	space = (c_tail - p_head - 1) & q->mask;
	/* Same as: */
	// space = (q->mask + c_tail - p_head) & q->mask;
	return space;
}

/* Main Multi-Producer ENQUEUE */
static inline int
alf_mp_enqueue(const u32 n;
	       struct alf_queue *q, void *ptr[n], const u32 n)
{
	u32 p_head, p_next, c_tail, space;
	u32 mask = q->mask;

	/* Reserve part of the array for enqueue STORE/WRITE */
	do {
		p_head = ACCESS_ONCE(q->producer.head);
		c_tail = ACCESS_ONCE(q->consumer.tail);

		space = (c_tail - p_head - 1) & mask;
		if (n > space)
			return -ENOBUFS;

		p_next = (p_head + n) & mask;
	}
	while (unlikely(cmpxchg(&q->producer.head, p_head, p_next) != p_head));

//	pr_info("%s(): DEBUG p_head:%d c_tail:%d space:%u p:0x%p\n",
//		__func__, p_head, c_tail, space, q);
//	pr_info("%s(): space_before:%d space_after:%u\n",
//		__func__, space, alf_queue_avail_space(q));
//	pr_info("%s(): DZZZZ ring[0]:0x%p\n",
//		__func__, &q->ring[0]);

	/* STORE the elems into the queue array */
	__helper_alf_enqueue_store_simple(p_head, c_tail, q, ptr, n);
	smp_wmb(); /* Write-Memory-Barrier matching dequeue LOADs */

	/* Wait for other concurrent preceeding enqueues not yet done,
	 * this part make us none-wait-free and could be problematic
	 * in case of congestion with many CPUs
	 */
	while (unlikely(ACCESS_ONCE(q->producer.tail) != p_head))
		cpu_relax();
	/* Mark this enq done and avail for consumption */
	ACCESS_ONCE(q->producer.tail) = p_next;

	return n;
}

/* Main Multi-Consumer DEQUEUE */
static inline int
alf_mc_dequeue(const u32 n;
	       struct alf_queue *q, void *ptr[n], const u32 n)
{
	u32 c_head, c_next, p_tail, elems;
	u32 mask = q->mask;

	/* Reserve part of the array for dequeue LOAD/READ */
	do {
		c_head = ACCESS_ONCE(q->consumer.head);
		p_tail = ACCESS_ONCE(q->producer.tail);

		elems = (p_tail - c_head) & mask;

//		pr_info("%s(): DEQ c_head:%d p_tail:%d elems:%u p:0x%p sz:%d\n",
//			__func__, c_head, p_tail, elems, q, q->size);

		if (elems == 0)
			return 0;
		else
			elems = min(elems, n);

		c_next = (c_head + elems) & mask;
	}
	while (unlikely(cmpxchg(&q->consumer.head, c_head, c_next) != c_head));

	/* LOAD the elems from the queue array.
	 *   We don't need a smb_rmb() Read-Memory-Barrier here because
	 *   the above cmpxchg is an implied full Memory-Barrier.
	 */
	__helper_alf_dequeue_load_simple(c_head, p_tail,  q, ptr, elems);

	/* Wait for other concurrent preceeding dequeues not yet done */
	while (unlikely(ACCESS_ONCE(q->consumer.tail) != c_head))
		cpu_relax();
	/* Mark this deq done and avail for producers */
	ACCESS_ONCE(q->consumer.tail) = c_next;

	return elems;
}


#endif /* _LINUX_ALF_QUEUE_H */

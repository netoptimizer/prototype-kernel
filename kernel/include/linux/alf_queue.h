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
 *  - Supports combinations of Multi and Single Producer/Consumer
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
/* Only a single of these helpers will survive upstream submission */
#include <linux/alf_queue_helpers.h>
#define __helper_alf_enqueue_store __helper_alf_enqueue_store_unroll
#define __helper_alf_dequeue_load  __helper_alf_dequeue_load_unroll

/* Main Multi-Producer ENQUEUE
 *
 * Even-though current API have a "fixed" semantics of aborting if it
 * cannot enqueue the full bulk size.  Users of this API should check
 * on the returned number of enqueue elements match, to verify enqueue
 * was successful.  This allow us to introduce a "variable" enqueue
 * scheme later.
 */
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

		space = mask + c_tail - p_head;
		if (n > space)
			return 0;

		p_next = p_head + n;
	}
	while (unlikely(cmpxchg(&q->producer.head, p_head, p_next) != p_head));

	/* STORE the elems into the queue array */
	__helper_alf_enqueue_store(p_head, q, ptr, n);
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

	/* Reserve part of the array for dequeue LOAD/READ */
	do {
		c_head = ACCESS_ONCE(q->consumer.head);
		p_tail = ACCESS_ONCE(q->producer.tail);

		elems = p_tail - c_head;

		if (elems == 0)
			return 0;
		else
			elems = min(elems, n);

		c_next = c_head + elems;
	}
	while (unlikely(cmpxchg(&q->consumer.head, c_head, c_next) != c_head));

	/* LOAD the elems from the queue array.
	 *   We don't need a smb_rmb() Read-Memory-Barrier here because
	 *   the above cmpxchg is an implied full Memory-Barrier.
	 */
	__helper_alf_dequeue_load(c_head, q, ptr, elems);

	/* Archs with weak Memory Ordering need a memory barrier here.
	 * As the STORE to q->consumer.tail, must happen after the
	 * dequeue LOADs. Dequeue LOADs have a dependent STORE into
	 * ptr, thus a smp_wmb() is enough. Paired with enqueue
	 * implicit full-MB in cmpxchg.
	 */
	smp_wmb();

	/* Wait for other concurrent preceeding dequeues not yet done */
	while (unlikely(ACCESS_ONCE(q->consumer.tail) != c_head))
		cpu_relax();
	/* Mark this deq done and avail for producers */
	ACCESS_ONCE(q->consumer.tail) = c_next;

	return elems;
}

//#define ASSERT_DEBUG_SPSC 1
#ifndef ASSERT_DEBUG_SPSC
#define ASSERT(x) do { } while (0)
#else
#define ASSERT(x)						\
	if (unlikely(!(x))) {					\
		pr_crit("Assertion failed %s:%d: \"%s\"\n",	\
			__FILE__, __LINE__, #x);		\
		BUG();						\
	}
#endif

/* Main SINGLE Producer ENQUEUE
 *  caller MUST make sure preemption is disabled
 */
static inline int
alf_sp_enqueue(const u32 n;
	       struct alf_queue *q, void *ptr[n], const u32 n)
{
	u32 p_head, p_next, c_tail, space;
	u32 mask = q->mask;

	/* Reserve part of the array for enqueue STORE/WRITE */
	p_head = ACCESS_ONCE(q->producer.head);
	smp_rmb(); /* for consumer.tail write, making sure deq loads are done */
	c_tail = ACCESS_ONCE(q->consumer.tail);

	space = mask + c_tail - p_head;
	if (n > space)
		return 0;

	p_next = p_head + n;
	ASSERT(ACCESS_ONCE(q->producer.head) == p_head);
	q->producer.head = p_next;

	/* STORE the elems into the queue array */
	__helper_alf_enqueue_store(p_head, q, ptr, n);
	smp_wmb(); /* Write-Memory-Barrier matching dequeue LOADs */

	/* Assert no other CPU (or same CPU via preemption) changed queue */
	ASSERT(ACCESS_ONCE(q->producer.tail) == p_head);

	/* Mark this enq done and avail for consumption */
	ACCESS_ONCE(q->producer.tail) = p_next;

	return n;
}

/* Main SINGLE Consumer DEQUEUE
 *  caller MUST make sure preemption is disabled
 */
static inline int
alf_sc_dequeue(const u32 n;
	       struct alf_queue *q, void *ptr[n], const u32 n)
{
	u32 c_head, c_next, p_tail, elems;

	/* Reserve part of the array for dequeue LOAD/READ */
	c_head = ACCESS_ONCE(q->consumer.head);
	p_tail = ACCESS_ONCE(q->producer.tail);

	elems = p_tail - c_head;

	if (elems == 0)
		return 0;
	else
		elems = min(elems, n);

	c_next = c_head + elems;
	ASSERT(ACCESS_ONCE(q->consumer.head) == c_head);
	q->consumer.head = c_next;

	smp_rmb(); /* Read-Memory-Barrier matching enq STOREs */
	__helper_alf_dequeue_load(c_head, q, ptr, elems);

	/* Archs with weak Memory Ordering need a memory barrier here.
	 * As the STORE to q->consumer.tail, must happen after the
	 * dequeue LOADs. Dequeue LOADs have a dependent STORE into
	 * ptr, thus a smp_wmb() is enough.
	 */
	smp_wmb();

	/* Assert no other CPU (or same CPU via preemption) changed queue */
	ASSERT(ACCESS_ONCE(q->consumer.tail) == c_head);

	/* Mark this deq done and avail for producers */
	ACCESS_ONCE(q->consumer.tail) = c_next;

	return elems;
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

	/* Due to u32 arithmetic the values are implicitly
	 * masked/modulo 32-bit, thus saving one mask operation
	 */
	elems = p_tail - c_head;
	/* Thus, same as:
	 *  elems = (p_tail - c_head) & q->mask;
	 */
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

	/* Due to u32 arithmetic the values are implicitly
	 * masked/modulo 32-bit, thus saving one mask operation
	 */
	space = q->mask + c_tail - p_head;
	/* Thus, same as:
	 *  space = (q->mask + c_tail - p_head) & q->mask;
	 */
	return space;
}

#endif /* _LINUX_ALF_QUEUE_H */

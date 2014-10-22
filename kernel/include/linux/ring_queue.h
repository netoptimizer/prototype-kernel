/*
 * Copyright 2014 Red Hat, Inc. and/or its affiliates.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Based on code covered by the following legal notices:
 */

/*   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Derived from FreeBSD's bufring.h
 *
 **************************************************************************
 *
 * Copyright (c) 2007-2009 Kip Macy kmacy@freebsd.org
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. The name of Kip Macy nor the names of other
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************/

#ifndef _LINUX_RING_QUEUE_H
#define _LINUX_RING_QUEUE_H

/**
 * @file
 * Ring Queue
 *
 * The Ring Queue is a fixed-size queue, implemented as a table of
 * pointers. Head and tail pointers are modified atomically, allowing
 * concurrent access to it. It has the following features:
 *
 * - FIFO (First In First Out)
 * - Maximum size is fixed; the pointers are stored in a table.
 * - Lockless implementation.
 * - Multi- or single-consumer dequeue.
 * - Multi- or single-producer enqueue.
 * - Bulk dequeue.
 * - Bulk enqueue.
 *
 * Note: the ring implementation is not preemptable. A core must not
 * be interrupted by another task that uses the same ring.
 *
 * FIXME: Add preempt enable/disable, perhaps use???
 *  local_bh_{disable,enable} or do we need preempt_{disable,enable} ???
 */

//#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/processor.h>  /* cpu_relax() */
#include <linux/compiler.h> /* barrier() */
#include <linux/percpu.h>

unsigned int ring_queue_fake_test(unsigned int count);

// Need 32-bit version of cmpxchg()
//  __cmpxchg(ptr, old, new, sizeof(u32))
//  perhaps normal cmpxchg already handles this correctly...
/**
 * Atomic compare and set.
 *
 * (atomic) equivalent to:
 *   if (*dst == exp)
 *     *dst = src (all 32-bit words)
 *
 * @param dst
 *   The destination location into which the value will be written.
 * @param exp
 *   The expected value.
 * @param src
 *   The new value.
 * @return
 *   Non-zero on success; 0 on failure.
 */
static inline int
rte_atomic32_cmpset(volatile uint32_t *dst, uint32_t exp, uint32_t src)
{
	uint8_t res;

	asm volatile(
			"lock; "
			"cmpxchgl %[src], %[dst];"
			"sete %[res];"
			: [res] "=a" (res),     /* output */
			  [dst] "=m" (*dst)
			: [src] "r" (src),      /* input */
			  "a" (exp),
			  "m" (*dst)
			: "memory");            /* no-clobber list */
	return res;
}

enum ring_queue_queue_behavior {
	RING_QUEUE_FIXED = 0, /* Enq/Deq a fixed number of items from a ring */
	RING_QUEUE_VARIABLE   /* Enq/Deq as many items a possible from ring */
};

#ifdef CONFIG_LIB_RING_QUEUE_DEBUG
/**
 * A structure that stores the ring statistics (per-core).
 */
struct ring_queue_debug_stats {
	uint64_t enq_success_bulk; /**< Successful enqueues number. */
	uint64_t enq_success_objs; /**< Objects successfully enqueued. */
	uint64_t enq_quota_bulk;   /**< Successful enqueues above watermark. */
	uint64_t enq_quota_objs;   /**< Objects enqueued above watermark. */
	uint64_t enq_fail_bulk;    /**< Failed enqueues number. */
	uint64_t enq_fail_objs;    /**< Objects that failed to be enqueued. */
	uint64_t deq_success_bulk; /**< Successful dequeues number. */
	uint64_t deq_success_objs; /**< Objects successfully dequeued. */
	uint64_t deq_fail_bulk;    /**< Failed dequeues number. */
	uint64_t deq_fail_objs;    /**< Objects that failed to be dequeued. */
} ____cacheline_aligned_in_smp;
#endif

/**
 * The ring queue structure.
 *
 * The producer and the consumer have a head and a tail index. The particularity
 * of these index is that they are not between 0 and size(ring). These indexes
 * are between 0 and 2^32, and we mask their value when we access the ring[]
 * field. Thanks to this assumption, we can do subtractions between 2 index
 * values in a modulo-32bit base: that's why the overflow of the indexes is not
 * a problem.
 */
struct ring_queue {
	int flags;                       /**< Flags supplied at creation. */

	/** Ring producer status. */
	struct prod {
		uint32_t watermark;      /**< Maximum items before EDQUOT. */
		uint32_t sp_enqueue;     /**< True, if single producer. */
		uint32_t size;           /**< Size of ring. */
		uint32_t mask;           /**< Mask (size-1) of ring. */
		volatile uint32_t head;  /**< Producer head. */
		volatile uint32_t tail;  /**< Producer tail. */
	} prod ____cacheline_aligned_in_smp;

	/** Ring consumer status. */
	struct cons {
		uint32_t sc_dequeue;     /**< True, if single consumer. */
		uint32_t size;           /**< Size of the ring. */
		uint32_t mask;           /**< Mask (size-1) of ring. */
		volatile uint32_t head;  /**< Consumer head. */
		volatile uint32_t tail;  /**< Consumer tail. */
#ifdef CONFIG_LIB_RING_QUEUE_SPLIT_PROD_CONS
	} cons ____cacheline_aligned_in_smp;
#else
	} cons;
#endif

#ifdef CONFIG_LIB_RING_QUEUE_DEBUG
	struct ring_queue_debug_stats stats[NR_CPUS];
#endif

	/**< Memory space of ring starts here.
	 * not volatile so need to be careful
	 * about compiler re-ordering */
	void *ring[0] ____cacheline_aligned_in_smp;
};

#define RING_F_SP_ENQ 0x0001 /**< Flag selects enqueue "single-producer". */
#define RING_F_SC_DEQ 0x0002 /**< Flag selects dequeue "single-consumer". */
#define RING_QUEUE_QUOT_EXCEED (1 << 31)  /**< Quota exceed for burst ops */
#define RING_QUEUE_SZ_MASK  (unsigned)(0x0fffffff) /**< Ring size mask */

/**
 * @internal When debug is enabled, store ring statistics.
 * @param r
 *   A pointer to the ring.
 * @param name
 *   The name of the statistics field to increment in the ring.
 * @param n
 *   The number to add to the object-oriented statistics.
 */
//FIXME: can we save the bh_disable by only calling this from safe context?
#ifdef CONFIG_LIB_RING_QUEUE_DEBUG
#define __RING_STAT_ADD(r, name, n) do {			\
		local_bh_disable();				\
		r->stats[smp_processor_id()].name##_objs += n;	\
		r->stats[smp_processor_id()].name##_bulk += 1;	\
		local_bh_enable();				\
	} while (0)
#else
#define __RING_STAT_ADD(r, name, n) do {} while (0)
#endif

/**
 * Create a new ring named *name* in memory.
 *
 * This function allocate memory for the ring. Its size is
 * set to *count*, which must be a power of two. Water marking is
 * disabled by default.
 * Note that the real usable ring size is *count-1* instead of
 * *count*.
 *
 * @param count
 *   The size of the ring (must be a power of 2).
 * @param flags
 *   An OR of the following:
 *    - RING_F_SP_ENQ: If this flag is set, the default behavior when
 *      using ``ring_queue_enqueue()`` or ``ring_queue_enqueue_bulk()``
 *      is "single-producer". Otherwise, it is "multi-producers".
 *    - RING_F_SC_DEQ: If this flag is set, the default behavior when
 *      using ``ring_queue_dequeue()`` or ``ring_queue_dequeue_bulk()``
 *      is "single-consumer". Otherwise, it is "multi-consumers".
 * @return
 *   On success, the pointer to the new allocated ring. NULL on error
 * FIXME not-true: perhaps use PTR_ERR return values?
 *    - EINVAL - count provided is not a power of 2
 *    - ENOMEM - no memory avail
 */
struct ring_queue *
ring_queue_create(unsigned int count, unsigned int flags);

/**
 * Free memory allocated to the ring
 *
 * @param r
 *   A pointer to the ring.
 */
bool ring_queue_free(struct ring_queue *r);

/**
 * Change the high water mark.
 *
 * If *count* is 0, water marking is disabled. Otherwise, it is set to the
 * *count* value. The *count* value must be greater than 0 and less
 * than the ring size.
 *
 * This function can be called at any time (not necessarily at
 * initialization).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param count
 *   The new water mark value.
 * @return
 *   - 0: Success; water mark changed.
 *   - -EINVAL: Invalid water mark value.
 */
int ring_queue_set_water_mark(struct ring_queue *r, unsigned count);

/**
 * Dump the status of the ring to the console.
 *
 * @param r
 *   A pointer to the ring structure.
 */
void ring_queue_dump(const struct ring_queue *r);

/* the actual enqueue of pointers on the ring.
 * Placed here since identical code needed in both
 * single and multi producer enqueue functions */
#define ENQUEUE_PTRS() do { \
	const uint32_t size = r->prod.size; \
	uint32_t idx = prod_head & mask; \
	if (likely(idx + n < size)) { \
		for (i = 0; i < (n & ((~(unsigned)0x3))); i += 4, idx += 4) { \
			r->ring[idx] = obj_table[i]; \
			r->ring[idx+1] = obj_table[i+1]; \
			r->ring[idx+2] = obj_table[i+2]; \
			r->ring[idx+3] = obj_table[i+3]; \
		} \
		switch (n & 0x3) { \
			case 3: r->ring[idx++] = obj_table[i++]; \
			case 2: r->ring[idx++] = obj_table[i++]; \
			case 1: r->ring[idx++] = obj_table[i++]; \
		} \
	} else { \
		for (i = 0; idx < size; i++, idx++)\
			r->ring[idx] = obj_table[i]; \
		for (idx = 0; i < n; i++, idx++) \
			r->ring[idx] = obj_table[i]; \
	} \
} while (0)

/* the actual copy of pointers on the ring to obj_table.
 * Placed here since identical code needed in both
 * single and multi consumer dequeue functions */
#define DEQUEUE_PTRS() do { \
	uint32_t idx = cons_head & mask; \
	const uint32_t size = r->cons.size; \
	if (likely(idx + n < size)) { \
		for (i = 0; i < (n & (~(unsigned)0x3)); i += 4, idx += 4) {\
			obj_table[i] = r->ring[idx]; \
			obj_table[i+1] = r->ring[idx+1]; \
			obj_table[i+2] = r->ring[idx+2]; \
			obj_table[i+3] = r->ring[idx+3]; \
		} \
		switch (n & 0x3) { \
			case 3: obj_table[i++] = r->ring[idx++]; \
			case 2: obj_table[i++] = r->ring[idx++]; \
			case 1: obj_table[i++] = r->ring[idx++]; \
		} \
	} else { \
		for (i = 0; idx < size; i++, idx++) \
			obj_table[i] = r->ring[idx]; \
		for (idx = 0; i < n; i++, idx++) \
			obj_table[i] = r->ring[idx]; \
	} \
} while (0)

/**
 * @internal Enqueue several objects on the ring (multi-producers safe).
 *
 * This function uses a "compare and set" instruction to move the
 * producer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @param behavior
 *   RING_QUEUE_FIXED:    Enqueue a fixed number of items from a ring
 *   RING_QUEUE_VARIABLE: Enqueue as many items a possible from ring
 * @return
 *   Depend on the behavior value
 *   if behavior = RING_QUEUE_FIXED
 *   - 0: Success; objects enqueue.
 *   - -EDQUOT: Quota exceeded. The objects have been enqueued, but the
 *     high water mark is exceeded.
 *   - -ENOBUFS: Not enough room in the ring to enqueue, no object is enqueued.
 *   if behavior = RING_QUEUE_VARIABLE
 *   - n: Actual number of objects enqueued.
 */
static inline int __attribute__((always_inline))
__ring_queue_mp_do_enqueue(struct ring_queue *r, void * const *obj_table,
			 unsigned n, enum ring_queue_queue_behavior behavior)
{
	uint32_t prod_head, prod_next;
	uint32_t cons_tail, free_entries;
	const unsigned max = n;
	int success;
	unsigned i;
	uint32_t mask = r->prod.mask;
	int ret;

	/* move prod.head atomically */
	do {
		/* Reset n to the initial burst count */
		n = max;

		prod_head = r->prod.head;
		cons_tail = r->cons.tail;
		/* The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * prod_head > cons_tail). So 'free_entries' is always between 0
		 * and size(ring)-1. */
		free_entries = (mask + cons_tail - prod_head);

		/* check that we have enough room in ring */
		if (unlikely(n > free_entries)) {
			if (behavior == RING_QUEUE_FIXED) {
				__RING_STAT_ADD(r, enq_fail, n);
				return -ENOBUFS;
			} else {
				/* No free entry available */
				if (unlikely(free_entries == 0)) {
					__RING_STAT_ADD(r, enq_fail, n);
					return 0;
				}

				n = free_entries;
			}
		}

		prod_next = prod_head + n;
		success = rte_atomic32_cmpset(&r->prod.head, prod_head,
					      prod_next);
	} while (unlikely(success == 0));

	/* write entries in ring */
	ENQUEUE_PTRS();
	barrier(); /* compiler barrier */

	/* if we exceed the watermark */
	if (unlikely(((mask + 1) - free_entries + n) > r->prod.watermark)) {
		ret = (behavior == RING_QUEUE_FIXED) ? -EDQUOT :
				(int)(n | RING_QUEUE_QUOT_EXCEED);
		__RING_STAT_ADD(r, enq_quota, n);
	} else {
		ret = (behavior == RING_QUEUE_FIXED) ? 0 : n;
		__RING_STAT_ADD(r, enq_success, n);
	}

	/*
	 * If there are other enqueues in progress that preceeded us,
	 * we need to wait for them to complete
	 */
	while (unlikely(r->prod.tail != prod_head))
		cpu_relax();

	r->prod.tail = prod_next;
	return ret;
}

/**
 * @internal Enqueue several objects on a ring (NOT multi-producers safe).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @param behavior
 *   RING_QUEUE_FIXED:    Enqueue a fixed number of items from a ring
 *   RING_QUEUE_VARIABLE: Enqueue as many items a possible from ring
 * @return
 *   Depend on the behavior value
 *   if behavior = RING_QUEUE_FIXED
 *   - 0: Success; objects enqueue.
 *   - -EDQUOT: Quota exceeded. The objects have been enqueued, but the
 *     high water mark is exceeded.
 *   - -ENOBUFS: Not enough room in the ring to enqueue, no object is enqueued.
 *   if behavior = RING_QUEUE_VARIABLE
 *   - n: Actual number of objects enqueued.
 */
static inline int __attribute__((always_inline))
__ring_queue_sp_do_enqueue(struct ring_queue *r, void * const *obj_table,
			 unsigned n, enum ring_queue_queue_behavior behavior)
{
	uint32_t prod_head, cons_tail;
	uint32_t prod_next, free_entries;
	unsigned i;
	uint32_t mask = r->prod.mask;
	int ret;

	prod_head = r->prod.head;
	cons_tail = r->cons.tail;
	/* The subtraction is done between two unsigned 32bits value
	 * (the result is always modulo 32 bits even if we have
	 * prod_head > cons_tail). So 'free_entries' is always between 0
	 * and size(ring)-1. */
	free_entries = mask + cons_tail - prod_head;

	/* check that we have enough room in ring */
	if (unlikely(n > free_entries)) {
		if (behavior == RING_QUEUE_FIXED) {
			__RING_STAT_ADD(r, enq_fail, n);
			return -ENOBUFS;
		} else {
			/* No free entry available */
			if (unlikely(free_entries == 0)) {
				__RING_STAT_ADD(r, enq_fail, n);
				return 0;
			}

			n = free_entries;
		}
	}

	prod_next = prod_head + n;
	r->prod.head = prod_next;

	/* write entries in ring */
	ENQUEUE_PTRS();
	barrier(); /* compiler barrier */

	/* if we exceed the watermark */
	if (unlikely(((mask + 1) - free_entries + n) > r->prod.watermark)) {
		ret = (behavior == RING_QUEUE_FIXED) ? -EDQUOT :
			(int)(n | RING_QUEUE_QUOT_EXCEED);
		__RING_STAT_ADD(r, enq_quota, n);
	} else {
		ret = (behavior == RING_QUEUE_FIXED) ? 0 : n;
		__RING_STAT_ADD(r, enq_success, n);
	}

	r->prod.tail = prod_next;
	return ret;
}

/**
 * @internal Dequeue several objects from a ring (multi-consumers safe). When
 * the request objects are more than the available objects, only dequeue the
 * actual number of objects
 *
 * This function uses a "compare and set" instruction to move the
 * consumer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @param behavior
 *   RING_QUEUE_FIXED:    Dequeue a fixed number of items from a ring
 *   RING_QUEUE_VARIABLE: Dequeue as many items a possible from ring
 * @return
 *   Depend on the behavior value
 *   if behavior = RING_QUEUE_FIXED
 *   - 0: Success; objects dequeued.
 *   - -ENOENT: Not enough entries in the ring to dequeue; no object is
 *     dequeued.
 *   if behavior = RING_QUEUE_VARIABLE
 *   - n: Actual number of objects dequeued.
 */

static inline int __attribute__((always_inline))
__ring_queue_mc_do_dequeue(struct ring_queue *r, void **obj_table,
		 unsigned n, enum ring_queue_queue_behavior behavior)
{
	uint32_t cons_head, prod_tail;
	uint32_t cons_next, entries;
	const unsigned max = n;
	int success;
	unsigned i;
	uint32_t mask = r->prod.mask;

	/* move cons.head atomically */
	do {
		/* Restore n as it may change every loop */
		n = max;

		cons_head = r->cons.head;
		prod_tail = r->prod.tail;
		/* The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * cons_head > prod_tail). So 'entries' is always between 0
		 * and size(ring)-1. */
		entries = (prod_tail - cons_head);

		/* Set the actual entries for dequeue */
		if (n > entries) {
			if (behavior == RING_QUEUE_FIXED) {
				__RING_STAT_ADD(r, deq_fail, n);
				return -ENOENT;
			} else {
				if (unlikely(entries == 0)) {
					__RING_STAT_ADD(r, deq_fail, n);
					return 0;
				}

				n = entries;
			}
		}

		cons_next = cons_head + n;
		success = rte_atomic32_cmpset(&r->cons.head, cons_head,
					      cons_next);
	} while (unlikely(success == 0));

	/* copy in table */
	DEQUEUE_PTRS();
	barrier(); /* compiler barrier */

	/*
	 * If there are other dequeues in progress that preceded us,
	 * we need to wait for them to complete
	 */
	while (unlikely(r->cons.tail != cons_head))
		cpu_relax();

	__RING_STAT_ADD(r, deq_success, n);
	r->cons.tail = cons_next;

	return behavior == RING_QUEUE_FIXED ? 0 : n;
}

/**
 * @internal Dequeue several objects from a ring (NOT multi-consumers safe).
 * When the request objects are more than the available objects, only dequeue
 * the actual number of objects
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @param behavior
 *   RING_QUEUE_FIXED:    Dequeue a fixed number of items from a ring
 *   RING_QUEUE_VARIABLE: Dequeue as many items a possible from ring
 * @return
 *   Depend on the behavior value
 *   if behavior = RING_QUEUE_FIXED
 *   - 0: Success; objects dequeued.
 *   - -ENOENT: Not enough entries in the ring to dequeue; no object is
 *     dequeued.
 *   if behavior = RING_QUEUE_VARIABLE
 *   - n: Actual number of objects dequeued.
 */
static inline int __attribute__((always_inline))
__ring_queue_sc_do_dequeue(struct ring_queue *r, void **obj_table,
		 unsigned n, enum ring_queue_queue_behavior behavior)
{
	uint32_t cons_head, prod_tail;
	uint32_t cons_next, entries;
	unsigned i;
	uint32_t mask = r->prod.mask;

	cons_head = r->cons.head;
	prod_tail = r->prod.tail;
	/* The subtraction is done between two unsigned 32bits value
	 * (the result is always modulo 32 bits even if we have
	 * cons_head > prod_tail). So 'entries' is always between 0
	 * and size(ring)-1. */
	entries = prod_tail - cons_head;

	if (n > entries) {
		if (behavior == RING_QUEUE_FIXED) {
			__RING_STAT_ADD(r, deq_fail, n);
			return -ENOENT;
		} else {
			if (unlikely(entries == 0)) {
				__RING_STAT_ADD(r, deq_fail, n);
				return 0;
			}

			n = entries;
		}
	}

	cons_next = cons_head + n;
	r->cons.head = cons_next;

	/* copy in table */
	DEQUEUE_PTRS();
	barrier(); /* compiler barrier */

	__RING_STAT_ADD(r, deq_success, n);
	r->cons.tail = cons_next;
	return behavior == RING_QUEUE_FIXED ? 0 : n;
}

/**
 * Enqueue several objects on the ring (multi-producers safe).
 *
 * This function uses a "compare and set" instruction to move the
 * producer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @return
 *   - 0: Success; objects enqueue.
 *   - -EDQUOT: Quota exceeded. The objects have been enqueued, but the
 *     high water mark is exceeded.
 *   - -ENOBUFS: Not enough room in the ring to enqueue, no object is enqueued.
 */
static inline int __attribute__((always_inline))
ring_queue_mp_enqueue_bulk(struct ring_queue *r, void * const *obj_table,
			 unsigned n)
{
	return __ring_queue_mp_do_enqueue(r, obj_table, n, RING_QUEUE_FIXED);
}

/**
 * Enqueue several objects on a ring (NOT multi-producers safe).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @return
 *   - 0: Success; objects enqueued.
 *   - -EDQUOT: Quota exceeded. The objects have been enqueued, but the
 *     high water mark is exceeded.
 *   - -ENOBUFS: Not enough room in the ring to enqueue; no object is enqueued.
 */
static inline int __attribute__((always_inline))
ring_queue_sp_enqueue_bulk(struct ring_queue *r, void * const *obj_table,
			 unsigned n)
{
	return __ring_queue_sp_do_enqueue(r, obj_table, n, RING_QUEUE_FIXED);
}

/**
 * Enqueue several objects on a ring.
 *
 * This function calls the multi-producer or the single-producer
 * version depending on the default behavior that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @return
 *   - 0: Success; objects enqueued.
 *   - -EDQUOT: Quota exceeded. The objects have been enqueued, but the
 *     high water mark is exceeded.
 *   - -ENOBUFS: Not enough room in the ring to enqueue; no object is enqueued.
 */
static inline int __attribute__((always_inline))
ring_queue_enqueue_bulk(struct ring_queue *r, void * const *obj_table,
		      unsigned n)
{
	if (r->prod.sp_enqueue)
		return ring_queue_sp_enqueue_bulk(r, obj_table, n);
	else
		return ring_queue_mp_enqueue_bulk(r, obj_table, n);
}

/**
 * Enqueue one object on a ring (multi-producers safe).
 *
 * This function uses a "compare and set" instruction to move the
 * producer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj
 *   A pointer to the object to be added.
 * @return
 *   - 0: Success; objects enqueued.
 *   - -EDQUOT: Quota exceeded. The objects have been enqueued, but the
 *     high water mark is exceeded.
 *   - -ENOBUFS: Not enough room in the ring to enqueue; no object is enqueued.
 */
static inline int __attribute__((always_inline))
ring_queue_mp_enqueue(struct ring_queue *r, void *obj)
{
	return ring_queue_mp_enqueue_bulk(r, &obj, 1);
}

/**
 * Enqueue one object on a ring (NOT multi-producers safe).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj
 *   A pointer to the object to be added.
 * @return
 *   - 0: Success; objects enqueued.
 *   - -EDQUOT: Quota exceeded. The objects have been enqueued, but the
 *     high water mark is exceeded.
 *   - -ENOBUFS: Not enough room in the ring to enqueue; no object is enqueued.
 */
static inline int __attribute__((always_inline))
ring_queue_sp_enqueue(struct ring_queue *r, void *obj)
{
	return ring_queue_sp_enqueue_bulk(r, &obj, 1);
}

/**
 * Enqueue one object on a ring.
 *
 * This function calls the multi-producer or the single-producer
 * version, depending on the default behaviour that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj
 *   A pointer to the object to be added.
 * @return
 *   - 0: Success; objects enqueued.
 *   - -EDQUOT: Quota exceeded. The objects have been enqueued, but the
 *     high water mark is exceeded.
 *   - -ENOBUFS: Not enough room in the ring to enqueue; no object is enqueued.
 */
static inline int __attribute__((always_inline))
ring_queue_enqueue(struct ring_queue *r, void *obj)
{
	if (r->prod.sp_enqueue)
		return ring_queue_sp_enqueue(r, obj);
	else
		return ring_queue_mp_enqueue(r, obj);
}

/**
 * Dequeue several objects from a ring (multi-consumers safe).
 *
 * This function uses a "compare and set" instruction to move the
 * consumer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @return
 *   - 0: Success; objects dequeued.
 *   - -ENOENT: Not enough entries in the ring to dequeue; no object is
 *     dequeued.
 */
static inline int __attribute__((always_inline))
ring_queue_mc_dequeue_bulk(struct ring_queue *r, void **obj_table, unsigned n)
{
	return __ring_queue_mc_do_dequeue(r, obj_table, n, RING_QUEUE_FIXED);
}

/**
 * Dequeue several objects from a ring (NOT multi-consumers safe).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table,
 *   must be strictly positive.
 * @return
 *   - 0: Success; objects dequeued.
 *   - -ENOENT: Not enough entries in the ring to dequeue; no object is
 *     dequeued.
 */
static inline int __attribute__((always_inline))
ring_queue_sc_dequeue_bulk(struct ring_queue *r, void **obj_table, unsigned n)
{
	return __ring_queue_sc_do_dequeue(r, obj_table, n, RING_QUEUE_FIXED);
}

/**
 * Dequeue several objects from a ring.
 *
 * This function calls the multi-consumers or the single-consumer
 * version, depending on the default behaviour that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @return
 *   - 0: Success; objects dequeued.
 *   - -ENOENT: Not enough entries in the ring to dequeue, no object is
 *     dequeued.
 */
static inline int __attribute__((always_inline))
ring_queue_dequeue_bulk(struct ring_queue *r, void **obj_table, unsigned n)
{
	if (r->cons.sc_dequeue)
		return ring_queue_sc_dequeue_bulk(r, obj_table, n);
	else
		return ring_queue_mc_dequeue_bulk(r, obj_table, n);
}

/**
 * Dequeue one object from a ring (multi-consumers safe).
 *
 * This function uses a "compare and set" instruction to move the
 * consumer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_p
 *   A pointer to a void * pointer (object) that will be filled.
 * @return
 *   - 0: Success; objects dequeued.
 *   - -ENOENT: Not enough entries in the ring to dequeue; no object is
 *     dequeued.
 */
static inline int __attribute__((always_inline))
ring_queue_mc_dequeue(struct ring_queue *r, void **obj_p)
{
	return ring_queue_mc_dequeue_bulk(r, obj_p, 1);
}

/**
 * Dequeue one object from a ring (NOT multi-consumers safe).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_p
 *   A pointer to a void * pointer (object) that will be filled.
 * @return
 *   - 0: Success; objects dequeued.
 *   - -ENOENT: Not enough entries in the ring to dequeue, no object is
 *     dequeued.
 */
static inline int __attribute__((always_inline))
ring_queue_sc_dequeue(struct ring_queue *r, void **obj_p)
{
	return ring_queue_sc_dequeue_bulk(r, obj_p, 1);
}

/**
 * Dequeue one object from a ring.
 *
 * This function calls the multi-consumers or the single-consumer
 * version depending on the default behaviour that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_p
 *   A pointer to a void * pointer (object) that will be filled.
 * @return
 *   - 0: Success, objects dequeued.
 *   - -ENOENT: Not enough entries in the ring to dequeue, no object is
 *     dequeued.
 */
static inline int __attribute__((always_inline))
ring_queue_dequeue(struct ring_queue *r, void **obj_p)
{
	if (r->cons.sc_dequeue)
		return ring_queue_sc_dequeue(r, obj_p);
	else
		return ring_queue_mc_dequeue(r, obj_p);
}

/**
 * Test if a ring is full.
 *
 * @param r
 *   A pointer to the ring structure.
 * @return
 *   - 1: The ring is full.
 *   - 0: The ring is not full.
 */
static inline int
ring_queue_full(const struct ring_queue *r)
{
	uint32_t prod_tail = r->prod.tail;
	uint32_t cons_tail = r->cons.tail;
	return (((cons_tail - prod_tail - 1) & r->prod.mask) == 0);
}

/**
 * Test if a ring is empty.
 *
 * @param r
 *   A pointer to the ring structure.
 * @return
 *   - 1: The ring is empty.
 *   - 0: The ring is not empty.
 */
static inline int
ring_queue_empty(const struct ring_queue *r)
{
	uint32_t prod_tail = r->prod.tail;
	uint32_t cons_tail = r->cons.tail;
	return !!(cons_tail == prod_tail);
}

/**
 * Return the number of entries in a ring.
 *
 * @param r
 *   A pointer to the ring structure.
 * @return
 *   The number of entries in the ring.
 */
static inline unsigned
ring_queue_count(const struct ring_queue *r)
{
	uint32_t prod_tail = r->prod.tail;
	uint32_t cons_tail = r->cons.tail;
	return ((prod_tail - cons_tail) & r->prod.mask);
}

/**
 * Return the number of free entries in a ring.
 *
 * @param r
 *   A pointer to the ring structure.
 * @return
 *   The number of free entries in the ring.
 */
static inline unsigned
ring_queue_free_count(const struct ring_queue *r)
{
	uint32_t prod_tail = r->prod.tail;
	uint32_t cons_tail = r->cons.tail;
	return ((cons_tail - prod_tail - 1) & r->prod.mask);
}

/**
 * Enqueue several objects on the ring (multi-producers safe).
 *
 * This function uses a "compare and set" instruction to move the
 * producer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @return
 *   - n: Actual number of objects enqueued.
 */
static inline int __attribute__((always_inline))
ring_queue_mp_enqueue_burst(struct ring_queue *r, void * const *obj_table,
			 unsigned n)
{
	return __ring_queue_mp_do_enqueue(r, obj_table, n, RING_QUEUE_VARIABLE);
}

/**
 * Enqueue several objects on a ring (NOT multi-producers safe).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @return
 *   - n: Actual number of objects enqueued.
 */
static inline int __attribute__((always_inline))
ring_queue_sp_enqueue_burst(struct ring_queue *r, void * const *obj_table,
			 unsigned n)
{
	return __ring_queue_sp_do_enqueue(r, obj_table, n, RING_QUEUE_VARIABLE);
}

/**
 * Enqueue several objects on a ring.
 *
 * This function calls the multi-producer or the single-producer
 * version depending on the default behavior that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @return
 *   - n: Actual number of objects enqueued.
 */
static inline int __attribute__((always_inline))
ring_queue_enqueue_burst(struct ring_queue *r, void * const *obj_table,
		      unsigned n)
{
	if (r->prod.sp_enqueue)
		return ring_queue_sp_enqueue_burst(r, obj_table, n);
	else
		return ring_queue_mp_enqueue_burst(r, obj_table, n);
}

/**
 * Dequeue several objects from a ring (multi-consumers safe). When the request
 * objects are more than the available objects, only dequeue the actual number
 * of objects
 *
 * This function uses a "compare and set" instruction to move the
 * consumer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @return
 *   - n: Actual number of objects dequeued, 0 if ring is empty
 */
static inline int __attribute__((always_inline))
ring_queue_mc_dequeue_burst(struct ring_queue *r, void **obj_table, unsigned n)
{
	return __ring_queue_mc_do_dequeue(r, obj_table, n, RING_QUEUE_VARIABLE);
}

/**
 * Dequeue several objects from a ring (NOT multi-consumers safe).When the
 * request objects are more than the available objects, only dequeue the
 * actual number of objects
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @return
 *   - n: Actual number of objects dequeued, 0 if ring is empty
 */
static inline int __attribute__((always_inline))
ring_queue_sc_dequeue_burst(struct ring_queue *r, void **obj_table, unsigned n)
{
	return __ring_queue_sc_do_dequeue(r, obj_table, n, RING_QUEUE_VARIABLE);
}

/**
 * Dequeue multiple objects from a ring up to a maximum number.
 *
 * This function calls the multi-consumers or the single-consumer
 * version, depending on the default behaviour that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @return
 *   - Number of objects dequeued, or a negative error code on error
 */
static inline int __attribute__((always_inline))
ring_queue_dequeue_burst(struct ring_queue *r, void **obj_table, unsigned n)
{
	if (r->cons.sc_dequeue)
		return ring_queue_sc_dequeue_burst(r, obj_table, n);
	else
		return ring_queue_mc_dequeue_burst(r, obj_table, n);
}

#endif /* _LINUX_RING_QUEUE_H */

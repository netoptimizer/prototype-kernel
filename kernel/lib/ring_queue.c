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
 *  Based on code covered by the following legal notices:
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
 * Derived from FreeBSD's bufring.c
 *
 **************************************************************************
 *
 * Copyright (c) 2007,2008 Kip Macy kmacy@freebsd.org
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/cache.h> /* SMP_CACHE_BYTES */

#include <linux/ring_queue.h>

// Do we really need a list of all the rings created in kernel???
//LIST_HEAD(global_ring_queue_list);

/* True if x is a power of 2 */
#define POWEROF2(x) (((x) & ((x) - 1)) == 0)

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE SMP_CACHE_BYTES     /* Cache line size */
#endif
#define CACHE_LINE_MASK (CACHE_LINE_SIZE-1) /* Cache line mask */

/* create the ring */
struct ring_queue *
ring_queue_create(unsigned int count, unsigned int flags)
{
	struct ring_queue *r;
	size_t ring_size;

	/* compilation-time checks */
	BUILD_BUG_ON(!POWEROF2(CACHE_LINE_SIZE));
//FIXME: Add CONFIG_SMP check due to ____cacheline_aligned_in_smp usage
// perhaps use ____cacheline_aligned instead?
	BUILD_BUG_ON((sizeof(struct ring_queue) &
		      CACHE_LINE_MASK) != 0);
#ifdef CONFIG_LIB_RING_QUEUE_SPLIT_PROD_CONS
	BUILD_BUG_ON((offsetof(struct ring_queue, cons) &
		      CACHE_LINE_MASK) != 0);
#endif
	BUILD_BUG_ON((offsetof(struct ring_queue, prod) &
		      CACHE_LINE_MASK) != 0);

	/* count must be a power of 2 */
	if ((!POWEROF2(count)) || (count > RING_QUEUE_SZ_MASK)) {
		pr_err("Requested size is invalid, must be power of 2, and "
		       "do not exceed the size limit %u\n", RING_QUEUE_SZ_MASK);
		return NULL;
	}

	ring_size = count * sizeof(void *) + sizeof(struct ring_queue);
	//ring_size = PAGE_ALIGN(ring_size);
	// TODO: This might be suboptimal use of pages, look at improving
	r = alloc_pages_exact(ring_size, GFP_KERNEL|__GFP_ZERO|__GFP_NOWARN);
	if (r == NULL) {
		pr_err("%s(): Cannot reserve continous memory for ring\n",
		       __func__);
		return NULL;
	}

	/* init the ring structure */
	memset(r, 0, sizeof(*r));
	r->flags = flags;
	r->prod.watermark = count;
	r->prod.sp_enqueue = !!(flags & RING_F_SP_ENQ);
	r->cons.sc_dequeue = !!(flags & RING_F_SC_DEQ);
	r->prod.size = r->cons.size = count;
	r->prod.mask = r->cons.mask = count-1;
	r->prod.head = r->cons.head = 0;
	r->prod.tail = r->cons.tail = 0;

	//TAILQ_INSERT_TAIL(ring_list, r, next);
	//list_add_rcu(global_ring_queue_list, r->next);

	return r;
}
EXPORT_SYMBOL(ring_queue_create);

/* free memory allocated to the ring */
bool ring_queue_free(struct ring_queue *r)
{
	size_t ring_size;
	unsigned int count = r->prod.size;
	//TODO: Add sanity checks e.g. if queue is empty...

	ring_size = count * sizeof(void *) + sizeof(struct ring_queue);
	free_pages_exact(r, ring_size);
	return true;
}
EXPORT_SYMBOL(ring_queue_free);


/* change the high water mark. If *count* is 0, water marking is
 * disabled
 */
int
ring_queue_set_water_mark(struct ring_queue *r, unsigned count)
{
	if (count >= r->prod.size)
		return -EINVAL;

	/* if count is 0, disable the watermarking */
	if (count == 0)
		count = r->prod.size;

	r->prod.watermark = count;
	return 0;
}

static int __init ring_queue_init(void)
{
	pr_warn("Loaded\n"); //TODO: remove
	// TODO: create /proc dir for reading debug/dump info
	return 0;
}
module_init(ring_queue_init);

static void __exit ring_queue_exit(void)
{
	// TODO: perform sanity checks, and free mem
	pr_warn("Unloaded\n"); //TODO: remove
}
module_exit(ring_queue_exit);


// Dummy EXPORT_SYMBOL func for testing overhead of call
unsigned int ring_queue_fake_test(unsigned int count)
{
	return count;
}
EXPORT_SYMBOL(ring_queue_fake_test);

MODULE_DESCRIPTION("Producer/Consumer ring based queue");
MODULE_AUTHOR("Jesper Dangaard Brouer");
MODULE_LICENSE("Dual BSD/GPL");

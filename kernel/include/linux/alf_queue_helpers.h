#pragma once
/* This file is NOT for upstream submission!
 *
 * Only one of the helper function will survive upstream submission,
 * and it will be added directly in alf_queue.h.
 *
 * This is helpers for LOAD and STORE of elements, into the queue.
 * This is primarily to find the best pipeline and loop-unroll
 * optimizations.
 */

static inline void
__helper_alf_enqueue_store_simple(u32 p_head, struct alf_queue *q,
				  void **ptr, const u32 n)
{
	int i, index = p_head & q->mask;

	/* Basic idea is to save masked "AND-op" in exchange with
	 * branch-op for checking explicit for wrap
	 */
	for (i = 0; i < n; i++, index++) {
		q->ring[index] = ptr[i];
		if (unlikely(index == q->size)) /* handle array wrap */
			index = 0;
	}
}
static inline void
__helper_alf_dequeue_load_simple(u32 c_head, struct alf_queue *q,
				 void **ptr, const u32 elems)
{
	int i, index = c_head & q->mask;

	for (i = 0; i < elems; i++, index++) {
		ptr[i] = q->ring[index];
		if (unlikely(index == q->size)) /* handle array wrap */
			index = 0;
	}
}

static inline void
__helper_alf_enqueue_store_mask(u32 p_head, struct alf_queue *q,
				void **ptr, const u32 n)
{
	int i, index = p_head;

	for (i = 0; i < n; i++, index++) {
		q->ring[index & q->mask] = ptr[i];
	}
}
static inline void
__helper_alf_dequeue_load_mask(u32 c_head, struct alf_queue *q,
			       void **ptr, const u32 elems)
{
	int i, index = c_head;

	for (i = 0; i < elems; i++, index++) {
		ptr[i] = q->ring[index & q->mask];
	}
}

static inline void
__helper_alf_enqueue_store_mask_less(u32 p_head, struct alf_queue *q,
				     void **ptr, const u32 n)
{
	int i, index = p_head & q->mask;

	if (likely((index + n) <= q->mask)) {
		/* Can save masked-AND knowing we cannot wrap */
		for (i = 0; i < n; i++, index++) {
			q->ring[index] = ptr[i];
		}
	} else {
		for (i = 0; i < n; i++, index++) {
			q->ring[index & q->mask] = ptr[i];
		}
	}
}
static inline void
__helper_alf_dequeue_load_mask_less(u32 c_head, struct alf_queue *q,
				    void **ptr, const u32 elems)
{
	int i, index = c_head & q->mask;

	if (likely((index + elems) <= q->mask)) {
		/* Can save masked-AND knowing we cannot wrap */
		for (i = 0; i < elems; i++, index++) {
			ptr[i] = q->ring[index];
		}
	} else {
		for (i = 0; i < elems; i++, index++) {
			ptr[i] = q->ring[index & q->mask];
		}
	}
}

static inline void
__helper_alf_enqueue_store_mask_less2(u32 p_head, struct alf_queue *q,
				      void **ptr, const u32 n)
{
	int i = 0, index = p_head & q->mask;

	/* Saving masked-AND operation  */
	if (likely((index + n) <= q->mask)) {
	rest:
		for (; i < n; i++, index++) {
			q->ring[index] = ptr[i];
		}
	} else {
		for (i = 0; index <= q->mask; i++, index++) {
			q->ring[index] = ptr[i];
		}
		index = 0;
		goto rest;
//		for (index = 0; i < n; i++, index++) {
//			q->ring[index] = ptr[i];
//		}
	}
}
static inline void
__helper_alf_dequeue_load_mask_less2(u32 c_head, struct alf_queue *q,
				     void **ptr, const u32 elems)
{
	int i = 0, index = c_head & q->mask;

	/* Saving masked-AND operation  */
	if (likely((index + elems) <= q->mask)) {
	rest:
		for (; i < elems; i++, index++) {
			ptr[i] = q->ring[index];
		}
	} else {
		for (i = 0; index <= q->mask; i++, index++) {
			ptr[i] = q->ring[index];
		}
		index = 0;
		goto rest;
//		for (index = 0; i < elems; i++, index++) {
//			ptr[i] = q->ring[index];
//		}
	}
}


static inline void
__helper_alf_enqueue_store_nomask(u32 p_head, struct alf_queue *q,
				  void **ptr, const u32 n)
{
	int i, index = p_head & q->mask;

	/* Avoids if-statement and any mask of array index */
	for (i = 0; i < n && index <= q->mask; i++, index++) {
		q->ring[index] = ptr[i];
	}
	for (index = 0; i < n; i++, index++) {
		q->ring[index] = ptr[i];
	}
}
static inline void
__helper_alf_dequeue_load_nomask(u32 c_head, struct alf_queue *q,
				 void **ptr, const u32 elems)
{
	int i, index = c_head & q->mask;

	/* Avoids if-statement and any mask of array index */
	for (i = 0; i < elems && index <= q->mask; i++, index++) {
		ptr[i] = q->ring[index];
	}
	for (index = 0; i < elems; i++, index++) {
		ptr[i] = q->ring[index];
	}
}

static inline void
__helper_alf_enqueue_store_unroll(u32 p_head, struct alf_queue *q,
				  void **ptr, const u32 n)
{
	int i, iterations = n & ~3UL;
	u32 index = p_head & q->mask;

	if (likely((index + n) <= q->mask)) {
		/* Can save masked-AND knowing we cannot wrap */
		/* Loop unroll */
		for (i = 0; i < iterations; i+=4, index+=4) {
			q->ring[index  ] = ptr[i];
			q->ring[index+1] = ptr[i+1];
			q->ring[index+2] = ptr[i+2];
			q->ring[index+3] = ptr[i+3];
		}
		/* Remainder handling */
		switch(n & 0x3) {
		case 3:
			q->ring[index  ] = ptr[i];
			q->ring[index+1] = ptr[i+1];
			q->ring[index+2] = ptr[i+2];
			break;
		case 2:
			q->ring[index  ] = ptr[i];
			q->ring[index+1] = ptr[i+1];
			break;
		case 1:
			q->ring[index  ] = ptr[i];
		}
	} else {
		/* Fall-back to "mask" version */
		for (i = 0; i < n; i++, index++) {
			q->ring[index & q->mask] = ptr[i];
		}
	}
}
static inline void
__helper_alf_dequeue_load_unroll(u32 c_head, struct alf_queue *q,
				 void **ptr, const u32 elems)
{
	int i, iterations = elems & ~3UL;
	u32 index = c_head & q->mask;

	if (likely((index + elems) <= q->mask)) {
		/* Can save masked-AND knowing we cannot wrap */
		/* Loop unroll */
		for (i = 0; i < iterations; i+=4, index+=4) {
			ptr[i]   = q->ring[index  ];
			ptr[i+1] = q->ring[index+1];
			ptr[i+2] = q->ring[index+2];
			ptr[i+3] = q->ring[index+3];
		}
		/* Remainder handling */
		switch(elems & 0x3) {
		case 3:
			ptr[i]   = q->ring[index  ];
			ptr[i+1] = q->ring[index+1];
			ptr[i+2] = q->ring[index+2];
			break;
		case 2:
			ptr[i]   = q->ring[index  ];
			ptr[i+1] = q->ring[index+1];
			break;
		case 1:
			ptr[i]   = q->ring[index];
		}
	} else {
		/* Fall-back to "mask" version */
		for (i = 0; i < elems; i++, index++) {
			ptr[i] = q->ring[index & q->mask];
		}
	}
}


static inline void
__helper_alf_enqueue_store_unroll_duff(u32 p_head,  struct alf_queue *q,
				       void **ptr, const u32 n)
{
	int i = 0, r = n & 0x3, iterations = n & ~3UL;
	u32 index = p_head & q->mask;

	if (likely((index + n) <= q->mask)) {
		/* Can save masked-AND knowing we cannot wrap */
		/* Loop unroll using duff's device w/end condition update */
		switch(r) {
		case 0:
			do {
				q->ring[index+3] = ptr[i+3];
			case 3:
				q->ring[index+2] = ptr[i+2];
			case 2:
				q->ring[index+1] = ptr[i+1];
			case 1:
				q->ring[index  ] = ptr[i];
				//pr_info("i:%d index:%d r:%d iterations:%d\n",
				//	i, index, r, iterations);
			} while ((r > 0) ? (i+=r, index+=r, r=0, iterations)
				  : (i+=4, index+=4)
				 && i < iterations
				);
		}
	} else {
		/* Fall-back to "mask" version */
		for (i = 0; i < n; i++, index++) {
			q->ring[index & q->mask] = ptr[i];
		}
	}
}
static inline void
__helper_alf_dequeue_load_unroll_duff(u32 c_head, struct alf_queue *q,
				      void **ptr, const u32 elems)
{
	int i = 0, r = elems & 0x3, iterations = elems & ~3UL;
	u32 index = c_head & q->mask;

	if (likely((index + elems) <= q->mask)) {
		/* Can save masked-AND knowing we cannot wrap */
		/* Loop unroll using duff's device w/end condition update */
		switch(r) {
		case 0:
			do {
				ptr[i+3] = q->ring[index+3];
			case 3:
				ptr[i+2] = q->ring[index+2];
			case 2:
				ptr[i+1] = q->ring[index+1];
			case 1:
				ptr[i]   = q->ring[index  ];
			} while ((r > 0) ? (i+=r, index+=r, r=0, iterations)
				 : (i+=4, index+=4)
				 && i < iterations
				);
		}
	} else {
		/* Fall-back to "mask" version */
		for (i = 0; i < elems; i++, index++) {
			ptr[i] = q->ring[index & q->mask];
		}
	}
}

static inline void
__helper_alf_enqueue_store_memcpy(u32 p_head, struct alf_queue *q,
				  void **ptr, const u32 n)
{
	u32 p_next = (p_head + n) & q->mask;
	p_head &= q->mask;
	if (p_next >= p_head) {
		memcpy(&q->ring[p_head], ptr, (p_next-p_head) * sizeof(ptr[0]));
	} else {
		memcpy(&q->ring[p_head], &ptr[0], (q->size-p_head) * sizeof(ptr[0]));
		memcpy(&q->ring[0], &ptr[q->size-p_head], p_next * sizeof(ptr[0]));
	}
}
static inline void
__helper_alf_dequeue_load_memcpy(u32 c_head, struct alf_queue *q,
				 void **ptr, const u32 elems)
{
	u32 c_next = (c_head + elems) & q->mask;
	c_head &= q->mask;
	if (c_next >= c_head) {
		memcpy(ptr, &q->ring[c_head], (c_next-c_head) * sizeof(ptr[0]));
	} else {
		memcpy(&ptr[0], &q->ring[c_head], (q->size - c_head) * sizeof(ptr[0]));
		memcpy(&ptr[q->size-c_head], &q->ring[0], c_next * sizeof(ptr[0]));
	}
}

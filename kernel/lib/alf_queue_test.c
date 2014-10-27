/*
 * Test module for linux/alf_queue.h usage
 *  a Producer/Consumer Array-based Lock-Free pointer queue
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/alf_queue.h>

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/errno.h>

//#include <linux/time_bench.h>

static int verbose=1;

/*** Basic functionality true/false test functions ***/

static bool test_detect_not_power_of_two(void)
{
	struct alf_queue *queue = alf_queue_alloc(42, GFP_KERNEL);
	if (IS_ERR_OR_NULL(queue)) {
		if (queue == ERR_PTR(-EINVAL)) {
			return true;
		} else {
			return false;
		}
	}
	alf_queue_free(queue);
	return false;
}

static bool test_alloc_and_free(void)
{
	struct alf_queue *queue = alf_queue_alloc(2048, GFP_KERNEL);
	if (IS_ERR_OR_NULL(queue))
		return false;
	alf_queue_free(queue);
	return true;
}

static noinline bool test_add_and_remove_elem(void)
{
	struct alf_queue *queue;
	int on_stack = 123;
	int *obj = &on_stack;
	int *deq_obj = NULL;

	queue = alf_queue_alloc(8, GFP_KERNEL);
	if (IS_ERR_OR_NULL(queue))
		return false;

	/* enqueue */
	if (alf_mp_enqueue(queue, (void **)&obj, 1) < 0)
		goto fail;
	/* count */
	if (alf_queue_count(queue) != 1)
		goto fail;
	/* dequeue */
	if (alf_mc_dequeue(queue, (void **)&deq_obj, 1) < 0)
		goto fail;
	/* compare pointer values from enq and deq */
	if (verbose)
		pr_info("%s(): ptr deq_obj:0x%p obj:0x%p &on_stack:0x%p\n",
			__func__, deq_obj, obj, &on_stack);
	if (obj != deq_obj)
		goto fail;
	/* compare int values stored (by ptr) from enq and deq */
	if (verbose)
		pr_info("%s(): int deq_obj:%d obj:%d on_stack:%d\n",
			__func__, *deq_obj, *obj, on_stack);
	if (*deq_obj != *obj)
		goto fail;
	/* empty */
	if (!alf_queue_empty(queue))
		goto fail;
	alf_queue_free(queue);
	return true;
fail:
	alf_queue_free(queue);
	return false;
}

static bool test_add_and_remove_elems_BULK(void)
{
#define BULK  10
#define LOOPS 6
#define SIZE 32
	struct alf_queue *queue;
	void *objs[BULK];
	void *deq_objs[BULK];
	int i, j, n = 20;

	queue = alf_queue_alloc(SIZE, GFP_KERNEL);
	if (IS_ERR_OR_NULL(queue))
		return false;
	/* The max queue size it SIZE-1 */
	if (alf_queue_avail_space(queue) != (SIZE-1))
		goto fail;
	/* Repeat the enqueue/dequeue cycle */
	for (j = 0; j < LOOPS; j++) {
		/* fake init pointers to a number */
		for (i = 0; i < BULK; i++, n++)
			objs[i] = (void *)(unsigned long)(n);
		/* enqueue */
		if (alf_mp_enqueue(queue, objs, BULK) < 0)
			goto fail;
		/* count */
		if (alf_queue_count(queue) != BULK)
			goto fail;
		/* dequeue */
		if (alf_mc_dequeue(queue, deq_objs, BULK) < 0)
			goto fail;
		/* compare pointers with fake values from enq to deq */
		for (i = 0; i < BULK; i++) {
			if (verbose)
				pr_info("%s(%d): cmp deq_objs[%d]:%lu == obj[%d]:%lu\n",
					__func__, j, i,
					(unsigned long)deq_objs[i],
					i, (unsigned long)objs[i]);
			if (objs[i] != deq_objs[i])
				goto fail;
		}
	}
	/* empty */
	if (!alf_queue_empty(queue))
		goto fail;
	alf_queue_free(queue);
	return true;
fail:
	alf_queue_free(queue);
	return false;
#undef BULK
#undef LOOPS
#undef SIZE
}

/* Testing: enqueue until full and dequeue until empty.  Also
 * demonstrate effect of increasing bulk enqueue.  As current enqueue
 * semantics is to abort if the entire bulk does not fit.  The bulk
 * dequeue will return the number of elements it was able to bulk
 * dequeue.
 */
static bool test_add_until_full(void)
{
#define BULK 15
#define SIZE 16
	struct alf_queue *q;
	void *objs[BULK];
	void *deq_objs[BULK];
	int i, j, n = 20;
	int enq_cnt = 0;
	int enq_cnt_total;
	int deq_cnt = 0;
	int deq_cnt_total;

	q = alf_queue_alloc(SIZE, GFP_KERNEL);
	if (IS_ERR_OR_NULL(q))
		return false;
	/* The max queue size it SIZE-1 */
	if (alf_queue_avail_space(q) != (SIZE-1))
		goto fail;
	/* fake init pointers to a number */
	for (i = 0; i < BULK; i++, n++)
		objs[i] = (void *)(unsigned long)(n);

	/* Repeat the enqueue/dequeue cycle with larger BULK enqueues*/
	for (j = 1; j <= BULK; j++) {
		enq_cnt_total = 0;
		deq_cnt_total = 0;

		/* enqueue until full */
		do {
			enq_cnt = alf_mp_enqueue(q, objs, j); /* notice "j" */
			if (enq_cnt > 0)
				enq_cnt_total += enq_cnt;
		} while (enq_cnt > 0);

		/* count */
		if (verbose)
			pr_info("%s(bulk:%d): enq before full %d(%d)\n",
				__func__, j, enq_cnt_total, alf_queue_count(q));
		if (alf_queue_count(q) != enq_cnt_total)
			goto fail;
		/* dequeue until empty */
		do {
			deq_cnt = alf_mc_dequeue(q, deq_objs, BULK);
			if (deq_cnt > 0) {
				deq_cnt_total += deq_cnt;
				if (deq_cnt != BULK)
					pr_info("%s(j:%d): deq:%d < bulk:%d\n",
						__func__, j, deq_cnt, BULK);
			}
		} while (deq_cnt > 0);

		if (verbose)
			pr_info("%s(%d): total:%d deq before empty=%d\n",
				__func__, j, deq_cnt_total, alf_queue_count(q));
		/* queue must be empty here */
		if (alf_queue_count(q) != 0)
			goto fail;
		if (deq_cnt_total != enq_cnt_total)
			goto fail;
	}
	/* empty */
	if (!alf_queue_empty(q))
		goto fail;
	alf_queue_free(q);
	return true;
fail:
	alf_queue_free(q);
	return false;
#undef BULK
#undef SIZE
}

#define TEST_FUNC(func) 					\
do {								\
	if (!(func)) {						\
		pr_info("FAILED - " #func "\n");		\
		return -1;					\
	} else {						\
		if (verbose)					\
			pr_info("PASSED - " #func "\n");	\
		passed_count++;					\
	}							\
} while (0)

int run_basic_tests(void)
{
	int passed_count = 0;
	TEST_FUNC(test_detect_not_power_of_two());
	TEST_FUNC(test_alloc_and_free());
	TEST_FUNC(test_add_and_remove_elem());
	TEST_FUNC(test_add_and_remove_elems_BULK());
	TEST_FUNC(test_add_until_full());
	return passed_count;
}

static int __init alf_queue_test_module_init(void)
{
	preempt_disable();
	pr_info("DEBUG: cpu:%d\n", smp_processor_id());
	preempt_enable();

	if (verbose)
		pr_info("Loaded\n");

	if (run_basic_tests() < 0)
		return -ECANCELED;

	return 0;
}
module_init(alf_queue_test_module_init);

static void __exit alf_queue_test_module_exit(void)
{
	// TODO: perform sanity checks, and free mem
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(alf_queue_test_module_exit);

MODULE_DESCRIPTION("Sample of Array-based Lock-Free queue");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

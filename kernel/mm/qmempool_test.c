/*
 * Test module for linux/qmempool.h usage
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/alf_queue.h>
#include <linux/slab.h>
#include <linux/time_bench.h>
#include <linux/skbuff.h>

#include <linux/qmempool.h>

static int verbose=1;

/*** Basic functionality true/false test functions ***/

/* queues must be a power-of-2 */
static bool test_detect_not_power_of_two(void)
{
	struct qmempool *pool = qmempool_create(32, 142, 0, NULL, GFP_ATOMIC);
	if (pool == NULL) /* failure is the expected result */
		return true;
	qmempool_destroy(pool);
	return false;
}

/* don't prealloc larger than shared queue size*/
static bool test_detect_prealloc_too_large(void)
{
	struct qmempool *pool = qmempool_create(32, 256, 512, NULL, GFP_ATOMIC);
	if (pool == NULL) /* failure is the expected result */
		return true;
	qmempool_destroy(pool);
	return false;
}

static bool test_basic_create_and_destroy(void)
{
	struct kmem_cache *slab;
	struct qmempool *pool;

	slab = kmem_cache_create("qmempool_test1", 256, 0,
				 SLAB_HWCACHE_ALIGN, NULL);
	pool = qmempool_create(32, 512, 511, slab, GFP_ATOMIC);
	if (pool == NULL) {
		kmem_cache_destroy(slab);
		return false;
	}
	qmempool_destroy(pool);
	kmem_cache_destroy(slab);
	return true;
}

static bool test_basic_req_elem(void)
{
	struct kmem_cache *slab;
	struct qmempool *pool;
	void *elem;
	struct qmempool_percpu *cpu;
	bool result = true;
	int queue_sz;

	slab = kmem_cache_create("qmempool_test2", 256, 0,
				 SLAB_HWCACHE_ALIGN, NULL);
	pool = qmempool_create(32, 512, 200, slab, GFP_ATOMIC);
	if (pool == NULL) {
		kmem_cache_destroy(slab);
		return false;
	}
	/* Request an element, this will refill the localq with elems
	 * from sharedq */
	elem = qmempool_alloc(pool, GFP_ATOMIC);
	if (elem == NULL)
		result = false;
	else
		kmem_cache_free(pool->kmem, elem); /* SLAB free elem */

	preempt_disable();
	cpu = this_cpu_ptr(pool->percpu);
	queue_sz = alf_queue_count(cpu->localq);
	/* Localq should be refilled with BULK-1 */
	if (queue_sz != (QMEMPOOL_BULK - 1))
		result = false;
	if (verbose >= 2)
		pr_info("%s() localq:%d sharedq:%d\n", __func__,
			queue_sz, alf_queue_count(pool->sharedq));
	preempt_enable();

	qmempool_destroy(pool);
	kmem_cache_destroy(slab);
	return result;
}

static void print_qstats(struct qmempool *pool,
			 const char *func, const char *msg)
{
	struct qmempool_percpu *cpu;
	int localq_sz, sharedq_sz;

	preempt_disable();
	cpu = this_cpu_ptr(pool->percpu);
	localq_sz  = alf_queue_count(cpu->localq);
	sharedq_sz = alf_queue_count(pool->sharedq);
	if (verbose >= 2)
		pr_info("%s() qstats localq:%d sharedq:%d (%s)\n", func,
			localq_sz, sharedq_sz, msg);
	preempt_enable();
}

static bool test_alloc_and_free_nr(int nr)
{
	struct kmem_cache *slab;
	struct qmempool *pool;
	void *elem;
	bool result = true;
	int i, res;
	struct alf_queue *temp_queue;

	/* temporary queue for keeping elements for testing */
	temp_queue = alf_queue_alloc(1024, GFP_ATOMIC);
	if (temp_queue == NULL)
		return false;

	slab = kmem_cache_create("qmempool_test3", 256, 0,
				 SLAB_HWCACHE_ALIGN, NULL);
	pool = qmempool_create(32, 128, 0, slab, GFP_ATOMIC);
	if (pool == NULL) {
		alf_queue_free(temp_queue);
		kmem_cache_destroy(slab);
		return false;
	}

	/* Request many elements */
	for (i = 0; i < nr; i++) {
		elem = qmempool_alloc(pool, GFP_ATOMIC);
		alf_mp_enqueue(temp_queue, &elem, 1);
	}
	if (verbose >= 2)
		pr_info("%s() nr elems %d qstats temp_queue:%d\n", __func__,
			nr, alf_queue_count(temp_queue));

	print_qstats(pool, __func__, "A");

	/* Free all the elements */
	for (i = 0; i < nr; i++) {
		res = alf_mc_dequeue(temp_queue, &elem, 1);
		BUG_ON(res == 0);
		qmempool_free(pool, elem);
	}
	print_qstats(pool, __func__, "B");

	alf_queue_free(temp_queue);
	qmempool_destroy(pool);
	kmem_cache_destroy(slab);
	return result;
}

#define TEST_FUNC(func) 					\
do {								\
	if (!(func)) {						\
		pr_info("FAILED - " #func "\n");		\
		failed_count++;					\
	} else {						\
		if (verbose)					\
			pr_info("PASSED - " #func "\n");	\
	}							\
} while (0)

int run_basic_tests(void)
{
	int failed_count = 0;
	TEST_FUNC(test_detect_not_power_of_two());
	TEST_FUNC(test_detect_prealloc_too_large());
	TEST_FUNC(test_basic_create_and_destroy());
	TEST_FUNC(test_basic_req_elem());
	TEST_FUNC(test_alloc_and_free_nr(128));
	TEST_FUNC(test_alloc_and_free_nr(129));
	TEST_FUNC(test_alloc_and_free_nr((128+(128/(QMEMPOOL_BULK*QMEMPOOL_REFILL_MULTIPLIER)))));
	TEST_FUNC(test_alloc_and_free_nr((128+(128/(QMEMPOOL_BULK*QMEMPOOL_REFILL_MULTIPLIER)))+1));
	return failed_count;
}

static int __init qmempool_test_module_init(void)
{
	preempt_disable();
	pr_info("DEBUG: cpu:%d\n", smp_processor_id());
	preempt_enable();

	if (verbose)
		pr_info("Loaded\n");

	if (run_basic_tests() > 0)
		return -ECANCELED;

	return 0;
}
module_init(qmempool_test_module_init);

static void __exit qmempool_test_module_exit(void)
{
	// TODO: perform sanity checks, and free mem
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(qmempool_test_module_exit);

MODULE_DESCRIPTION("Testing of qmempool");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

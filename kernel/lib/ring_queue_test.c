/*
 * Sample module for linux/ring_queue.h usage
 *  a Producer/Consumer ring based pointer queue
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ring_queue.h>
#include <linux/module.h>
//#include <linux/smp.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <asm/msr.h>

#include <linux/time_bench.h>

static int verbose=1;

/*** Basic functionality true/false test functions ***/

static bool test_detect_not_power_of_two(void)
{
	struct ring_queue *queue = ring_queue_create(42, 0);
	if (queue == NULL)
		return true;
	ring_queue_free(queue);
	return false;
}

static bool test_alloc_and_free(void)
{
	struct ring_queue *queue = ring_queue_create(2048, 0);
	if (queue == NULL)
		return false;
	return ring_queue_free(queue);
}

static bool test_SPSC_add_and_remove_elem(void)
{
	struct ring_queue *queue;
	int on_stack = 123;
	int *obj = &on_stack;
	int *deq_obj = NULL;

	queue = ring_queue_create(128, RING_F_SP_ENQ|RING_F_SC_DEQ);
	if (queue == NULL)
		return false;
	/* enqueue */
	if (ring_queue_enqueue(queue, obj) < 0)
		goto fail;
	/* count */
	if (ring_queue_count(queue) != 1)
		goto fail;
	/* dequeue */
	if (ring_queue_dequeue(queue, (void **)&deq_obj) < 0)
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
	if (!ring_queue_empty(queue))
		goto fail;
	return ring_queue_free(queue);
fail:
	ring_queue_free(queue);
	return false;
}

static bool test_SPSC_add_and_remove_elems_BULK(void)
{
#define BULK 10
	struct ring_queue *queue;
	void *objs[BULK];
	void *deq_objs[BULK];
	unsigned int i;

	queue = ring_queue_create(128, RING_F_SP_ENQ|RING_F_SC_DEQ);
	if (queue == NULL)
		return false;
	/* fake init pointers to a number */
	for (i = 0; i < BULK; i++)
		objs[i] = (void *)(unsigned long)(i+20);
	/* enqueue */
	if (ring_queue_enqueue_bulk(queue, objs, BULK) < 0)
		goto fail;
	/* count */
	if (ring_queue_count(queue) != BULK)
		goto fail;
	/* dequeue */
	if (ring_queue_dequeue_bulk(queue, deq_objs, BULK) < 0)
		goto fail;
	/* compare pointers with fake values from enq to deq */
	for (i = 0; i < BULK; i++) {
		if (verbose)
			pr_info("%s(): cmp deq_objs[%d]:%lu == obj[%d]:%lu\n",
				__func__, i, (unsigned long)deq_objs[i],
				i, (unsigned long)objs[i]);
		if (objs[i] != deq_objs[i])
			goto fail;
	}
	/* empty */
	if (!ring_queue_empty(queue))
		goto fail;
	return ring_queue_free(queue);
fail:
	ring_queue_free(queue);
	return false;
}

static bool test_late_void_ptr_cast_BULK(void)
{
#define BULK 10
	struct ring_queue *queue;
	/* Notice: Not using void ptr to allow compiler to catch issues */
	int *objs[BULK];
	int objs_data[BULK] = {21,22,23,24,25,26,27,28,29,30};
	int *deq_objs[BULK];
	unsigned int i;

	queue = ring_queue_create(128, RING_F_SP_ENQ|RING_F_SC_DEQ);
	if (queue == NULL)
		return false;
	/* init object pointer to point to the data */
	for (i = 0; i < BULK; i++)
		objs[i] = &objs_data[i];
	/* enqueue */
	if (ring_queue_enqueue_bulk(queue, (void**)objs, BULK) < 0)
		goto fail;
	/* count */
	if (ring_queue_count(queue) != BULK)
		goto fail;
	/* dequeue */
	if (ring_queue_dequeue_bulk(queue, (void**)deq_objs, BULK) < 0)
		goto fail;
	/* compare values from enq to deq */
	for (i = 0; i < BULK; i++) {
		if (verbose)
			pr_info("%s(): ptr deq_objs[%d]:0x%p == obj[%d]:0x%p\n",
				__func__, i, deq_objs[i], i, objs[i]);
		/* compare pointers */
		if (objs[i] != deq_objs[i])
			goto fail;
		if (verbose)
			pr_info("%s(): val deq_objs[%d]:%d == obj[%d]:%d\n",
				__func__, i, *deq_objs[i], i, *objs[i]);
		/* compare int values at end of ptr */
		if (*objs[i] != *deq_objs[i])
			goto fail;
	}
	/* empty */
	if (!ring_queue_empty(queue))
		goto fail;
	return ring_queue_free(queue);
fail:
	ring_queue_free(queue);
	return false;
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
	TEST_FUNC(test_SPSC_add_and_remove_elem());
	TEST_FUNC(test_SPSC_add_and_remove_elems_BULK());
	TEST_FUNC(test_late_void_ptr_cast_BULK());
	return passed_count;
}

/*** Benchmark code execution time tests ***/

/* Timing at the nanosec level, we need to know the overhead
 * introduced by the for loop itself */
static int time_bench_for_loop(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier(); /* avoid compiler to optimize this loop */
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

/* Fake function ptr construct */
unsigned int my_func(void *data, u16 q)
{
	u16 test = q;
	if (data)
		return 42;
	return (unsigned int)test;
}
struct func_ptr_ops {
	unsigned int (*func)(void *data, u16 q);
};
static struct func_ptr_ops my_func_ptr __read_mostly = {
	.func  = my_func,
};
static int time_call_func_ptr(struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;
	unsigned int tmp, tmp2;
	struct func_ptr_ops *func_ptr = &my_func_ptr;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		tmp =func_ptr->func(&tmp2, 1);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

#include <linux/netdevice.h>
static int my_ndo_open(struct net_device *netdev)
{
	if (netdev)
		return 0;
	return 42;
}
static const struct net_device_ops my_netdev_ops = {
	.ndo_open = my_ndo_open,
};
static int time_ndo_func_ptr(struct time_bench_record *rec, void *data)
{
	struct net_device *netdev;
	int i;
	uint64_t loops_cnt = 0;
	unsigned int tmp;

	netdev = kmalloc(sizeof(*netdev), GFP_ATOMIC);
	netdev->netdev_ops = &my_netdev_ops;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		tmp = netdev->netdev_ops->ndo_open(netdev);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	kfree(netdev);
	return loops_cnt;
}
static int time_ndo_func_ptr_null_tst(struct time_bench_record *rec, void *data)
{
	struct net_device *netdev;
	int i;
	uint64_t loops_cnt = 0;
	unsigned int tmp = 0;

	netdev = kmalloc(sizeof(*netdev), GFP_ATOMIC);
	netdev->netdev_ops = &my_netdev_ops;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		if (dev_xmit_complete(tmp) && netdev->netdev_ops->ndo_open)
			netdev->netdev_ops->ndo_open(netdev);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	kfree(netdev);
	return loops_cnt;

}

static int time_bench_single_enqueue_dequeue(
	struct time_bench_record *rec, void *data)
{
	int on_stack = 123;
	int *obj = &on_stack;
	int *deq_obj = NULL;
	int i;
	uint64_t loops_cnt = 0;
	struct ring_queue *queue = (struct ring_queue*)data;

	if (queue == NULL) {
		pr_err("Need ring_queue as input\n");
		return -1;
	}
	/* loop count is limited to 32-bit due to div_u64_rem() use */
	if (((uint64_t)rec->loops * 2) >= ((1ULL<<32)-1)) {
		pr_err("Loop cnt too big will overflow 32-bit\n");
		return 0;
	}

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		if (ring_queue_enqueue(queue, obj) < 0)
			goto fail;
		loops_cnt++;
		barrier(); /* compiler barrier */
		if (ring_queue_dequeue(queue, (void **)&deq_obj) < 0)
			goto fail;
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
fail:
	return 0;
}

#define MAX_BULK 32

static int time_BULK_enqueue_dequeue(
	struct time_bench_record *rec, void *data)
{
	int *objs[MAX_BULK];
	int *deq_objs[MAX_BULK];
	int i;
	uint64_t loops_cnt = 0;
	int bulk = rec->step;
	struct ring_queue* queue = (struct ring_queue*)data;

	if (queue == NULL) {
		pr_err("Need ring_queue as input\n");
		return -1;
	}
	if (bulk > MAX_BULK) {
		pr_warn("%s() bulk(%d) request too big cap at %d\n",
			__func__, bulk, MAX_BULK);
		bulk = MAX_BULK;
	}
	/* loop count is limited to 32-bit due to div_u64_rem() use */
	if (((uint64_t)rec->loops * bulk *2) >= ((1ULL<<32)-1)) {
		pr_err("Loop cnt too big will overflow 32-bit\n");
		return 0;
	}
	/* fake init pointers to a number */
	for (i = 0; i < MAX_BULK; i++)
		objs[i] = (void *)(unsigned long)(i+20);

	time_bench_start(rec);

	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		if (ring_queue_enqueue_bulk(queue, (void**)objs, bulk) < 0)
			goto fail;
		loops_cnt += bulk;
		barrier(); /* compiler barrier */
		if (ring_queue_dequeue_bulk(queue, (void **)deq_objs, bulk) < 0)
			goto fail;
		loops_cnt +=bulk;
	}

	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
fail:
	return -1;
}

/* Multi enqueue before dequeue
 * - strange test as bulk is normal solution, but want to see
 *   if we didn't have/use bulk, and touch more of ring array
 */
static int time_multi_enqueue_dequeue(
	struct time_bench_record *rec, void *data)
{
	int on_stack = 123;
	int *obj = &on_stack;
	int *deq_obj = NULL;
	int i, n;
	uint64_t loops_cnt = 0;
	int elems = rec->step;
	struct ring_queue* queue = (struct ring_queue*)data;

	if (queue == NULL) {
		pr_err("Need ring_queue as input\n");
		return -1;
	}
	/* loop count is limited to 32-bit due to div_u64_rem() use */
	if (((uint64_t)rec->loops * 2 * elems) >= ((1ULL<<32)-1)) {
		pr_err("Loop cnt too big will overflow 32-bit\n");
		return 0;
	}

	time_bench_start(rec);

	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		for (n = 0; n < elems; n++) {
			if (ring_queue_enqueue(queue, obj) < 0)
				goto fail;
			loops_cnt++;
		}
		barrier(); /* compiler barrier */
		for (n = 0; n < elems; n++) {
			if (ring_queue_dequeue(queue, (void **)&deq_obj) < 0)
				goto fail;
			loops_cnt++;
		}
	}

	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
fail:
	return -1;
}

/** measuring doubly linked list **/

struct my_list_elem {
	struct list_head list;
	uint64_t number;
} ____cacheline_aligned_in_smp; /* Make sure test uses different cachelines */

#define ELEMS 10
static struct my_list_elem elem_array[ELEMS];

static int time_list_head(
	struct time_bench_record *rec, void *data)
{
	struct list_head list;
	uint64_t loops_cnt = 0;
	int i;
	struct my_list_elem *elem;
	//struct my_list_elem *pos;
	//int cnt=0;
	INIT_LIST_HEAD(&list);

	/* Init elems and add to the "list" */
	for (i = 0; i < ELEMS; i++) {
		elem = &elem_array[i];
		elem->number = i;
		INIT_LIST_HEAD(&elem->list);
		/* CONSIDERATIONS: should we add elem in reverse-memory order
		 * to avoid CPU cache being smart?
		 */
		list_add(&elem->list, &list);
		//list_add_tail(&elem->list, &list);
	}

	/* Remove/Dequeue list head */
	elem = list_first_entry(&list, struct my_list_elem, list);
	list_del_init(&elem->list);

	/* Re-add elem to tail */
	// list_add_tail(&elem->list, &list);

	/* Debug: list elements in list
	pr_info("Debug1: list traversal\n");
	list_for_each_entry(pos, &list, list) {
		cnt++;
		pr_info("(cnt:%d) elem->number:%llu\n", cnt, pos->number);
	} */

	time_bench_start(rec);

	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* tail enqueue elem */
		list_add_tail(&elem->list, &list);
		loops_cnt++;

		barrier(); /* compiler barrier */

		/* head dequeue elem */
		elem = list_first_entry(&list, struct my_list_elem, list);
		list_del_init(&elem->list); // Will be re-added
		loops_cnt++;
	}

	/* Debug: list elements in list
	pr_info("Debug2: list traversal\n");
	list_for_each_entry(pos, &list, list) {
		cnt++;
		pr_info("(cnt:%d) elem->number:%llu\n", cnt, pos->number);
	} */

	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
}

/* Try to avoid false sharing by placing lock here */
static spinlock_t my_list_lock ____cacheline_aligned_in_smp;

static int time_list_locked(
	struct time_bench_record *rec, void *data)
{
	struct list_head list;
	uint64_t loops_cnt = 0;
	int i;
	struct my_list_elem *elem;
	INIT_LIST_HEAD(&list);
	spin_lock_init(&my_list_lock);

	/* Init elems and add to the "list" */
	for (i = 0; i < ELEMS; i++) {
		elem = &elem_array[i];
		elem->number = i;
		INIT_LIST_HEAD(&elem->list);
		list_add_tail(&elem->list, &list);
	}

	/* Remove/Dequeue list head */
	elem = list_first_entry(&list, struct my_list_elem, list);
	list_del_init(&elem->list);

	time_bench_start(rec);

	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		spin_lock(&my_list_lock);
		/* tail enqueue elem */
		list_add_tail(&elem->list, &list);
		spin_unlock(&my_list_lock);

		loops_cnt++;

		barrier(); /* compiler barrier */

		/* head dequeue elem */
		spin_lock(&my_list_lock);
		elem = list_first_entry(&list, struct my_list_elem, list);
		list_del_init(&elem->list); // Will be re-added
		spin_unlock(&my_list_lock);

		loops_cnt++;
	}

	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
}


struct my_elem {
	uint64_t number;
} ____cacheline_aligned_in_smp; /* Make sure test uses different cachelines */

/* kmem pointer tries to avoid false cache sharing */
static struct kmem_cache *kmem ____cacheline_aligned_in_smp __read_mostly;

static int time_bench_kmem_cache_reuse(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	int i;
	//struct my_elem *elem;
	struct sk_buff *elem;

	//kmem = kmem_cache_create("ring_queue_test", sizeof(struct my_elem),
	kmem = kmem_cache_create("ring_queue_test", sizeof(struct sk_buff),
				 0,SLAB_HWCACHE_ALIGN, NULL);
	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* request new elem */
		elem = kmem_cache_alloc(kmem, GFP_ATOMIC);
		if (elem == NULL)
			goto out;
		loops_cnt++;

		barrier(); /* compiler barrier */

		/* return elem */
		kmem_cache_free(kmem, elem);
		loops_cnt++;
	}
out:
	time_bench_stop(rec, loops_cnt);
	/* cleanup */
	kmem_cache_destroy(kmem);
	return true;
}

static int time_bench_kmem_cache_test2(
	struct time_bench_record *rec, void *data)
{
#define KMEM_MAX_ELEMS 128
	uint64_t loops_cnt = 0;
	int i, n;
	//struct my_elem *elem;
	struct sk_buff *elems[KMEM_MAX_ELEMS];

	//kmem = kmem_cache_create("ring_queue_test", sizeof(struct my_elem),
	kmem = kmem_cache_create("ring_queue_test", sizeof(struct sk_buff),
				 0,SLAB_HWCACHE_ALIGN, NULL);

	time_bench_start(rec);

	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* request N new elems */
		for (n = 0; n < KMEM_MAX_ELEMS; n++) {
			elems[n] = kmem_cache_alloc(kmem, GFP_ATOMIC);
			loops_cnt++;
		}

		barrier(); /* compiler barrier */

		/* return N elems */
		for (n = 0; n < KMEM_MAX_ELEMS; n++) {
			kmem_cache_free(kmem, elems[n]);
			loops_cnt++;
		}
	}
	time_bench_stop(rec, loops_cnt);

	/* cleanup */
	kmem_cache_destroy(kmem);
	return loops_cnt;
}

/** kmalloc comparison benchmarking **/

static int time_bench_kmalloc_test1(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	int i;
	struct sk_buff *elem;
	size_t elem_sz = sizeof(*elem);

	time_bench_start(rec);
	pr_info("%s() kmalloc elem sizeof=%lu\n", __func__, elem_sz);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		elem = kmalloc(elem_sz, GFP_ATOMIC);
		loops_cnt++;

		barrier(); /* compiler barrier */

		kfree(elem);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
}

static int time_bench_kmalloc_test2(
	struct time_bench_record *rec, void *data)
{
# define KMALLOC_MAX_ELEMS 128
	uint64_t loops_cnt = 0;
	int i, n;
	struct sk_buff *elems[KMALLOC_MAX_ELEMS];
	size_t elem_sz = sizeof(*elems[0]);

	if (verbose)
		pr_info("%s() kmalloc elems=%d sizeof=%lu total=%lu\n",
			__func__, KMALLOC_MAX_ELEMS, elem_sz,
			KMALLOC_MAX_ELEMS * elem_sz);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* alloc  N new elems */
		for (n = 0; n < KMEM_MAX_ELEMS; n++) {
			elems[n] = kmalloc(elem_sz, GFP_ATOMIC);
			loops_cnt++;
		}

		barrier(); /* compiler barrier */

		/* free N elems */
		for (n = 0; n < KMEM_MAX_ELEMS; n++) {
			kfree(elems[n]);
			loops_cnt++;
		}
	}
	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
}


void run_timing_bulksize(int bulk, uint32_t loops,
			struct ring_queue *MPMC,
			struct ring_queue *SPSC,
			struct ring_queue *MPSC)
{
	pr_info("*** Timing with BULK=%d ***\n", bulk);
	time_bench_loop(loops, bulk, "MPMC", MPMC, time_BULK_enqueue_dequeue);
	time_bench_loop(loops, bulk, "SPSC", SPSC, time_BULK_enqueue_dequeue);
	time_bench_loop(loops, bulk, "MPSC", MPSC, time_BULK_enqueue_dequeue);
}

int run_timing_tests(void)
{
	int passed_count = 0;
	int ring_size = 512;
	struct ring_queue *MPMC;
	struct ring_queue *SPSC;
	struct ring_queue *MPSC;
	uint32_t loops = 10000000;

	time_bench_loop(loops*1000, 0, "for_loop", NULL, time_bench_for_loop);

	time_bench_loop(loops*20, 0, "time_call_func_ptr", NULL,
			time_call_func_ptr);

	time_bench_loop(loops*20, 0, "time_ndo_func_ptr", NULL,
			time_ndo_func_ptr);

	time_bench_loop(loops*20, 0, "time_ndo_func_ptr_null_tst", NULL,
			time_ndo_func_ptr_null_tst);

	time_bench_loop(loops/10, 0, "list_unlocked", NULL,
			time_list_head);
	time_bench_loop(loops/10, 0, "list_locked", NULL,
			time_list_locked);

	time_bench_loop(loops*10, 0, "kmem_simple_reuse", NULL,
			time_bench_kmem_cache_reuse);

	time_bench_loop(loops/100, 0, "kmem_simple_test2", NULL,
			time_bench_kmem_cache_test2);

	time_bench_loop(loops, 0, "kmalloc_test1", NULL,
			time_bench_kmalloc_test1);
	time_bench_loop(loops/10, 0, "kmalloc_test2", NULL,
			time_bench_kmalloc_test2);

	MPMC = ring_queue_create(ring_size, 0);
	SPSC = ring_queue_create(ring_size, RING_F_SP_ENQ|RING_F_SC_DEQ);
	MPSC = ring_queue_create(ring_size, RING_F_SC_DEQ);

	time_bench_loop(loops, 0, "MPMC", MPMC,
			time_bench_single_enqueue_dequeue);
	time_bench_loop(loops, 0, "SPSC", SPSC,
			time_bench_single_enqueue_dequeue);
	time_bench_loop(loops, 0, "MPSC", MPSC,
			time_bench_single_enqueue_dequeue);

	time_bench_loop(loops/100, 128, "MPMC-m", MPMC,
			time_multi_enqueue_dequeue);
	time_bench_loop(loops/100, 128, "SPSC-m", SPSC,
			time_multi_enqueue_dequeue);
	time_bench_loop(loops/100, 128, "MPSC-m", MPSC,
			time_multi_enqueue_dequeue);

	run_timing_bulksize( 2, loops, MPMC, SPSC, MPSC);
	run_timing_bulksize( 4, loops, MPMC, SPSC, MPSC);
	run_timing_bulksize( 8, loops, MPMC, SPSC, MPSC);
	run_timing_bulksize(16, loops, MPMC, SPSC, MPSC);
	run_timing_bulksize(32, loops, MPMC, SPSC, MPSC);

	ring_queue_free(MPMC);
	ring_queue_free(SPSC);
	ring_queue_free(MPSC);
	return passed_count;
}

static int __init ring_queue_module_init(void)
{
//	u64 pcm_inst_begin = pmc_inst();
//	u64 pcm_inst_end;
//	u64 clk_begin = pmc_clk();
//	u64 clk_end;

	preempt_disable();
	pr_info("DEBUG: cpu:%d\n", smp_processor_id());
	preempt_enable();
	//TEST enable PMU registers
	//time_bench_PMU_config(true);

	if (verbose)
		pr_info("Loaded\n");
//	if (run_basic_tests() < 0)
//		return -ECANCELED;

	if (run_timing_tests() < 0) {
		return -ECANCELED;
	}
//	pcm_inst_end = pmc_inst();
//	clk_end = pmc_clk();

//	pr_info("PMC inst: start:%llu end:%llu diff:%llu\n",
//		pcm_inst_begin, pcm_inst_end, pcm_inst_end-pcm_inst_begin);

//	pr_info("PMC clk: start:%llu end:%llu diff:%llu\n",
//		clk_begin, clk_end, clk_end - clk_begin);

	return 0;
}
module_init(ring_queue_module_init);

static void __exit ring_queue_module_exit(void)
{
	// TODO: perform sanity checks, and free mem
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(ring_queue_module_exit);

MODULE_DESCRIPTION("Sample/test of Producer/Consumer ring based queue");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

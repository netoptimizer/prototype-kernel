/*
* Benchmark module for linux/alf_queue.h usage
*  a Producer/Consumer Array-based Lock-Free pointer queue
*/
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/alf_queue.h>

//#include <linux/list.h>
//#include <linux/spinlock.h>
//#include <linux/slab.h>
//#include <linux/skbuff.h>
//#include <linux/errno.h>

#include <linux/time_bench.h>

static int verbose=1;

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

#define ALF_FLAG_MP 0x1  /* Multi  Producer */
#define ALF_FLAG_MC 0x2  /* Multi  Consumer */
#define ALF_FLAG_SP 0x4  /* Single Producer */
#define ALF_FLAG_SC 0x8  /* Single Consumer */

enum queue_behavior_type {
	MPMC = (ALF_FLAG_MP|ALF_FLAG_MC),
	SPSC = (ALF_FLAG_SP|ALF_FLAG_SC)
};

static __always_inline int time_bench_one_enq_deq(
	struct time_bench_record *rec, void *data,
	enum queue_behavior_type type)
{
	int on_stack = 123;
	int *obj = &on_stack;
	int *deq_obj = NULL;
	int i;
	uint64_t loops_cnt = 0;
	struct alf_queue *queue = (struct alf_queue*)data;

	if (queue == NULL) {
		pr_err("Need queue struct ptr as input\n");
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
		/* Compile will hopefull optimized this out */
		if (type & ALF_FLAG_SP) {
			if (alf_sp_enqueue(queue, (void **)&obj, 1) < 0)
				goto fail;
		} else if (type & ALF_FLAG_MP) {
			if (alf_mp_enqueue(queue, (void **)&obj, 1) < 0)
				goto fail;
		} else {
			BUILD_BUG();
		}

		loops_cnt++;
		barrier(); /* compiler barrier */

		if (type & ALF_FLAG_SC) {
			if (alf_sc_dequeue(queue, (void **)&deq_obj, 1) < 0)
				goto fail;
		} else if (type & ALF_FLAG_MC) {
			if (alf_mc_dequeue(queue, (void **)&deq_obj, 1) < 0)
				goto fail;
		} else {
			BUILD_BUG();
		}

		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
fail:
	return 0;
}
/* Compiler should inline optimize other function calls out */
static int time_bench_one_enq_deq_mpmc(
	struct time_bench_record *rec, void *data)
{
	return time_bench_one_enq_deq(rec, data, MPMC);
}
static int time_bench_one_enq_deq_spsc(
	struct time_bench_record *rec, void *data)
{
	return time_bench_one_enq_deq(rec, data, SPSC);
}

/* Multi enqueue before dequeue
 * - strange test as bulk is normal solution, but want to see
 *   if we didn't have/use bulk, and touch more of array
 */
static __always_inline int time_multi_enq_deq(
	struct time_bench_record *rec, void *data,
	enum queue_behavior_type type)
{
	int on_stack = 123;
	int *obj = &on_stack;
	int *deq_obj = NULL;
	int i, n;
	uint64_t loops_cnt = 0;
	int elems = rec->step;
	struct alf_queue* queue = (struct alf_queue*)data;

	if (queue == NULL) {
		pr_err("Need queue struct ptr as input\n");
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
			if (type & ALF_FLAG_SP) {
				if (alf_sp_enqueue(queue, (void **)&obj, 1) < 0)
					goto fail;
			} else if (type  & ALF_FLAG_MP) {
				if (alf_mp_enqueue(queue, (void **)&obj, 1) < 0)
					goto fail;
			} else {
				BUILD_BUG();
			}
			loops_cnt++;
		}
		barrier(); /* compiler barrier */
		for (n = 0; n < elems; n++) {
			if (type & ALF_FLAG_SC) {
				if (alf_sc_dequeue(queue, (void **)&deq_obj, 1) < 0)
					goto fail;
			} else if (type & ALF_FLAG_MC) {
				if (alf_mc_dequeue(queue, (void **)&deq_obj, 1) < 0)
					goto fail;
			} else {
				BUILD_BUG();
			}
			loops_cnt++;
		}
	}

	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
fail:
	return -1;
}
/* Compiler should inline optimize other function calls out */
static int time_multi_enq_deq_mpmc(
	struct time_bench_record *rec, void *data)
{
	return time_multi_enq_deq(rec, data, MPMC);
}
static int time_multi_enq_deq_spsc(
	struct time_bench_record *rec, void *data)
{
	return time_multi_enq_deq(rec, data, SPSC);
}

static __always_inline int time_BULK_enq_deq(
	struct time_bench_record *rec, void *data,
	enum queue_behavior_type type)
{
#define MAX_BULK 32
	int *objs[MAX_BULK];
	int *deq_objs[MAX_BULK];
	int i;
	uint64_t loops_cnt = 0;
	int bulk = rec->step;
	struct alf_queue* queue = (struct alf_queue*)data;

	if (queue == NULL) {
		pr_err("Need alf_queue as input\n");
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
		if (type & ALF_FLAG_SP) {
			if (alf_sp_enqueue(queue, (void**)objs, bulk) < 0)
				goto fail;
		} else if (type & ALF_FLAG_MP) {
			if (alf_mp_enqueue(queue, (void**)objs, bulk) < 0)
				goto fail;
		} else {
			BUILD_BUG();
		}
		loops_cnt += bulk;

		barrier(); /* compiler barrier */
		if (type & ALF_FLAG_SC) {
			if (alf_sc_dequeue(queue, (void **)deq_objs, bulk) < 0)
				goto fail;
		} else if (type & ALF_FLAG_MC) {
			if (alf_mc_dequeue(queue, (void **)deq_objs, bulk) < 0)
				goto fail;
		} else {
			BUILD_BUG();
		}
		loops_cnt +=bulk;
	}

	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
fail:
	return -1;
}
/* Compiler should inline optimize other function calls out */
static int time_BULK_enq_deq_mpmc(
	struct time_bench_record *rec, void *data)
{
	return time_BULK_enq_deq(rec, data, MPMC);
}
static int time_BULK_enq_deq_spsc(
	struct time_bench_record *rec, void *data)
{
	return time_BULK_enq_deq(rec, data, SPSC);
}


int run_benchmark_tests(void)
{
	uint32_t loops = 10000000;
	int ring_size = 512;
	int passed_count = 0;
	struct alf_queue *MPMC;
	struct alf_queue *SPSC;

	/* Results listed below for a E5-2695 CPU */

	/*  0.360 ns cost overhead of the for loop */
	time_bench_loop(loops*10, 0,
			"for_loop", NULL, time_bench_for_loop);

	/* MPMC: Multi-Producer-Multi-Consumer tests */
	MPMC = alf_queue_alloc(ring_size, GFP_KERNEL);

	/* 10.910 ns cost for single enqueue or dequeue */
	time_bench_loop(loops, 0, "ALF-MPMC-simple", MPMC,
			time_bench_one_enq_deq_mpmc);

	/* 13.576 ns cost when touching more of the array  */
	time_bench_loop(loops/100, 128, "ALF-MPMC-multi", MPMC,
			time_multi_enq_deq_mpmc);

	/* Bulk MPMC */
	time_bench_loop(loops,  2, "MPMC-bulk2",  MPMC, time_BULK_enq_deq_mpmc);
	time_bench_loop(loops,  3, "MPMC-bulk3",  MPMC, time_BULK_enq_deq_mpmc);
	time_bench_loop(loops,  4, "MPMC-bulk4",  MPMC, time_BULK_enq_deq_mpmc);
	time_bench_loop(loops,  6, "MPMC-bulk6",  MPMC, time_BULK_enq_deq_mpmc);
	time_bench_loop(loops,  8, "MPMC-bulk8",  MPMC, time_BULK_enq_deq_mpmc);
	time_bench_loop(loops, 16, "MPMC-bulk16", MPMC, time_BULK_enq_deq_mpmc);

	alf_queue_free(MPMC);

	/* SPSC: Single-Producer-Single-Consumer tests */
	SPSC = alf_queue_alloc(ring_size, GFP_KERNEL);

	time_bench_loop(loops*10, 0, "ALF-SPSC-simple", SPSC,
			time_bench_one_enq_deq_spsc);
	time_bench_loop(loops/10, 128, "ALF-SPSC-multi", SPSC,
			time_multi_enq_deq_spsc);
	/* Bulk SPSC */
	time_bench_loop(loops,  2, "SPSC-bulk2",  SPSC, time_BULK_enq_deq_spsc);
	time_bench_loop(loops,  3, "SPSC-bulk3",  SPSC, time_BULK_enq_deq_spsc);
	time_bench_loop(loops,  4, "SPSC-bulk4",  SPSC, time_BULK_enq_deq_spsc);
	time_bench_loop(loops,  6, "SPSC-bulk6",  SPSC, time_BULK_enq_deq_spsc);
	time_bench_loop(loops,  8, "SPSC-bulk8",  SPSC, time_BULK_enq_deq_spsc);
	time_bench_loop(loops, 16, "SPSC-bulk16", SPSC, time_BULK_enq_deq_spsc);

	alf_queue_free(SPSC);
	return passed_count;
}

static int __init alf_queue_bench_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_benchmark_tests() < 0) {
		return -ECANCELED;
	}


	return 0;
}
module_init(alf_queue_bench_module_init);

static void __exit alf_queue_bench_module_exit(void)
{
	// TODO: perform sanity checks, and free mem
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(alf_queue_bench_module_exit);

MODULE_DESCRIPTION("Benchmark of Array-based Lock-Free queue");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

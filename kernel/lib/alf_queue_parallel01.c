/*
* Concurrency/parallel benchmark module for linux/alf_queue.h usage
*  a Producer/Consumer Array-based Lock-Free pointer queue
*/
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/alf_queue.h>
#include <linux/time_bench.h>
#include <linux/slab.h>

static int verbose=1;

static int parallel_cpus = 4;
module_param(parallel_cpus, uint, 0);
MODULE_PARM_DESC(parallel_cpus, "Number of parallel CPUs (default 4)");

#define ALF_FLAG_MP 0x1  /* Multi  Producer */
#define ALF_FLAG_MC 0x2  /* Multi  Consumer */
#define ALF_FLAG_SP 0x4  /* Single Producer */
#define ALF_FLAG_SC 0x8  /* Single Consumer */

enum queue_behavior_type {
	MPMC = (ALF_FLAG_MP|ALF_FLAG_MC),
	SPSC = (ALF_FLAG_SP|ALF_FLAG_SC)
};

static __always_inline int time_bench_CPU_enq_or_deq(
	struct time_bench_record *rec, void *data,
	enum queue_behavior_type type)
{
	int on_stack = 123;
	int *obj = &on_stack;
	int *deq_obj = NULL;
	int i;
	uint64_t loops_cnt = 0;
	struct alf_queue *queue = (struct alf_queue*)data;
	bool enq_CPU = false;

	if (queue == NULL) {
		pr_err("Need queue struct ptr as input\n");
		return -1;
	}
	/* loop count is limited to 32-bit due to div_u64_rem() use */
	if (((uint64_t)rec->loops * 2) >= ((1ULL<<32)-1)) {
		pr_err("Loop cnt too big will overflow 32-bit\n");
		return 0;
	}

	/* Split CPU between enq/deq based on even/odd */
	if ((smp_processor_id() % 2)== 0)
		enq_CPU = true;

	/* Hack: use "step" to mark enq/deq, as "step" gets printed */
	rec->step = enq_CPU;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		if (enq_CPU) {
			/* Compile will hopefully optimized this out */
			if (type & ALF_FLAG_SP) {
				if (alf_sp_enqueue(queue, (void **)&obj, 1)!=1)
					goto finish_early;
			} else if (type & ALF_FLAG_MP) {
				if (alf_mp_enqueue(queue, (void **)&obj, 1)!=1)
					goto finish_early;
			} else {
				BUILD_BUG();
			}
		} else {
			if (type & ALF_FLAG_SC) {
				if (alf_sc_dequeue(queue,
						   (void **)&deq_obj, 1) != 1)
					goto finish_early;
			} else if (type & ALF_FLAG_MC) {
				if (alf_mc_dequeue(queue,
						   (void **)&deq_obj, 1) != 1)
					goto finish_early;
			} else {
				BUILD_BUG();
			}
		}
		barrier(); /* compiler barrier */
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;

finish_early:
	time_bench_stop(rec, loops_cnt);
	if (enq_CPU) {
		pr_err("%s() WARN: enq fullq(CPU:%d) i:%d\n",
		       __func__, smp_processor_id(), i);
	} else {
		pr_err("%s() WARN: deq emptyq (CPU:%d) i:%d\n",
		       __func__, smp_processor_id(), i);
	}
	return loops_cnt;
}
/* Compiler should inline optimize other function calls out */
static int time_bench_CPU_enq_or_deq_mpmc(
	struct time_bench_record *rec, void *data)
{
	return time_bench_CPU_enq_or_deq(rec, data, MPMC);
}
static int time_bench_CPU_enq_or_deq_spsc(
	struct time_bench_record *rec, void *data)
{
	return time_bench_CPU_enq_or_deq(rec, data, SPSC);
}

int run_parallel(const char *desc, uint32_t loops, const cpumask_t *cpumask,
		 int step, void *data,
		 int (*func)(struct time_bench_record *record, void *data)
	)
{
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	size_t size;

	/* Allocate records for every CPU */
	size = sizeof(*cpu_tasks) * num_possible_cpus();
	cpu_tasks = kzalloc(size, GFP_KERNEL);

	time_bench_run_concurrent(loops, step, data,
				  cpumask, &sync, cpu_tasks, func);
	time_bench_print_stats_cpumask(desc, cpu_tasks, cpumask);

	kfree(cpu_tasks);
	return 1;
}

struct alf_queue* alloc_and_init_queue(int q_size, int prefill)
{
	struct alf_queue *queue;
	void *object;
	int i;

	/* Allocate and prefill alf_queue queue
	 */
	queue = alf_queue_alloc(q_size, GFP_KERNEL);
	if (IS_ERR_OR_NULL(queue)) {
		pr_err("%s() err creating alf_queue queue size:%d\n",
		       __func__, q_size);
		return NULL;
	}
	/* Fake pointer value to enqueue */
	object = (void *)(unsigned long)42;
	/* IMPORTANT:
	 *  Prefill with objects, in-order to keep enough distance
	 *  between producer and consumer, so the benchmark does not
	 *  run dry of objects to dequeue.
	 */
	for (i = 0; i < prefill; i++) {
		if (alf_mp_enqueue(queue, (void **)&object, 1) != 1) {
			pr_err("%s() err cannot prefill:%d sz:%d\n",
			       __func__, prefill, q_size);
			/* Only fake objects added. Thus, simply free queue */
			alf_queue_free(queue);
			return NULL;
		}
	}

	return queue;
}

static void run_parallel_two_CPUs(enum queue_behavior_type type,
				  uint32_t loops, int q_size, int prefill)
{
	struct alf_queue *queue = NULL;
	cpumask_t cpumask;

	if (!(queue = alloc_and_init_queue(q_size, prefill)))
		return; /* fail */

	/* Restrict the CPUs to run on
	 */
	cpumask_clear(&cpumask);
	cpumask_set_cpu(0, &cpumask);
	cpumask_set_cpu(1, &cpumask);

	if (type & SPSC) {
		run_parallel("alf_queue_SPSC_parallel_two_CPUs",
			     loops, &cpumask, 0, queue,
			     time_bench_CPU_enq_or_deq_spsc);
	} else if (type & MPMC) {
		run_parallel("alf_queue_MPMC_parallel_two_CPUs",
			     loops, &cpumask, 0, queue,
			     time_bench_CPU_enq_or_deq_mpmc);
	} else {
		pr_err("%s() WRONG TYPE!!! FIX\n", __func__);
	}
	alf_queue_free(queue);
}



static void run_parallel_many_CPUs(enum queue_behavior_type type,
				    uint32_t loops, int q_size, int prefill,
				    int CPUs)
{
	struct alf_queue *queue = NULL;
	cpumask_t cpumask;
	int i;

	if (CPUs == 0)
		return;

	if (!(queue = alloc_and_init_queue(q_size, prefill)))
		return; /* fail */

	/* Restrict the CPUs to run on
	 */
	if (verbose)
		pr_info("Limit to %d parallel CPUs\n", CPUs);
	cpumask_clear(&cpumask);
	for (i = 0; i < CPUs ; i++) {
		cpumask_set_cpu(i, &cpumask);
	}

	if (type & SPSC) {
		if (CPUs > 2) {
			pr_err("%s() ERR SPSC does not support CPUs > 2\n",
			       __func__);
			goto out;
		}
		run_parallel("alf_queue_SPSC_parallel_many_CPUs",
			     loops, &cpumask, 0, queue,
			     time_bench_CPU_enq_or_deq_spsc);
	} else if (type & MPMC) {
		run_parallel("alf_queue_MPMC_parallel_many_CPUs",
			     loops, &cpumask, 0, queue,
			     time_bench_CPU_enq_or_deq_mpmc);
	} else {
		pr_err("%s() WRONG TYPE!!! FIX\n", __func__);
	}
out:
	alf_queue_free(queue);
}

int run_benchmark_tests(void)
{
      //uint32_t loops = 1000000;
	uint32_t loops = 100000;
	int prefill = 32000;
	int q_size = 65536;

	run_parallel_two_CPUs(MPMC, loops, q_size, prefill);
	run_parallel_two_CPUs(SPSC, loops, q_size, prefill);

	run_parallel_many_CPUs(MPMC, loops, q_size, prefill, parallel_cpus);
	//run_parallel_many_CPUs(SPSC, loops, q_size, prefill, parallel_cpus);

	return 0;
}

static int __init alf_queue_parallel01_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_benchmark_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(alf_queue_parallel01_module_init);

static void __exit alf_queue_parallel01_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(alf_queue_parallel01_module_exit);

MODULE_DESCRIPTION("Concurrency/parallel benchmarking of alf_queue");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

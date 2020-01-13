/*
 * Concurrency/parallel benchmark module for linux/skb_array.h
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time_bench.h>
#include <linux/mm.h> /* missing in ptr_ring.h on >= v4.16 */
#include <linux/skb_array.h>

static int verbose=1;

//static int parallel_cpus = 0; /* disable many CPUs test default */
static int parallel_cpus = 4;
module_param(parallel_cpus, uint, 0);
MODULE_PARM_DESC(parallel_cpus, "Number of parallel CPUs (default 4)");

/* This is the main benchmark function.
 *
 *  lib/time_bench.c:time_bench_run_concurrent() sync concurrent execution
 *
 * Notice this function is called by different CPUs, and the enq/deq
 * behavior is dependend on CPU id number.
 */
static int time_bench_CPU_enq_or_deq(
	struct time_bench_record *rec, void *data)
{
	struct skb_array *queue = (struct skb_array*)data;
	struct sk_buff *skb, *nskb;
	uint64_t loops_cnt = 0;
	int i;

	bool enq_CPU = false;

	/* Split CPU between enq/deq based on even/odd */
	if ((smp_processor_id() % 2)== 0)
		enq_CPU = true;

	/* Hack: use "step" to mark enq/deq, as "step" gets printed */
	rec->step = enq_CPU;

	/* Fake pointer value to enqueue */
	skb = (struct sk_buff *)(unsigned long)42;

	if (queue == NULL) {
		pr_err("Need queue ptr as input\n");
		return 0;
	}
	/* loop count is limited to 32-bit due to div_u64_rem() use */
	if (((uint64_t)rec->loops * 2) >= ((1ULL<<32)-1)) {
		pr_err("Loop cnt too big will overflow 32-bit\n");
		return 0;
	}

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		if (enq_CPU) {
			/* enqueue side */
			if (skb_array_produce(queue, skb) < 0) {
				pr_err("%s() WARN: enq fullq(CPU:%d) i:%d\n",
				       __func__, smp_processor_id(), i);
				goto finish_early;
			}
		} else {
			/* dequeue side */
			nskb = skb_array_consume(queue);
			if (nskb == NULL) {
				pr_err("%s() WARN: deq emptyq (CPU:%d) i:%d\n",
				       __func__, smp_processor_id(), i);
				goto finish_early;
			}
		}
		loops_cnt++;
		barrier(); /* compiler barrier */
	}
finish_early:
	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
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

/* Helper for emptying the queue before calling skb_array_cleanup(),
 * because we are using fake SKB pointers, which will Oops the kernel
 * if the destructor kfree_skb() is invoked.
 */
void helper_empty_queue(struct skb_array *queue)
{
	struct sk_buff *skb;

	while ((skb = skb_array_consume(queue)))
		/* Emptying fake SKB pointers */;
}

bool init_queue(struct skb_array *queue, int q_size, int prefill)
{
	struct sk_buff *skb;
	int result, i;

	/* Allocate and prefill skb_array queue
	 */
	result = skb_array_init(queue, q_size, GFP_KERNEL);
	if (result < 0) {
		pr_err("%s() err creating skb_array queue size:%d\n",
		       __func__, q_size);
		return false;
	}
	/* Fake pointer value to enqueue */
	skb = (struct sk_buff *)(unsigned long)42;
	/* IMPORTANT:
	 *  Prefill with objects, in-order to keep enough distance
	 *  between producer and consumer, so the benchmark does not
	 *  run dry of objects to dequeue.
	 */
	for (i = 0; i < prefill; i++) {
		if (skb_array_produce_bh(queue, skb) < 0) {
			pr_err("%s() err cannot prefill:%d sz:%d\n",
			       __func__, prefill, q_size);
			helper_empty_queue(queue);
			skb_array_cleanup(queue);
			return false;
		}
	}

	return true;
}

void noinline run_parallel_two_CPUs(uint32_t loops, int q_size, int prefill)
{
	struct skb_array *queue;
	cpumask_t cpumask;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);

	/* Restrict the CPUs to run on
	 */
	cpumask_clear(&cpumask);
	cpumask_set_cpu(0, &cpumask);
	cpumask_set_cpu(1, &cpumask);

	if (!init_queue(queue, q_size, prefill))
	    goto fail;

	run_parallel("skb_array_parallel_two_CPUs",
		     loops, &cpumask, 0, queue,
		     time_bench_CPU_enq_or_deq);

	helper_empty_queue(queue); /* dequeue fake pointers before cleanup */
	skb_array_cleanup(queue);
fail:
	kfree(queue);
}

void noinline run_parallel_many_CPUs(uint32_t loops, int q_size, int prefill)
{
	struct skb_array *queue;
	cpumask_t cpumask;
	int i;

	/* This test is dependend on module parm */
	if (parallel_cpus == 0)
		return;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);

	/* Restrict the CPUs to run on, depending on
	 * global module parameter: parallel_cpus
	 */
	if (verbose)
		pr_info("Limit to %d parallel CPUs\n", parallel_cpus);
	cpumask_clear(&cpumask);
	for (i = 0; i < parallel_cpus ; i++) {
		cpumask_set_cpu(i, &cpumask);
	}

	if (!init_queue(queue, q_size, prefill))
	    goto fail;

	run_parallel("skb_array_parallel_many_CPUs",
		     loops, &cpumask, 0, queue,
		     time_bench_CPU_enq_or_deq);

	helper_empty_queue(queue); /* dequeue fake pointers before cleanup */
	skb_array_cleanup(queue);
fail:
	kfree(queue);
}


int run_benchmark_tests(void)
{
	/* ADJUST: These likely need some adjustments on different
	 * systems, else the tests likely cannot "complete", because
	 * the CPUs catchup to each-other.
	 *
	 * The benchmark will stop as soon as the CPUs catchup, either
	 * when the queue is full, or the queue is empty.
	 *
	 * If the test does not complete the number of "loops", then
	 * the results are still showed, but a WARNing is printed
	 * indicating how many interations were completed.  Thus, you
	 * can judge if the results are valid.
	 */
	// uint32_t loops = 1000000;
	uint32_t loops = 200000;
	int prefill = 32000;
	int q_size = 64000;

	if (verbose)
		pr_info("For 'skb_array_parallel_two_CPUs'"
			" step = enq(1)/deq(0)"
			", cost is either enqueue or dequeue\n");

	run_parallel_two_CPUs(loops, q_size, prefill);

	run_parallel_many_CPUs(loops, q_size, prefill);

	return 0;
}

static int __init skb_array_parallel01_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_benchmark_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(skb_array_parallel01_module_init);

static void __exit skb_array_parallel01_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(skb_array_parallel01_module_exit);

MODULE_DESCRIPTION("Concurrency/parallel benchmarking of skb_array");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

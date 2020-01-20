/*
 * Benchmark module for page_pool.
 *
 * Cross CPU tests.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/time_bench.h>
#include <net/page_pool.h>

#include <linux/interrupt.h>
#include <linux/limits.h>

/* notice time_bench is limited to U32_MAX nr loops */
static unsigned long loops = 10000000;
module_param(loops, ulong, 0);
MODULE_PARM_DESC(loops, "Specify loops bench will run");

static int verbose=1;
#define MY_POOL_SIZE	1024

/*
 * Benchmark idea:
 *
 * One process simulate NIC-RX, which needs to allocate page to refill it's
 * RX-ring. This needs to run under softirq (here as taskset).
 *
 * Multiple other processes, running on remote CPUs, will return pages into
 * the page_pool. Simulation a page getting freed from a remote CPU, being
 * returned to page_pool.  These should runs under softirq, as this usually
 * happens during DMA TX completion, but we can live with not using softirq.
 *
 * Issue #1: Real struct "page".
 *
 * The object used in the benchmark needs to be real struct page'es, due to
 * functions used by page_pool, like page_to_nid(page), page_ref_count(page),
 * page_is_pfmemalloc().  It would have been easier if we could use dummy
 * pointers.
 *
 * Issue #2:
 *
 * The objects/pages need to be returned from a remote CPU, obviously need to
 * be transfered from the originating CPU first.  This step must not be the
 * bottleneck, as it is the page_pool return-bottleneck that we want to
 * benchmark.
 *
 * Solution idea for Issue #2"
 *
 * Create multiple ptr_ring's one for each remote-CPU, in-practis creating
 * SPSC queues, which should be faster than the page_pool MPSC ptr_ring
 * setup.  These queues will have a bounded size, which will be the limiting
 * factor for refill-simulator CPU.
 *
 */

bool init_cpu_queue(struct ptr_ring *queue, int q_size, int prefill,
		    struct page_pool *pp)
{
	gfp_t gfp_mask = (GFP_KERNEL);
	struct page *page;
	int result, i;

	result = ptr_ring_init(queue, q_size, GFP_KERNEL);
	if (result < 0) {
		pr_err("%s() err creating queue size:%d\n", __func__, q_size);
		return false;
	}

	/*
	 *  Prefill with objects, in-order to keep enough distance
	 *  between producer and consumer, so the benchmark does not
	 *  run dry of objects to dequeue.
	 */
	for (i = 0; i < prefill; i++) {
		page = page_pool_alloc_pages(pp, gfp_mask);
		if (!page) {
			pr_err("%s() alloc cannot prefill:%d sz:%d\n",
			       __func__, prefill, q_size);
			return false;
		}
		if (ptr_ring_produce(queue, page) < 0) {
			pr_err("%s() queue cannot prefill:%d sz:%d\n",
			       __func__, prefill, q_size);
			return false;
		}
	}
	return true;
}

struct page_pool *pp_create(int pool_size)
{
	struct page_pool *pp;
	int err;

	struct page_pool_params pp_params = {
		.order = 0,
		.flags = 0,
		.pool_size = pool_size,
		.nid = NUMA_NO_NODE,
		.dev = NULL, /* Only use for DMA mapping */
		.dma_dir = DMA_BIDIRECTIONAL,
	};

	pp = page_pool_create(&pp_params);
	if (IS_ERR(pp)) {
		err = PTR_ERR(pp);
		pr_warn("%s: Error(%d) creating page_pool\n", __func__, err);
		return NULL;
	}
	return pp;
}

struct datarec {
	struct page_pool *pp;
	int nr_cpus;
	struct ptr_ring *cpu_queues;
	// struct completion ?
	// Or just call tasklet_kill(&pp_tasklet) ?
};


static void pp_tasklet_simulate_rx_napi(unsigned long data)
{
	struct datarec *d = (struct datarec *)data;
	int cpu = smp_processor_id();

	if (in_serving_softirq())
		pr_warn("%s(%d): in_serving_softirq fast-path\n", __func__, cpu);
	else
		pr_warn("%s(%d): Cannot use page_pool fast-path\n", __func__, cpu);

}

int run_parallel(const char *desc, uint32_t nr_loops, const cpumask_t *cpumask,
		 int step, void *data,
		 int (*func)(struct time_bench_record *record, void *data)
	)
{
	DECLARE_TASKLET_DISABLED(pp_tasklet, pp_tasklet_simulate_rx_napi,
				 (unsigned long)data);
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	size_t size;

	// struct datarec *d = data; // Do have access to datarec here
//	tasklet_init(&pp_tasklet, pp_tasklet_simulate_rx_napi, (unsigned long)data);

	/* Allocate records for every CPU, even if CPU are limited in cpumask */
	size = sizeof(*cpu_tasks) * num_possible_cpus();
	cpu_tasks = kzalloc(size, GFP_KERNEL);

	/* Start tasklet here */
	tasklet_enable(&pp_tasklet);
	/* XXX: Runs on the CPU that schedule it, how to control this? taskset?*/
	tasklet_schedule(&pp_tasklet);

	time_bench_run_concurrent(nr_loops, step, data,
				  cpumask, &sync, cpu_tasks, func);
	/* After here remote CPU kthread's have been shutdown */
	time_bench_print_stats_cpumask(desc, cpu_tasks, cpumask);

	kfree(cpu_tasks);

	// Should we add tasklet shutdown+sync here?
	// Use tasklet_kill() or sync on mutex or struct completion ???
	tasklet_kill(&pp_tasklet); // hmm.. what if already tasklet finished?
	return 1;
}

static int time_example(
	struct time_bench_record *rec, void *data)
{
	struct datarec *d = data;
	uint64_t loops_cnt = 0;
	int i;

	pr_info("%s(): ran on CPU:%d expect nr_cpus:%d\n",
		__func__, smp_processor_id(), d->nr_cpus);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		barrier();
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	return loops_cnt;
}

static void empty_ptr_ring(struct page_pool *pp, struct ptr_ring *ring)
{
	struct page *page;

	while ((page = ptr_ring_consume_bh(ring))) {
		page_pool_put_page(pp, page, false);
	}
}

void noinline run_bench_pp_2cpus(
	uint32_t nr_loops, int q_size, int prefill)
{
	struct ptr_ring *cpu_queues;
	struct page_pool *pp;
	cpumask_t cpumask;
	struct datarec d;
	int nr_cpus;
	int i;

	pp = pp_create(MY_POOL_SIZE);
	if (!pp)
		return;

	/* Restrict the CPUs to run on
	 */
	cpumask_clear(&cpumask);
	cpumask_set_cpu(0, &cpumask);
	cpumask_set_cpu(1, &cpumask);
	nr_cpus = 2;

	cpu_queues = kzalloc(sizeof(*cpu_queues) * nr_cpus, GFP_KERNEL);

	for (i = 0; i < nr_cpus; i++) {
		if (!init_cpu_queue(&cpu_queues[i], q_size, prefill, pp))
			goto fail;
	}

	d.pp = pp;
	d.nr_cpus = nr_cpus;
	d.cpu_queues = cpu_queues;
	run_parallel("TEST",
		     nr_loops, &cpumask, 0, &d,
		     time_example);
	/* Remote CPU kthread have been taken down after call in
	 * run_parallel() time_bench_run_concurrent(), BUT the tasklet
	 * simulating softirq is not part of that sync....  Thus, we
	 * have to create extra tasklet sync step before we can
	 * release pp and cpu_queues.  Perhaps hide this in
	 * run_parallel() call.
	 */

fail:
	for (i = 0; i < nr_cpus; i++) {
		empty_ptr_ring(pp, &cpu_queues[i]);
		ptr_ring_cleanup(&cpu_queues[i], NULL);
	}
	kfree(cpu_queues);
	page_pool_destroy(pp);
}

int run_benchmarks(void)
{
	uint32_t nr_loops = loops;

	run_bench_pp_2cpus(nr_loops, 256, 0);

	return 1;
}

static int __init bench_page_pool_cross_cpu_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (loops > U32_MAX) {
		pr_err("Module param loops(%lu) exceeded U32_MAX(%u)\n",
		       loops, U32_MAX);
		return -ECHRNG;
	}

	run_benchmarks();
	return 0;
}
module_init(bench_page_pool_cross_cpu_module_init);

static void __exit bench_page_pool_cross_cpu_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(bench_page_pool_cross_cpu_module_exit);

MODULE_DESCRIPTION("Benchmark of page_pool cross-CPU cases");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

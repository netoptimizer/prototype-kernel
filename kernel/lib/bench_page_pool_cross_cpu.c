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
#include <linux/delay.h>
#include <linux/version.h>

/* notice time_bench is limited to U32_MAX nr loops */
static unsigned long loops = 1000000;
module_param(loops, ulong, 0);
MODULE_PARM_DESC(loops, "Specify loops bench will run");

static unsigned int returning_cpus = 2;
module_param(returning_cpus, uint, 0);
MODULE_PARM_DESC(returning_cpus, "Concurrent CPUs returning pages");

static int verbose=1;
//#define MY_POOL_SIZE	4096
#define MY_POOL_SIZE	32000

#define SPSC_QUEUE_SZ	1024

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static inline
void _page_pool_put_page(struct page_pool *pool, struct page *page,
			 bool allow_direct)
{
	page_pool_put_page(pool, page, -1, allow_direct);
}
#else
static inline
void _page_pool_put_page(struct page_pool *pool, struct page *page,
			 bool allow_direct)
{
	page_pool_put_page(pool, page, allow_direct);
}
#endif

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

/* Helper for filling some page's into page_pool's internal ptr_ring */
static void pp_prefill(struct page_pool *pp, int elems)
{
	gfp_t gfp_mask = GFP_KERNEL;
	struct page **array;
	int i;

	array = kzalloc(sizeof(struct page*) * elems, gfp_mask);

	for (i = 0; i < elems; i++) {
		array[i] = page_pool_alloc_pages(pp, gfp_mask);
	}
	for (i = 0; i < elems; i++) {
		_page_pool_put_page(pp, array[i], false);
	}

	kfree(array);
}

struct page_pool *pp_create(int pool_size, unsigned int prefill)
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
	pp_prefill(pp, prefill);

	return pp;
}

struct datarec {
	struct page_pool *pp;
	int nr_cpus;
	unsigned int nr_loops;
	struct ptr_ring *cpu_queues;
	struct mutex wait_for_tasklet;
	int tasklet_cpu;
	struct tasklet_struct pp_tasklet;
};


static void pp_tasklet_simulate_rx_napi(unsigned long data)
{
	gfp_t gfp_mask = GFP_ATOMIC; /* GFP_ATOMIC is not really needed */
	struct datarec *d = (struct datarec *)data;
	struct page_pool *pp = d->pp;
	int cpu = smp_processor_id();
	u64 nr_produce, cnt = 0;
	u64 max_attempts, full = 0;
	struct page *page;
	struct ptr_ring *queue;
	u32 queue_id, queue_rr = 0;

	/* How many pages does bench loops expect to get */
	nr_produce = (d->nr_cpus * d->nr_loops);
	/* Add queue size that gets full once consumer stops */
	nr_produce += (SPSC_QUEUE_SZ * d->nr_cpus);
	max_attempts = nr_produce * 1000;

	if (verbose)
		pr_info("%s(): started on CPU:%d (nr:%llu)\n",
			__func__, cpu, nr_produce);

	while (cnt < nr_produce && --max_attempts) {

		page = page_pool_alloc_pages(pp, gfp_mask);
		if (!page) {
			pr_err("%s(): out-of-pages\n", __func__);
			continue;
			// TODO: How to break loop and stop kthreads?
		}

		queue_id = queue_rr++ % d->nr_cpus;
		queue = &(d->cpu_queues[queue_id]);
		if (__ptr_ring_produce(queue, page) < 0) {
			full++;
			page_pool_recycle_direct(pp, page);
			continue;
		}
		cnt++;
	}

	if (max_attempts == 0) {
		pr_err("%s(%d): FAIL (cnt:%llu), queue full(%llu) too many times\n",
		       __func__, cpu, cnt, full);
	} else {
		pr_info("%s(cpu:%d): done (cnt:%llu) queue full(%llu)\n",
			__func__, cpu, cnt, full);
	}

	mutex_unlock(&d->wait_for_tasklet); /* others are waiting on unlock */
}

static int time_pp_put_page_recycle(
	struct time_bench_record *rec, void *data)
{
	int cpu = smp_processor_id();
	struct datarec *d = data;
	uint64_t retry_cnt = 0;
	uint64_t loops_cnt = 0;
	uint64_t wait_cnt = 0;
	struct ptr_ring *queue;
	struct page *page;
	int i;

	if (verbose)
		pr_info("%s(): run on CPU:%d expect nr_cpus:%d\n",
			__func__, cpu, d->nr_cpus);

	/* Schedule tasklet on one of the CPUs */
	if (d->tasklet_cpu == cpu) {
		u64 nr_produce = (d->nr_cpus * d->nr_loops) + d->nr_cpus;
		time_bench_start(rec);
		tasklet_schedule(&d->pp_tasklet);
		mutex_lock(&d->wait_for_tasklet); /* Block waiting for tasklet */
		time_bench_stop(rec, nr_produce);
		return nr_produce;
		/* Returns to kthread_should_stop while loop */
	}

	queue = &d->cpu_queues[cpu];

	/* Spin waiting for first page */
	while (!(page = ptr_ring_consume(queue))) {
		if ((++wait_cnt % 1000000) == 0 ) {
			pr_info("%s(cpu:%d): waiting(%llu) on first page\n",
				__func__, cpu, wait_cnt);
		}
	}
	ndelay(400); /* Small delay to get more objects in queue */
	//udelay(2);
	_page_pool_put_page(d->pp, page, false);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

	retry:
		page = ptr_ring_consume(queue);
		if (page == NULL) { /* Empty queue */
			retry_cnt++;
			if (retry_cnt > (rec->loops*100)) {
				pr_err("%s(cpu:%d): abort on retries\n",
				       __func__, cpu);
				break;
			}
			goto retry;
		}

		/* Issue: If page_pool ptr_ring is full, page will be
		 * returned to page-allocator. Can we determine if it
		 * happens?!?
		 */
//		ndelay(10);
		_page_pool_put_page(d->pp, page, false);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);

	pr_info("%s(cpu:%d): recycled:%llu pages, empty:%llu times\n",
		__func__, cpu, loops_cnt, retry_cnt);

	return loops_cnt;
}

int run_parallel(const char *desc, uint32_t nr_loops, const cpumask_t *cpumask,
		 int step, void *data,
		 int (*func)(struct time_bench_record *record, void *data)
	)
{
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	size_t size;

	/* Allocate records for every CPU, even if CPU are limited in cpumask */
	size = sizeof(*cpu_tasks) * num_possible_cpus();
	cpu_tasks = kzalloc(size, GFP_KERNEL);

	time_bench_run_concurrent(nr_loops, step, data,
				  cpumask, &sync, cpu_tasks, func);
	/* After here remote CPU kthread's have been shutdown */
	time_bench_print_stats_cpumask(desc, cpu_tasks, cpumask);

	kfree(cpu_tasks);
	return 1;
}

static void empty_ptr_ring(struct page_pool *pp, struct ptr_ring *ring)
{
	struct page *page;

	while ((page = ptr_ring_consume_bh(ring))) {
		_page_pool_put_page(pp, page, false);
	}
}

void noinline run_bench_pp_cpus(
	int nr_cpus, uint32_t nr_loops, int q_size, int prefill)
{
	struct ptr_ring *cpu_queues;
	struct page_pool *pp;
	cpumask_t cpumask;
	struct datarec d;
	int i, j;

	tasklet_init(&d.pp_tasklet, pp_tasklet_simulate_rx_napi,
		     (unsigned long)&d);

	pp = pp_create(MY_POOL_SIZE, 256 /*prefill*/ );
	if (!pp)
		return;

	/* Restrict the CPUs to run on
	 */
	cpumask_clear(&cpumask);
	for (i = 0; i < nr_cpus; i++) {
		cpumask_set_cpu(i, &cpumask);
	}
	/* Extra CPU for tasklet */
	cpumask_set_cpu(i, &cpumask);
	d.tasklet_cpu = i;

	cpu_queues = kzalloc(sizeof(*cpu_queues) * nr_cpus, GFP_KERNEL);

	for (i = 0; i < nr_cpus; i++) {
		if (!init_cpu_queue(&cpu_queues[i], q_size, prefill, pp))
			goto fail;
	}

	d.pp = pp;
	d.nr_cpus = nr_cpus;
	d.cpu_queues = cpu_queues;
	d.nr_loops = nr_loops;
	mutex_init(&d.wait_for_tasklet);

	mutex_lock(&d.wait_for_tasklet);
	//tasklet_enable(&d.pp_tasklet);
	/* tasklet schedule happens in time_pp_put_page_recycle() */

	run_parallel("page_pool_cross_cpu",
		     nr_loops, &cpumask, nr_cpus, &d,
		     time_pp_put_page_recycle);

//	mutex_lock(&d.wait_for_tasklet); /* Block waiting for tasklet */
	tasklet_kill(&d.pp_tasklet);
fail:
	for (j = 0; j < i; j++) {
		empty_ptr_ring(pp, &cpu_queues[j]);
		ptr_ring_cleanup(&cpu_queues[j], NULL);
	}
	kfree(cpu_queues);
	page_pool_destroy(pp);
}

int run_benchmarks(void)
{
	uint32_t nr_loops = loops;

	run_bench_pp_cpus(returning_cpus, nr_loops, SPSC_QUEUE_SZ, 0);

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

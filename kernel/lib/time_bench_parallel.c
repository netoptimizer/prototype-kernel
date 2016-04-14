/*
 * Sample: Benchmark parallel concurrent executing code
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/cpumask.h>

static int verbose=1;

static int parallel_cpus = 0;
module_param(parallel_cpus, uint, 0);
MODULE_PARM_DESC(parallel_cpus, "Number of parallel CPUs (default ALL)");

/* Some global variable to use for contention points */
static DEFINE_SPINLOCK(global_lock);
static atomic_t global_atomic;

static int time_lock_unlock_local(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;
	spinlock_t local_lock;
	spin_lock_init(&local_lock);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		spin_lock(&local_lock);
		loops_cnt++;
		barrier();
		spin_unlock(&local_lock);
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_lock_unlock_global(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		spin_lock(&global_lock);
		loops_cnt++;
		barrier();
		spin_unlock(&global_lock);
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}


static int time_atomic_inc_dec_local(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;
	atomic_t atomic;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		atomic_inc(&(atomic));
		loops_cnt++;
		barrier();
		atomic_dec(&(atomic));
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static atomic_t global_atomic;
static int time_atomic_inc_dec_global(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		atomic_inc(&(global_atomic));
		loops_cnt++;
		barrier();
		atomic_dec(&(global_atomic));
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_atomic_read_local(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;
	atomic_t atomic;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		atomic_read(&(atomic));
		loops_cnt++;
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_atomic_read_global(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		atomic_read(&(global_atomic));
		loops_cnt++;
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}


static int time_local_bh(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		local_bh_disable();
		loops_cnt++;
		barrier();
		local_bh_enable();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_local_irq(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		local_irq_disable();
		loops_cnt++;
		barrier();
		local_irq_enable();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_local_irq_save(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;
	unsigned long flags;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		local_irq_save(flags);
		loops_cnt++;
		barrier();
		local_irq_restore(flags);
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_preempt(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		preempt_disable();
		loops_cnt++;
		barrier();
		preempt_enable();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

int run_parallel(const char *desc, uint32_t loops, const cpumask_t *cpumask,
		 int (*func)(struct time_bench_record *record, void *data)
	)
{
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	size_t size;

	/* Allocate records for every CPU */
	size = sizeof(*cpu_tasks) * num_possible_cpus();
	cpu_tasks = kzalloc(size, GFP_KERNEL);

	time_bench_run_concurrent(loops, 0, cpumask, &sync, cpu_tasks, func);
	time_bench_print_stats_cpumask(desc, cpu_tasks, cpumask);

	kfree(cpu_tasks);
	return 1;
}

int run_timing_tests(void)
{
	uint32_t loops = 1000000;
	cpumask_t cpumask;
	int i;

	/* Default run on all (online) CPUs */
	cpumask_copy(&cpumask, cpu_online_mask);

	/* Reduce CPUs to run on, via module parameter parallel_cpus */
	if (parallel_cpus != 0) {
		if (verbose)
			pr_info("Limit to %d parallel CPUs\n", parallel_cpus);
		cpumask_clear(&cpumask);
		for (i = 0; i < parallel_cpus ; i++) {
			cpumask_set_cpu(i, &cpumask);
		}
	}

	run_parallel("time_local_bh", loops, &cpumask,
		      time_local_bh);
	 /* For comparison */
	time_bench_loop(loops, 0, "time_local_bh",
			NULL,      time_local_bh);

	run_parallel("time_preempt", loops, &cpumask,
		      time_preempt);
	 /* For comparison */
	time_bench_loop(loops, 0, "time_preempt",
			NULL,      time_preempt);

	/* Experience: local IRQ disable seems to be affected slightly
	 * when parallel executing on HyperThreading sipling CPUs
	 */
	run_parallel("time_local_irq", loops, &cpumask,
		      time_local_irq);
	run_parallel("time_local_irq_save", loops, &cpumask,
		      time_local_irq_save);

	/* Atomic related */
	run_parallel("time_lock_unlock_local", loops, &cpumask,
		      time_lock_unlock_local);
	run_parallel("time_lock_unlock_global", loops, &cpumask,
		      time_lock_unlock_global);

	run_parallel("time_atomic_inc_dec_local", loops, &cpumask,
		      time_atomic_inc_dec_local);
	run_parallel("time_atomic_inc_dec_global", loops, &cpumask,
		      time_atomic_inc_dec_global);

	run_parallel("time_atomic_read_local", loops*100, &cpumask,
		      time_atomic_read_local);
	run_parallel("time_atomic_read_global", loops*100, &cpumask,
		      time_atomic_read_global);

	return 0;
}

static int __init time_bench_parallel_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_timing_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(time_bench_parallel_module_init);

static void __exit time_bench_parallel_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(time_bench_parallel_module_exit);

MODULE_DESCRIPTION("Benchmark parallel concurrent executing code");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

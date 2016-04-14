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

/* Quick and dirty way to unselect some of the benchmark tests, by
 * encoding this in a module parameter flag.  This is useful when
 * wanting to perf benchmark a specific benchmark test.
 *
 * Hint: Bash shells support writing binary number like: $((2#101010))
 * Use like:
 *  modprobe $MODULE parallel_cpus=4 run_flags=$((2#101))
 */
static unsigned long long run_flags = 0xFFFFFFFF;
module_param(run_flags, ullong, 0);
MODULE_PARM_DESC(run_flags, "Hack way to limit bench to run");
/* Count the bit number from the enum */
enum benchmark_bit {
	bit_run_bench_bh_preempt,
	bit_run_bench_irq_disable,
	bit_run_bench_locks,
	bit_run_bench_atomics,
	bit_run_bench_atomics_advanced,
};
#define bit(b)	(1 << (b))
#define run_or_return(b) do { if (!(run_flags & (bit(b)))) return; } while (0)



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

static int time_atomic_read_N_writers_global(
	struct time_bench_record *rec, void *data)
{
	uint64_t loops_cnt = 0;
	bool writer = false;
	int N = rec->step;
	int i;

	/* Select N CPUs to be come writers, atomic updaters */
	if (smp_processor_id() < N) {
		writer = true;
	}

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		if (writer)
			atomic_inc(&(global_atomic));
		else
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
		 int step,
		 int (*func)(struct time_bench_record *record, void *data)
	)
{
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	size_t size;

	/* Allocate records for every CPU */
	size = sizeof(*cpu_tasks) * num_possible_cpus();
	cpu_tasks = kzalloc(size, GFP_KERNEL);

	time_bench_run_concurrent(loops, step, cpumask, &sync, cpu_tasks, func);
	time_bench_print_stats_cpumask(desc, cpu_tasks, cpumask);

	kfree(cpu_tasks);
	return 1;
}

void noinline run_bench_bh_preempt(uint32_t loops, cpumask_t cpumask)
{
	run_or_return(bit_run_bench_bh_preempt);

	run_parallel("time_local_bh", loops, &cpumask, 0,
		      time_local_bh);
	 /* For comparison */
	time_bench_loop(loops, 0, "time_local_bh",
			NULL,      time_local_bh);

	run_parallel("time_preempt", loops, &cpumask, 0,
		      time_preempt);
	 /* For comparison */
	time_bench_loop(loops, 0, "time_preempt",
			NULL,      time_preempt);
}

void noinline run_bench_irq_disable(uint32_t loops, cpumask_t cpumask)
{
	run_or_return(bit_run_bench_irq_disable);

	/* Experience: local IRQ disable seems to be affected slightly
	 * when parallel executing on HyperThreading sipling CPUs
	 */
	run_parallel("time_local_irq", loops, &cpumask, 0,
		      time_local_irq);
	/* For comparison */
	time_bench_loop(loops, 0, "time_local_irq",
			NULL,      time_local_irq);

	run_parallel("time_local_irq_save", loops, &cpumask, 0,
		      time_local_irq_save);
	/* For comparison */
	time_bench_loop(loops, 0, "time_local_irq_save",
			NULL,      time_local_irq_save);

}

void noinline run_bench_locks(uint32_t loops, cpumask_t cpumask)
{
	run_or_return(bit_run_bench_locks);

	run_parallel("time_lock_unlock_local", loops, &cpumask, 0,
		      time_lock_unlock_local);
	run_parallel("time_lock_unlock_global", loops, &cpumask, 0,
		      time_lock_unlock_global);
}

void noinline run_bench_atomics(uint32_t loops, cpumask_t cpumask)
{
	run_or_return(bit_run_bench_atomics);

	run_parallel("time_atomic_inc_dec_local", loops, &cpumask, 0,
		      time_atomic_inc_dec_local);
	run_parallel("time_atomic_inc_dec_global", loops, &cpumask, 0,
		      time_atomic_inc_dec_global);

	run_parallel("time_atomic_read_local", loops*100, &cpumask, 0,
		      time_atomic_read_local);
	run_parallel("time_atomic_read_global", loops*100, &cpumask, 0,
		      time_atomic_read_global);
}

void noinline run_bench_atomics_advanced(uint32_t loops, cpumask_t cpumask)
{
	run_or_return(bit_run_bench_atomics_advanced);

	run_parallel("time_atomic_read_N_writers_global", loops, &cpumask, 1,
		      time_atomic_read_N_writers_global);
	run_parallel("time_atomic_read_N_writers_global", loops, &cpumask, 2,
		      time_atomic_read_N_writers_global);
	run_parallel("time_atomic_read_N_writers_global", loops, &cpumask, 3,
		      time_atomic_read_N_writers_global);
	run_parallel("time_atomic_read_N_writers_global", loops, &cpumask, 4,
		      time_atomic_read_N_writers_global);
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

	/* Selectable test types, see run_flags module parameter */
	run_bench_bh_preempt(loops, cpumask);
	run_bench_irq_disable(loops, cpumask);
	run_bench_locks(loops, cpumask);
	run_bench_atomics(loops, cpumask);
	run_bench_atomics_advanced(loops, cpumask);

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

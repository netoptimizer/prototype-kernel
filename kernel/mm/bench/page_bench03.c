/*
 * Benchmarking page allocator concurrency
 *  - parallel execution scalability
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/mm.h>

/* For concurrency testing */
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>

static int verbose=1;

#define DEFAULT_ORDER 0
static int page_order = DEFAULT_ORDER;
module_param(page_order, uint, 0);
MODULE_PARM_DESC(page_order, "Parameter page order to use in bench");

static int parallel_cpus = 2;
module_param(parallel_cpus, uint, 0);
MODULE_PARM_DESC(parallel_cpus, "Parameter for number of parallel CPUs");

/* Quick and dirty way to unselect some of the benchmark tests, by
 * encoding this in a module parameter flag.  This is useful when
 * wanting to perf benchmark a specific benchmark test.
 *
 * Hint: Bash shells support writing binary number like: $((2#101010))
 * Use like:
 *  modprobe page_bench03 page_order=1 parallel_cpus=4 run_flags=$((2#100))
 */
static unsigned long long run_flags = 0xFFFFFFFF;
module_param(run_flags, ullong, 0);
MODULE_PARM_DESC(run_flags, "Hack way to limit bench to run");
/* Count the bit number from the enum */
enum benchmark_bit {
	bit_run_bench_compare,
	bit_run_bench_parallel_all_cpus,
	bit_run_bench_limited_cpus
};
#define bit(b)	(1 << (b))
#define run_or_return(b) do { if (!(run_flags & (bit(b)))) return; } while (0)

struct time_bench_sync {
	atomic_t nr_tests_running;
	struct completion start_event;
};

/* Keep track of CPUs executing our bench function
 */
struct time_bench_cpu {
	struct time_bench_record rec;
	struct time_bench_sync *sync; /* back ptr */
	struct task_struct *task;
	struct cpumask mask;
        /* Support masking outsome CPUs, mark if it ran */
	bool did_bench_run;
	/* int cpu; // note CPU stored in time_bench_record */
	int (*bench_func)(struct time_bench_record *record, void *data);
};

static int time_alloc_pages(
	struct time_bench_record *rec, void *data)
{
	/* Important to set: __GFP_COMP for compound pages
	 */
	gfp_t gfp_mask = (GFP_ATOMIC | __GFP_COLD | __GFP_COMP);
	struct page *page;
	int order = rec->step;
	int i;

	/* Drop WARN on failures, time_bench will invalidate test */
	gfp_mask |= __GFP_NOWARN;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		page = alloc_pages(gfp_mask, order);
		if (unlikely(page == NULL))
			return 0;
		__free_pages(page, order);
	}
	time_bench_stop(rec, i);
	return i;
}

/* Function getting invoked by kthread */
static int invoke_test_on_cpu_func(void *private)
{
	struct time_bench_cpu *cpu = private;
	struct time_bench_sync *sync = cpu->sync;
	cpumask_t newmask = CPU_MASK_NONE;

	/* Restrict CPU */
	cpumask_set_cpu(cpu->rec.cpu, &newmask);
	set_cpus_allowed_ptr(current, &newmask);

	/* Synchronize start of concurrency test */
	atomic_inc(&sync->nr_tests_running);
	wait_for_completion(&sync->start_event);

	/* Start benchmark function */
	if (!cpu->bench_func(&cpu->rec, NULL)) {
		pr_err("ERROR: function being timed failed on CPU:%d(%d)\n",
		       cpu->rec.cpu, smp_processor_id());
	} else {
		if (verbose)
			pr_info("SUCCESS: ran on CPU:%d(%d)\n",
				cpu->rec.cpu, smp_processor_id());
	}
	cpu->did_bench_run = true;

	/* End test */
	atomic_dec(&sync->nr_tests_running);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	return 0;
}

void time_bench_print_stats_cpumask(const char *desc,
				    struct time_bench_cpu *cpu_tasks,
				    const struct cpumask *mask)
{
	int cpu;
	int step = 0;
	struct sum {
		uint64_t tsc_cycles;
		int records;
	} sum = {0};

	/* Get stats */
	for_each_cpu(cpu, mask) {
		struct time_bench_cpu *c = &cpu_tasks[cpu];
		struct time_bench_record *rec = &c->rec;

		/* Calculate stats */
		time_bench_calc_stats(rec);

		pr_info("Type:%s CPU(%d) %llu cycles(tsc) %llu.%03llu ns"
		" (step:%d)"
		" - (measurement period time:%llu.%09u sec time_interval:%llu)"
		" - (invoke count:%llu tsc_interval:%llu)\n",
		desc, cpu, rec->tsc_cycles,
		rec->ns_per_call_quotient, rec->ns_per_call_decimal, rec->step,
		rec->time_sec, rec->time_sec_remainder, rec->time_interval,
		rec->invoked_cnt, rec->tsc_interval);

		/* Collect average */
		sum.records++;
		sum.tsc_cycles += rec->tsc_cycles;
		step = rec->step;
	}

	pr_info("Sum Type:%s Average: %llu cycles(tsc) CPUs:%d step:%d\n",
		desc, sum.tsc_cycles / sum.records, sum.records, step);
}

void time_bench_run_concurrent(
		uint32_t loops, int step, const char *desc,
		const struct cpumask *mask, /* Support masking outsome CPUs*/
		struct time_bench_sync *sync,
		struct time_bench_cpu *cpu_tasks,
		int (*func)(struct time_bench_record *record, void *data)
	)
{
	int cpu, running = 0;

	if (verbose) // DEBUG
		pr_warn("%s() Started on CPU:%d)\n",
			__func__, smp_processor_id());

	/* Reset sync conditions */
	atomic_set(&sync->nr_tests_running, 0);
	init_completion(&sync->start_event);

	/* Spawn off jobs on all CPUs */
	for_each_cpu(cpu, mask) {
		struct time_bench_cpu *c = &cpu_tasks[cpu];

		running++;
		c->sync = sync; /* Send sync variable along */

		/* Init benchmark record */
		memset(&c->rec, 0, sizeof(struct time_bench_record));
		c->rec.version_abi = 1;
		c->rec.loops       = loops;
		c->rec.step        = step;
		c->rec.flags       = (TIME_BENCH_LOOP|TIME_BENCH_TSC|
				      TIME_BENCH_WALLCLOCK);
		c->rec.cpu = cpu;
		c->bench_func = func;
		c->task = kthread_run(invoke_test_on_cpu_func, c,
				      "time_bench%d", cpu);
		if (IS_ERR(c->task)) {
			pr_err("%s(): Failed to start test func\n", __func__);
			return;
		}
	}

	/* Wait until all processes are running */
	while (atomic_read(&sync->nr_tests_running) < running) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(10);
	}
	/* Kick off all CPU concurrently on completion event */
	complete_all(&sync->start_event);

	/* Wait for CPUs to finish */
	while (atomic_read(&sync->nr_tests_running)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(10);
	}

	/* Stop the kthreads */
	for_each_cpu(cpu, mask) {
		struct time_bench_cpu *c = &cpu_tasks[cpu];
		kthread_stop(c->task);
	}

	if (verbose) // DEBUG - happens often, finish on another CPU
		pr_warn("%s() Finished on CPU:%d)\n",
			__func__, smp_processor_id());

	time_bench_print_stats_cpumask(desc, cpu_tasks, mask);
}

void noinline run_bench_compare(uint32_t loops)
{
	run_or_return(bit_run_bench_compare);

	/* For comparison */
	time_bench_loop(loops, page_order, "alloc_pages_order_step", NULL,
			time_alloc_pages);
}

void noinline run_bench_parallel_all_cpus(uint32_t loops)
{
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	size_t size;

	run_or_return(bit_run_bench_parallel_all_cpus);

	/* Allocate records for every CPU */
	size = sizeof(*cpu_tasks) * num_possible_cpus();
	cpu_tasks = kzalloc(size, GFP_KERNEL);

	/* Run concurrently */
	time_bench_run_concurrent(loops, page_order, "parallel-test",
				  cpu_online_mask, &sync, cpu_tasks,
				  time_alloc_pages);
	kfree(cpu_tasks);
}

void noinline run_bench_limited_cpus(uint32_t loops, int nr_cpus)
{
	struct time_bench_sync sync;
	struct time_bench_cpu *cpu_tasks;
	struct cpumask my_cpumask;
	int i;

	run_or_return(bit_run_bench_limited_cpus);

	/* Allocate records for CPUs */
	cpu_tasks = kzalloc(sizeof(*cpu_tasks) * nr_cpus, GFP_KERNEL);

	/* Reduce number of CPUs to run on */
	cpumask_clear(&my_cpumask);
	for (i = 0; i < nr_cpus ; i++) {
		cpumask_set_cpu(i, &my_cpumask);
	}
	pr_info("Limit to %d parallel CPUs\n", parallel_cpus);
	time_bench_run_concurrent(loops, page_order, "limited-cpus",
				  &my_cpumask, &sync, cpu_tasks,
				  time_alloc_pages);
	kfree(cpu_tasks);
}

int run_timing_tests(void)
{
	uint32_t loops = 100000;

	run_bench_compare(loops);
	run_bench_parallel_all_cpus(loops);
	run_bench_limited_cpus(loops, parallel_cpus);

	return 0;
}

static int __init page_bench03_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_timing_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(page_bench03_module_init);

static void __exit page_bench03_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(page_bench03_module_exit);

MODULE_DESCRIPTION("Benchmarking page alloactor concurrency");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

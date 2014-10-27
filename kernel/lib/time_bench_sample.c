/*
 * Sample: Benchmarking code execution time inside the kernel
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>

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

static DEFINE_SPINLOCK(my_lock);
static int time_lock_unlock(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		spin_lock(&my_lock);
		loops_cnt++;
		barrier();
		spin_unlock(&my_lock);
		loops_cnt++;
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
		loops_cnt++;
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
		loops_cnt++;
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
		loops_cnt++;
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
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static void noinline measured_function(volatile int *var)
{
	(*var) = 1;
}
static int time_func(
	struct time_bench_record *rec, void *data)
{
	int i, tmp;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		measured_function(&tmp);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

struct func_ptr_ops {
	void (*func)(volatile int *var);
	unsigned int (*func2)(unsigned int count);
};
static struct func_ptr_ops my_func_ptr __read_mostly = {
	.func  = measured_function,
	//.func2 = ring_queue_fake_test,
};
static int time_func_ptr(
	struct time_bench_record *rec, void *data)
{
	int i, tmp;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		my_func_ptr.func(&tmp);
		loops_cnt++;
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}


int run_timing_tests(void)
{
	uint32_t loops = 100000000;

	time_bench_loop(loops*10, 0, "for_loop", NULL, time_bench_for_loop);
	time_bench_loop(loops, 0, "lock_unlock", NULL, time_lock_unlock);
	time_bench_loop(loops, 0, "local_bh", NULL, time_local_bh);
	time_bench_loop(loops, 0, "local_irq", NULL, time_local_irq);
	time_bench_loop(loops, 0, "local_irq_save", NULL, time_local_irq_save);
	time_bench_loop(loops, 0, "preempt", NULL, time_preempt);
	time_bench_loop(loops, 0, "call_func", NULL, time_func);
	time_bench_loop(loops, 0, "call_func_ptr", NULL, time_func_ptr);
	return 0;
}

static int __init time_bench_sample_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

	if (run_timing_tests() < 0) {
		return -ECANCELED;
	}

	return 0;
}
module_init(time_bench_sample_module_init);

static void __exit time_bench_sample_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(time_bench_sample_module_exit);

MODULE_DESCRIPTION("Sample: Benchmarking code execution time in kernel");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

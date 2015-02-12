/*
 * Sample: Benchmarking code execution time inside the kernel
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

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
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_lock_unlock_irqsave(
	struct time_bench_record *rec, void *data)
{
	int i;
	unsigned long flags;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		spin_lock_irqsave(&my_lock, flags);
		loops_cnt++;
		barrier();
		spin_unlock_irqrestore(&my_lock, flags);
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

/* Purpose of this func, is to determine of the combined
 * spin_lock_irqsave() call is more efficient than "manually" irqsave
 * before calling lock.
 */
static int time_irqsave_before_lock(
	struct time_bench_record *rec, void *data)
{
	int i;
	unsigned long flags;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		local_irq_save(flags);
		spin_lock(&my_lock);
		loops_cnt++;
		barrier();
		spin_unlock(&my_lock);
		local_irq_restore(flags);
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

/* How much is there to save when using non-flags save variant of
 * disabling interrupts */
static int time_simple_irq_disable_before_lock(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		local_irq_disable();
		spin_lock(&my_lock);
		loops_cnt++;
		barrier();
		spin_unlock(&my_lock);
		local_irq_enable();
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

static int time_page_alloc(
	struct time_bench_record *rec, void *data)
{
	int i;
	struct page *my_page;
	gfp_t gfp_mask = (GFP_ATOMIC | ___GFP_NORETRY);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		my_page = alloc_page(gfp_mask);
		if (unlikely(my_page == NULL))
			return 0;
		put_page(my_page);
	}
	time_bench_stop(rec, i);
	return i;
}

int run_timing_tests(void)
{
	uint32_t loops = 100000000;

	/* Results listed below for a E5-2695 CPU */

	/*  0.360 ns cost overhead of the for loop */
	time_bench_loop(loops*10, 0, "for_loop", /*  0.360 ns */
			NULL, time_bench_for_loop);

	/* Cost for spin_lock+spin_unlock
	 * 13.946 ns with CONFIG_PREEMPT=n PREEMPT_COUNT=n
	 * 16.449 ns with CONFIG_PREEMPT=n PREEMPT_COUNT=y
	 * 16.449 ns with CONFIG_PREEMPT=y PREEMPT_COUNT=y
	 * 22.177 ns with CONFIG_PREEMPT=y PREEMPT_COUNT=y DEBUG_PREEMPT=y
	 */
	time_bench_loop(loops, 0, "spin_lock_unlock",
			NULL, time_lock_unlock);

	time_bench_loop(loops/2, 0, "spin_lock_unlock_irqsave",
			NULL, time_lock_unlock_irqsave);

	time_bench_loop(loops/2, 0, "irqsave_before_lock",
			NULL, time_irqsave_before_lock);

	time_bench_loop(loops/2, 0, "simple_irq_disable_before_lock",
			NULL, time_simple_irq_disable_before_lock);

	/* Cost for local_bh_{disable,enable}
	 *  7.387 ns with CONFIG_PREEMPT=n PREEMPT_COUNT=n
	 *  7.459 ns with CONFIG_PREEMPT=n PREEMPT_COUNT=y
	 *  7.462 ns with CONFIG_PREEMPT=y PREEMPT_COUNT=y
	 * 21.691 ns with CONFIG_PREEMPT=y PREEMPT_COUNT=y DEBUG_PREEMPT=y
	 */
	time_bench_loop(loops, 0, "local_BH_disable_enable",
			NULL, time_local_bh);

	/*  2.860 ns cost for local_irq_{disable,enable} */
	time_bench_loop(loops, 0, "local_IRQ_disable_enable",
			NULL, time_local_irq);

	/* 14.840 ns cost for local_irq_save()+local_irq_restore() */
	time_bench_loop(loops, 0, "local_irq_save_restore",
			NULL, time_local_irq_save);

	/* Cost for preempt_{disable,enable}:
	 *   0.360 ns with CONFIG_PREEMPT=n PREEMPT_COUNT=n
	 *   4.291 ns with CONFIG_PREEMPT=n PREEMPT_COUNT=y
	 *   4.291 ns with CONFIG_PREEMPT=n PREEMPT_COUNT=y
	 *  12.294 ns with CONFIG_PREEMPT=y PREEMPT_COUNT=y DEBUG_PREEMPT=y
	 */
	time_bench_loop(loops, 0, "preempt_disable_enable",
			NULL, time_preempt);

	/*  2.145 ns cost for a local function call */
	time_bench_loop(loops, 0, "funcion_call_cost",
			NULL, time_func);

	/*  2.503 ns cost for a function pointer invocation */
	time_bench_loop(loops, 0, "func_ptr_call_cost",
			NULL, time_func_ptr);

	/*  Approx 141.488 ns cost for alloc_page()+put_page() */
	time_bench_loop(loops/100, 0, "page_alloc_put",
			NULL, time_page_alloc);
	return 0;
}

static int __init time_bench_sample_module_init(void)
{
	if (verbose)
		pr_info("Loaded\n");

#ifdef CONFIG_DEBUG_PREEMPT
	pr_warn("WARN: CONFIG_DEBUG_PREEMPT is enabled: this affect results\n");
#endif
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

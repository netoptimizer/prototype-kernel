/* slab_test.c
 *
 * Test module for synthetic in kernel slab allocator testing.
 *
 * The test is triggered by loading the module (which will fail).
 *
 * (C) 2009 Linux Foundation <cl@linux-foundation.org>
 */


#include <linux/jiffies.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/timex.h>

#define TEST_COUNT 10000

//#undef CONFIG_SMP /* Hack to disable concurrency tests */

#ifdef CONFIG_SMP
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>

static struct test_struct {
	struct task_struct *task;
	int cpu;
	int size;
	int count;
	void **v;
	void (*test_p1)(struct test_struct *);
	void (*test_p2)(struct test_struct *);
	unsigned long start1;
	unsigned long stop1;
	unsigned long start2;
	unsigned long stop2;
} test[NR_CPUS];

/*
 * Allocate TEST_COUNT objects on cpus > 0 and then all the
 * objects later on cpu 0
 */
static void remote_free_test_p1(struct test_struct *t)
{
	int i;

	/* Perform no allocations on cpu 0 */
	for (i = 0; i < t->count; i++) {
		u8 *p;

		if (smp_processor_id()) {
			p = kmalloc(t->size, GFP_KERNEL);
			/* Use object */
			*p = 17;
		} else
			p = NULL;
		t->v[i] = p;
	}
}

static void remote_free_test_p2(struct test_struct *t)
{
	int i;
	int cpu;

	/* All frees are completed on cpu zero */
	if (smp_processor_id())
		return;

	for_each_online_cpu(cpu)
		for (i = 0; i < t->count; i++) {
			u8 *p = test[cpu].v[i];

			if (!p)
				continue;

			*p = 16;
			kfree(p);
		}
}

/*
 * Allocate TEST_COUNT objects on cpu 0 and free them immediately on the
 * other processors.
 */
static void alloc_n_free_test_p1(struct test_struct *t)
{
	int i;
	int cpu;
	char *p;

	if (smp_processor_id()) {
		/* Consumer */
		for (i = 0; i < t->count / num_online_cpus(); i++) {
			do {
				p = t->v[i];
				if (!p)
					cpu_relax();
				else
					*p = 17;
			} while (!p);
			kfree(p);
			t->v[i] = NULL;
		}
		return;
	}
	/* Producer */
	for (i = 0; i < t->count; i++) {
		for_each_online_cpu(cpu) {
			if (cpu) {
				p = kmalloc(t->size, GFP_KERNEL);
				/* Use object */
				*p = 17;
				test[cpu].v[i] = p;
			}
		}
	}
}

/*
 * Allocate TEST_COUNT objects and later free them all again
 */
static void kmalloc_alloc_then_free_test_p1(struct test_struct *t)
{
	int i;

	for (i = 0; i < t->count; i++) {
		u8 *p = kmalloc(t->size, GFP_KERNEL);

		*p = 14;
		t->v[i] = p;
	}
}

static void kmalloc_alloc_then_free_test_p2(struct test_struct *t)
{
	int i;

	for (i = 0; i < t->count; i++) {
		u8 *p = t->v[i];

		*p = 13;
		kfree(p);
	}
}

/*
 * Allocate TEST_COUNT objects. Free them immediately.
 */
static void kmalloc_alloc_free_test_p1(struct test_struct *t)
{
	int i;

	for (i = 0; i < TEST_COUNT; i++) {
		u8 *p = kmalloc(t->size, GFP_KERNEL);

		*p = 12;
		kfree(p);
	}
}

static atomic_t tests_running;
static atomic_t phase1_complete;
static DECLARE_COMPLETION(completion1);
static DECLARE_COMPLETION(completion2);
static int started;

static int test_func(void *private)
{
	struct test_struct *t = private;
	cpumask_t newmask = CPU_MASK_NONE;

	cpumask_set_cpu(t->cpu, &newmask);
	set_cpus_allowed_ptr(current, &newmask);
	t->v = kzalloc(t->count * sizeof(void *), GFP_KERNEL);

	atomic_inc(&tests_running);
	wait_for_completion(&completion1);
	t->start1 = get_cycles();
	t->test_p1(t);
	t->stop1 = get_cycles();
	atomic_inc(&phase1_complete);
	wait_for_completion(&completion2);
	t->start2 = get_cycles();
	if (t->test_p2)
		t->test_p2(t);
	t->stop2 = get_cycles();
	kfree(t->v);
	atomic_dec(&tests_running);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	return 0;
}

static void do_concurrent_test(void (*p1)(struct test_struct *),
		void (*p2)(struct test_struct *),
		int size, const char *name)
{
	int cpu;
	unsigned long time1 = 0;
	unsigned long time2 = 0;
	unsigned long sum1 = 0;
	unsigned long sum2 = 0;

	atomic_set(&tests_running, 0);
	atomic_set(&phase1_complete, 0);
	started = 0;
	init_completion(&completion1);
	init_completion(&completion2);

	for_each_online_cpu(cpu) {
		struct test_struct *t = &test[cpu];

		t->cpu = cpu;
		t->count = TEST_COUNT;
		t->test_p1 = p1;
		t->test_p2 = p2;
		t->size = size;
		t->task = kthread_run(test_func, t, "test%d", cpu);
		if (IS_ERR(t->task)) {
			printk("Failed to start test func\n");
			return;
		}
	}

	/* Wait till all processes are running */
	while (atomic_read(&tests_running) < num_online_cpus()) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(10);
	}
	complete_all(&completion1);

	/* Wait till all processes have completed phase 1 */
	while (atomic_read(&phase1_complete) < num_online_cpus()) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(10);
	}
	complete_all(&completion2);

	while (atomic_read(&tests_running)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(10);
	}

	for_each_online_cpu(cpu)
		kthread_stop(test[cpu].task);

	printk(KERN_ALERT "%s(%d):", name, size);
	for_each_online_cpu(cpu) {
		struct test_struct *t = &test[cpu];

		time1 = t->stop1 - t->start1;
		time2 = t->stop2 - t->start2;
		sum1 += time1;
		sum2 += time2;
		printk(" %d=%lu", cpu, time1 / TEST_COUNT);
		if (p2)
			printk("/%lu", time2 / TEST_COUNT);
	}
	printk(" Average=%lu", sum1 / num_online_cpus() / TEST_COUNT);
	if (p2)
		printk("/%lu", sum2 / num_online_cpus() / TEST_COUNT);
	printk("\n");
	schedule_timeout(200);
}
#endif /* CONFIG_SMP */

static int slab_test_init(void)
{
	void **v = kmalloc(TEST_COUNT * sizeof(void *), GFP_KERNEL);
	unsigned int i;
	cycles_t time1, time2, time;
	int rem;
	int size;

	printk(KERN_ALERT "test init\n");

	printk(KERN_ALERT "Single thread testing\n");
	printk(KERN_ALERT "=====================\n");
	printk(KERN_ALERT "1. Kmalloc: Repeatedly allocate then free test\n");
	for (size = 8; size <= PAGE_SIZE << 2; size <<= 1) {
		time1 = get_cycles();
		for (i = 0; i < TEST_COUNT; i++) {
			u8 *p = kmalloc(size, GFP_KERNEL);

			*p = 22;
			v[i] = p;
		}
		time2 = get_cycles();
		time = time2 - time1;

		printk(KERN_ALERT "%i times kmalloc(%d) ", i, size);
		time = div_u64_rem(time, TEST_COUNT, &rem);
		printk("-> %llu cycles ", time);

		time1 = get_cycles();
		for (i = 0; i < TEST_COUNT; i++) {
			u8 *p = v[i];

			*p = 23;
			kfree(p);
		}
		time2 = get_cycles();
		time = time2 - time1;

		printk("kfree ");
		time = div_u64_rem(time, TEST_COUNT, &rem);
		printk("-> %llu cycles\n", time);
	}

	printk(KERN_ALERT "2. Kmalloc: alloc/free test\n");
	for (size = 8; size <= PAGE_SIZE << 2; size <<= 1) {
		time1 = get_cycles();
		for (i = 0; i < TEST_COUNT; i++) {
			u8 *p = kmalloc(size, GFP_KERNEL);

			kfree(p);
		}
		time2 = get_cycles();
		time = time2 - time1;

		printk(KERN_ALERT "%i times kmalloc(%d)/kfree ", i, size);
		time = div_u64_rem(time, TEST_COUNT, &rem);
		printk("-> %llu cycles\n", time);
	}
	kfree(v);
#ifdef CONFIG_SMP
	printk(KERN_INFO "Concurrent allocs\n");
	printk(KERN_INFO "=================\n");
	for (i = 3; i <= PAGE_SHIFT; i++) {
		do_concurrent_test(kmalloc_alloc_then_free_test_p1,
			kmalloc_alloc_then_free_test_p2,
			1 << i, "Kmalloc N*alloc N*free");
	}
	for (i = 3; i <= PAGE_SHIFT; i++) {
		do_concurrent_test(kmalloc_alloc_free_test_p1, NULL,
			1 << i, "Kmalloc N*(alloc free)");
	}

	printk(KERN_INFO "Remote free test\n");
	printk(KERN_INFO "================\n");
	for (i = 3; i <= PAGE_SHIFT; i++) {
		do_concurrent_test(remote_free_test_p1,
				remote_free_test_p2,
			1 << i, "N*remote free");
	}

	printk(KERN_INFO "1 alloc N free test\n");
	printk(KERN_INFO "===================\n");
	for (i = 3; i <= PAGE_SHIFT; i++) {
		do_concurrent_test(alloc_n_free_test_p1,
				NULL,
			1 << i, "1 alloc N free");
	}

#endif
	return -EAGAIN; /* Fail will directly unload the module */
}

static void slab_test_exit(void)
{
	printk(KERN_ALERT "test exit\n");
}

module_init(slab_test_init)
module_exit(slab_test_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christoph Lameter and Mathieu Desnoyers");
MODULE_DESCRIPTION("SLAB test");


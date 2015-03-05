/*
 * Benchmarking code execution time inside the kernel
 *
 * Testing memset zero clearing, specifically the effect of the
 * assembler instruction "rep stos" that get generated for x86_64.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* Trying to figure out why the REP STOS memset version is slower than
 * a memset that does not use that functionality....
 *
 * The REP operations has an associated cost of preserving register
 * state in case of interrupts.  That might explain the startup cost.
 *
 * Quote from: Intel Arch Software Devel Manual (page 1402)
 *   A repeating string operation can be suspended by an exception or
 *   interrupt. When this happens, the state of the registers is
 *   preserved to allow the string operation to be resumed upon a
 *   return from the exception or interrupt handler. The source and
 *   destination registers point to the next string elements to be
 *   operated on, the EIP register points to the string instruction,
 *   and the ECX register has the value it held following the last
 *   successful iteration of the instruction. This mechanism allows
 *   long string operations to proceed without affecting the interrupt
 *   response time of the system.
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

static int verbose=1;

#define GLOBAL_BUF_SIZE 8192
static char global_buf[GLOBAL_BUF_SIZE];

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

/* Looks like memset 32 does not translate into a repeated store */
static int time_memset_32(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 32
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}


static int time_memset_64(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 64
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

static int time_memset_128(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 128
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}


/* (currently) 200 is equiv to SKB clear size on 64bit */
static int time_memset_200(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 200
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

static int time_memset_1024(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 1024
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}


static int time_memset_4096(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 4096
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

static int time_memset_8192(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 8192
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}


/*
noinline static void callmemset(void *buf, int size)
{
	memset(buf, 0, size);
}
*/

static int time_memset_variable_step(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;
	int size = rec->step;

	if (size > GLOBAL_BUF_SIZE)
		return 0;

	printk(KERN_INFO "TEST: size:%d\n", size);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier(); /* avoid compiler tricks */
		memset(&global_buf, 0, size);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

int run_timing_tests(void)
{
	uint32_t loops = 100000000;

	/*  0.360 ns cost overhead of the for loop */
	time_bench_loop(loops*10, 0, "for_loop", /*  0.360 ns */
			NULL, time_bench_for_loop);

	time_bench_loop(loops, 0, "memset_32",
			NULL, time_memset_32);

	time_bench_loop(loops, 32, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops, 0, "memset_64",
			NULL, time_memset_64);

	time_bench_loop(loops, 64, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops, 0, "memset_128",
			NULL, time_memset_128);

	time_bench_loop(loops, 128, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops, 0, "memset_200",
			NULL, time_memset_200);

	time_bench_loop(loops, 200, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops/10, 0, "memset_1024",
			NULL, time_memset_1024);

	time_bench_loop(loops/10, 1024, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops/100, 0, "memset_4096",
			NULL, time_memset_4096);

	time_bench_loop(loops/100, 4096, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops/200, 0, "memset_8192",
			NULL, time_memset_8192);

	time_bench_loop(loops/200, 8192, "memset_variable_step",
			NULL,   time_memset_variable_step);

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

MODULE_DESCRIPTION("Benchmark: memset and rep stos");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");

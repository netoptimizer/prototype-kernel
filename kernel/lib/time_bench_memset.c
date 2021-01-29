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
#include <asm/fpu/api.h>

#include <linux/skbuff.h>

// #include <asm/mmx.h> // mmx_clear_page -> fast_clear_page

static int verbose=1;

extern bool irq_fpu_usable(void);
extern void kernel_fpu_begin(void);
extern void kernel_fpu_end(void);

#define GLOBAL_BUF_SIZE 8192
static char global_buf[GLOBAL_BUF_SIZE];

#define YMM_BYTES		(XSAVE_YMM_SIZE / BITS_PER_BYTE)
#define BYTES_TO_YMM(x)		((x) / YMM_BYTES)
#define TIME_MEMSET_AVX2_ZERO(reg)					\
	asm volatile("vpxor %ymm" #reg ", %ymm" #reg ", %ymm" #reg)
#define TIME_MEMSET_AVX2_STORE(loc, reg)				\
	asm volatile("vmovdqa %%ymm" #reg ", %0" : "=m" (loc))

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
		barrier();
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
		barrier();
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
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

static int time_memset_skb_tail(
	struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	preempt_disable();
	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		memset(&global_buf, 0, offsetof(struct sk_buff, tail));
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	preempt_enable();
	pr_info("SKB: offsetof-tail:%lu\n", offsetof(struct sk_buff, tail));

	return loops_cnt;
}

static int time_memset_skb_tail_roundup(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE roundup(offsetof(struct sk_buff, tail), SMP_CACHE_BYTES)

	int i;
	uint64_t loops_cnt = 0;

	preempt_disable();
	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	preempt_enable();
	pr_info("SKB: ROUNDUP(offsetof-tail: %lu)\n", CONST_CLEAR_SIZE);

	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

static int time_memset_199(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 199
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}


/* Do something aligned to 64*/
static int time_memset_192(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 192
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}


/* Do something not aligned */
static int time_memset_201(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 201
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

/* Do something not 8 bytes aligned */
static int time_memset_204(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 204
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}


/* (currently) 200 is equiv to SKB clear size on 64bit
 * - this does depend on some CONFIG defines
 */
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
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

/* 208 / 16 bytes = 13 ... which should be more optimal for REP STOS */
static int time_memset_208(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 208
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

/* 256 / 64 bytes = 4 ... which should be more optimal for REP STOS */
static int time_memset_256(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 256
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

static int time_memset_512(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 512
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

static int time_memset_768(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 768
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
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
		barrier();
		memset(&global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

static int time_memset_2048(
	struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 2048
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
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
		barrier();
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
		barrier();
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

static inline
void mem_zero_crazy_loop_unroll2(void *ptr, const unsigned int qword)
//void mem_zero_crazy_loop_unroll2(void *ptr, const unsigned int bytes)
{
/* WARNING: Setting VALUE to zero result in different assembler code,
 * with slightly less performance, this need more investigation!
 */
//#define VALUE      0x4141414141414141
//#define VALUE_BYTE 0x41
//#define VALIDATE_CLEARING 1
#define VALUE      0x0000000000000000
#define VALUE_BYTE 0x00

	/* Clear up to the next quad word */
	//const unsigned int qword = DIV_ROUND_UP(bytes, 8);
	int i;
	int iterations = qword & ~3UL;
	unsigned long *data = ptr;

	for (i = 0; i < iterations; i+=4) {
/*
		data[i+3] = VALUE;
		data[i+2] = VALUE;
		data[i+1] = VALUE;
		data[i]   = VALUE;
*/
		/* Const memset 32 is extremely fast! */
		memset(&data[i], VALUE_BYTE, 32);
	}
	/* Remainder qword handling */
	switch(qword & 0x3) {
	case 3:
		data[i+2] = VALUE;
	case 2:
		data[i+1] = VALUE;
	case 1:
		data[i]   = VALUE;
	}
}

static int time_mem_zero_hacks(
	struct time_bench_record *rec, void *data)
{
	int i, bytes_rounded_up;
	uint64_t loops_cnt = 0;
	int size = rec->step;
	size = DIV_ROUND_UP(size, 8); // convert to qwords
	bytes_rounded_up = size * 8;

	if ((bytes_rounded_up) > GLOBAL_BUF_SIZE)
		return 0;

	printk(KERN_INFO "Rounded %d up to size:%d\n",
	       rec->step, bytes_rounded_up);

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier(); /* avoid compiler tricks */
		mem_zero_crazy_loop_unroll2(&global_buf, size);
		barrier();
	}
	time_bench_stop(rec, loops_cnt);

#ifdef VALIDATE_CLEARING
	/* Better validate the clearing */
	for (i = 0; i < GLOBAL_BUF_SIZE; i++) {
		int n = 0;

		if (global_buf[i] == VALUE_BYTE) {
			n++;
		} else {
			printk(KERN_INFO "Number of VALUE_BYTE found: %d\n", n);
			return loops_cnt;
		}
	}
#endif
	return loops_cnt;
}

static void fast_clear_mmx_256(void *page)
{
	int i;

	kernel_fpu_begin();

	__asm__ __volatile__ (
		"  pxor %%mm0, %%mm0\n" : :
	);

	for (i = 0; i < 256/128; i++) {
		__asm__ __volatile__ (
		"  movq %%mm0, (%0)\n"
		"  movq %%mm0, 8(%0)\n"
		"  movq %%mm0, 16(%0)\n"
		"  movq %%mm0, 24(%0)\n"
		"  movq %%mm0, 32(%0)\n"
		"  movq %%mm0, 40(%0)\n"
		"  movq %%mm0, 48(%0)\n"
		"  movq %%mm0, 56(%0)\n"
		"  movq %%mm0, 64(%0)\n"
		"  movq %%mm0, 72(%0)\n"
		"  movq %%mm0, 80(%0)\n"
		"  movq %%mm0, 88(%0)\n"
		"  movq %%mm0, 96(%0)\n"
		"  movq %%mm0, 104(%0)\n"
		"  movq %%mm0, 112(%0)\n"
		"  movq %%mm0, 120(%0)\n"
			: : "r" (page) : "memory");
		page += 128;
	}

	kernel_fpu_end();
}

static int time_memset_mmx_256(struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 256
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);

	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		if (likely(irq_fpu_usable()))
			fast_clear_mmx_256(global_buf);
		else
			memset(global_buf, 0, CONST_CLEAR_SIZE);
		barrier();
	}

	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}

static int time_memset_avx2_256(struct time_bench_record *rec, void *data)
{
#define CONST_CLEAR_SIZE 256
	int i, j;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);

	for (i = 0; i < rec->loops; i++) {
		kernel_fpu_begin();
		TIME_MEMSET_AVX2_ZERO(0);

		loops_cnt++;
		barrier();
		for (j = 0; j < BYTES_TO_YMM(CONST_CLEAR_SIZE); j++)
			TIME_MEMSET_AVX2_STORE(global_buf[YMM_BYTES * j], 0);
		barrier();

		kernel_fpu_end();
	}

	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
#undef  CONST_CLEAR_SIZE
}


static void fast_clear_movq_192(void *page)
{
	int i;

	for (i = 0; i < 192/64; i++) {
		__asm__ __volatile__(
		"  movq $0, (%0)\n"
		"  movq $0, 8(%0)\n"
		"  movq $0, 16(%0)\n"
		"  movq $0, 24(%0)\n"
		"  movq $0, 32(%0)\n"
		"  movq $0, 40(%0)\n"
		"  movq $0, 48(%0)\n"
		"  movq $0, 56(%0)\n"
		: : "r" (page) : "memory");
		page += 64;
	}
}

static void fast_clear_movq_256(void *page)
{
	int i;

	for (i = 0; i < 256/64; i++) {
		__asm__ __volatile__(
		"  movq $0, (%0)\n"
		"  movq $0, 8(%0)\n"
		"  movq $0, 16(%0)\n"
		"  movq $0, 24(%0)\n"
		"  movq $0, 32(%0)\n"
		"  movq $0, 40(%0)\n"
		"  movq $0, 48(%0)\n"
		"  movq $0, 56(%0)\n"
		: : "r" (page) : "memory");
		page += 64;
	}
}

static int time_memset_movq_192(struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);

	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		fast_clear_movq_192(global_buf);
		barrier();
	}

	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

static int time_memset_movq_256(struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);

	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		fast_clear_movq_256(global_buf);
		barrier();
	}

	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

inline static void alternative_clear_movq_256(void *page)
{
	int i;

	for (i = 0; i < 256/128; i++) {
		__asm__ __volatile__(
			"  movq $0, (%0)\n"   //A
			"  movq $0, 8(%0)\n"  //A
		"  movq $0, 64(%0)\n"
		"  movq $0, 72(%0)\n"
			"  movq $0, 16(%0)\n" //A
			"  movq $0, 24(%0)\n" //A
		"  movq $0, 80(%0)\n"
		"  movq $0, 88(%0)\n"
			"  movq $0, 32(%0)\n" //A
			"  movq $0, 40(%0)\n" //A
		"  movq $0, 96(%0)\n"
		"  movq $0, 104(%0)\n"
			"  movq $0, 48(%0)\n" //A
			"  movq $0, 56(%0)\n" //A
		"  movq $0, 112(%0)\n"
		"  movq $0, 120(%0)\n"
		: : "r" (page) : "memory");
		page += 128;
	}

}

static int time_alternative_movq_256(struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	time_bench_start(rec);

	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		alternative_clear_movq_256(global_buf);
		barrier();
	}

	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

/* Copied from arch/x86/lib/mmx_32.c:
 *	Generic MMX implementation without K7 specific streaming
 */
static void fast_clear_page(void *page)
{
	int i;

	kernel_fpu_begin();

	__asm__ __volatile__ (
		"  pxor %%mm0, %%mm0\n" : :
	);

	for (i = 0; i < 4096/128; i++) {
		__asm__ __volatile__ (
		"  movq %%mm0, (%0)\n"
		"  movq %%mm0, 8(%0)\n"
		"  movq %%mm0, 16(%0)\n"
		"  movq %%mm0, 24(%0)\n"
		"  movq %%mm0, 32(%0)\n"
		"  movq %%mm0, 40(%0)\n"
		"  movq %%mm0, 48(%0)\n"
		"  movq %%mm0, 56(%0)\n"
		"  movq %%mm0, 64(%0)\n"
		"  movq %%mm0, 72(%0)\n"
		"  movq %%mm0, 80(%0)\n"
		"  movq %%mm0, 88(%0)\n"
		"  movq %%mm0, 96(%0)\n"
		"  movq %%mm0, 104(%0)\n"
		"  movq %%mm0, 112(%0)\n"
		"  movq %%mm0, 120(%0)\n"
			: : "r" (page) : "memory");
		page += 128;
	}

	kernel_fpu_end();
}

static int time_fast_clear_page(struct time_bench_record *rec, void *data)
{
	int i;
	uint64_t loops_cnt = 0;

	if (!irq_fpu_usable())
		return 0;

	time_bench_start(rec);

	for (i = 0; i < rec->loops; i++) {
		loops_cnt++;
		barrier();
		// mmx_clear_page(global_buf); // #include <asm/mmx.h>
		fast_clear_page(global_buf);
		barrier();
	}

	time_bench_stop(rec, loops_cnt);
	return loops_cnt;
}

int run_timing_tests(void)
{
	uint32_t loops = 10000000;

	/*  0.360 ns cost overhead of the for loop */
	time_bench_loop(loops*10, 0, "for_loop", /*  0.360 ns */
			NULL, time_bench_for_loop);

	time_bench_loop(loops, 16, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops, 0, "memset_32",
			NULL, time_memset_32);

	time_bench_loop(loops, 32, "mem_zero_hacks",
			NULL,  time_mem_zero_hacks);
	time_bench_loop(loops, 32, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops, 64, "mem_zero_hacks",
			NULL,  time_mem_zero_hacks);
	time_bench_loop(loops, 0, "memset_64",
			NULL, time_memset_64);
	time_bench_loop(loops, 64, "memset_variable_step",
			NULL,  time_memset_variable_step);

	time_bench_loop(loops, 128, "mem_zero_hacks",
			NULL,  time_mem_zero_hacks);
	time_bench_loop(loops, 0, "memset_128",
			NULL, time_memset_128);
	time_bench_loop(loops, 128, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops, 192, "mem_zero_hacks",
			NULL,   time_mem_zero_hacks);
	time_bench_loop(loops, 0, "memset_192", /* <= 3 * 64 */
			NULL, time_memset_192);
	time_bench_loop(loops, 192, "memset_variable_step",
			NULL,   time_memset_variable_step);
	time_bench_loop(loops, 0, "memset_MOVQ_192",
			NULL, time_memset_movq_192);

	time_bench_loop(loops, 0, "memset_skb_tail",
			NULL, time_memset_skb_tail);
	time_bench_loop(loops, 0, "memset_skb_tail_roundup",
			NULL, time_memset_skb_tail_roundup);

	time_bench_loop(loops, 0, "memset_199",
			NULL, time_memset_199);
	time_bench_loop(loops, 0, "memset_201",
			NULL, time_memset_201);
	time_bench_loop(loops, 0, "memset_204",
			NULL, time_memset_204);

	time_bench_loop(loops, 200, "mem_zero_hacks",
			NULL,  time_mem_zero_hacks);
	time_bench_loop(loops, 0, "memset_200",
			NULL, time_memset_200);
	time_bench_loop(loops, 200, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops, 208, "mem_zero_hacks",
			NULL,   time_mem_zero_hacks);
	time_bench_loop(loops, 0, "memset_208",
			NULL, time_memset_208);
	time_bench_loop(loops, 208, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops, 256, "mem_zero_hacks",
			NULL,   time_mem_zero_hacks);
	time_bench_loop(loops, 0, "memset_256",
			NULL, time_memset_256);
	time_bench_loop(loops, 256, "memset_variable_step",
			NULL,   time_memset_variable_step);
	time_bench_loop(loops, 0, "memset_MMX_256",
			NULL, time_memset_mmx_256);
	time_bench_loop(loops, 0, "memset_AVX2_256",
			NULL, time_memset_avx2_256);
	time_bench_loop(loops, 0, "memset_MOVQ_256",
			NULL, time_memset_movq_256);
	time_bench_loop(loops, 0, "alternative_MOVQ_256",
			NULL, time_alternative_movq_256);

	time_bench_loop(loops, 512, "mem_zero_hacks",
			NULL,   time_mem_zero_hacks);
	time_bench_loop(loops, 0, "memset_512",
			NULL, time_memset_512);
	time_bench_loop(loops, 512, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops, 768, "mem_zero_hacks",
			NULL,   time_mem_zero_hacks);
	time_bench_loop(loops, 0, "memset_768",
			NULL, time_memset_768);
	time_bench_loop(loops, 768, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops/10, 1024, "mem_zero_hacks",
			NULL,   time_mem_zero_hacks);
	time_bench_loop(loops/10, 0, "memset_1024",
			NULL, time_memset_1024);
	time_bench_loop(loops/10, 1024, "memset_variable_step",
			NULL,   time_memset_variable_step);

	time_bench_loop(loops/10, 1024+256, "memset_variable_step",
			NULL,   time_memset_variable_step);
	time_bench_loop(loops/10, 1024+512, "memset_variable_step",
			NULL,   time_memset_variable_step);


	time_bench_loop(loops/10, 2048, "mem_zero_hacks",
			NULL,   time_mem_zero_hacks);
	time_bench_loop(loops/10, 0, "memset_2048",
			NULL, time_memset_2048);
	time_bench_loop(loops/10, 2048, "memset_variable_step",
			NULL,   time_memset_variable_step);


	time_bench_loop(loops/100, 0, "memset_4096",
			NULL, time_memset_4096);
	time_bench_loop(loops/100, 4096, "memset_variable_step",
			NULL,   time_memset_variable_step);
	time_bench_loop(loops/100, 4096, "fast_clear_page",
			NULL,   time_fast_clear_page);

	time_bench_loop(loops/200, 0, "memset_8192",
			NULL, time_memset_8192);
	time_bench_loop(loops/200, 8192, "memset_variable_step",
			NULL,   time_memset_variable_step);

	return 0;
}

static int __init time_bench_sample_module_init(void)
{
	if (verbose)
		pr_info("Loaded: fpu_usable %d\n", irq_fpu_usable());

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

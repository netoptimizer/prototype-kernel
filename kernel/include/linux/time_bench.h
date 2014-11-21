/*
 * Benchmarking code execution time inside the kernel
 *
 * Copyright (C) 2014, Red Hat, Inc., Jesper Dangaard Brouer
 *  for licensing details see kernel-base/COPYING
 */
#ifndef _LINUX_TIME_BENCH_H
#define _LINUX_TIME_BENCH_H

struct time_bench_record
{
	uint32_t version_abi;
	uint32_t loops;		/* Requested loop invocations */
	uint32_t step;		/* option for e.g. bulk invocations */

	uint32_t flags; 	/* Measurements types enabled */
#define TIME_BENCH_LOOP		(1<<0)
#define TIME_BENCH_TSC		(1<<1)
#define TIME_BENCH_WALLCLOCK	(1<<2)
#define TIME_BENCH_PMU		(1<<3)

	uint32_t cpu; //FIXME USE THIS

	/* Records */
	uint64_t invoked_cnt; 	/* Returned actual invocations */
	uint64_t tsc_start;
	uint64_t tsc_stop;
	struct timespec ts_start;
	struct timespec ts_stop;
	/** PMU counters for instruction and cycles
	 * instructions counter including pipelined instructions */
	uint64_t pmc_inst_start;
	uint64_t pmc_inst_stop;
	/* CPU unhalted clock counter */
	uint64_t pmc_clk_start;
	uint64_t pmc_clk_stop;

	/* Result records */
	uint64_t tsc_interval;
	uint64_t time_start, time_stop, time_interval; /* in nanosec */
	uint64_t pmc_inst, pmc_clk;

	/* Derived result records */
	uint64_t tsc_cycles; // +decimal?
	uint64_t ns_per_call_quotient, ns_per_call_decimal;
	uint64_t time_sec;
	uint32_t time_sec_remainder;
	uint64_t pmc_ipc_quotient, pmc_ipc_decimal; /* inst per cycle */
};

/** TSC (Time-Stamp Counter) based **
 * Recommend reading, to understand details of reading TSC accurately:
 *  Intel Doc #324264, "How to Benchmark Code Execution Times on Intel"
 *
 * Consider getting exclusive ownership of CPU by using:
 *   unsigned long flags;
 *   preempt_disable();
 *   raw_local_irq_save(flags);
 *   _your_code_
 *   raw_local_irq_restore(flags);
 *   preempt_enable();
 *
 * Clobbered registers: "%rax", "%rbx", "%rcx", "%rdx"
 *  RDTSC only change "%rax" and "%rdx" but
 *  CPUID clears the high 32-bits of all (rax/rbx/rcx/rdx)
 */
static __always_inline uint64_t tsc_start_clock(void) {
	/* See: Intel Doc #324264 */
	unsigned hi, lo;
	asm volatile (
		"CPUID\n\t"
		"RDTSC\n\t"
		"mov %%edx, %0\n\t"
		"mov %%eax, %1\n\t": "=r" (hi), "=r" (lo)::
		"%rax", "%rbx", "%rcx", "%rdx");
	//FIXME: on 32bit use clobbered %eax + %edx
	return ((uint64_t)lo) | (((uint64_t)hi) << 32);
}

static __always_inline uint64_t tsc_stop_clock(void) {
	/* See: Intel Doc #324264 */
	unsigned hi, lo;
	asm volatile(
		"RDTSCP\n\t"
		"mov %%edx, %0\n\t"
		"mov %%eax, %1\n\t"
		"CPUID\n\t": "=r" (hi), "=r" (lo)::
		"%rax", "%rbx", "%rcx", "%rdx");
	return ((uint64_t)lo) | (((uint64_t)hi) << 32);
}

/* Notes for RDTSC and RDTSCP
 *
 * Hannes found out that __builtin_ia32_rdtsc and
 * __builtin_ia32_rdtscp are undocumented available in gcc, so there
 * is no need to write inline assembler functions for them any more.
 *
 *  unsigned long long __builtin_ia32_rdtscp(unsigned int *foo);
 *   (where foo is set to: numa_node << 12 | cpu)
 *    and
 *  unsigned long long __builtin_ia32_rdtsc(void);
 *
 * Above we combine the calls with CPUID, thus I don't see how this is
 * directly appreciable.
 */

/*
inline uint64_t rdtsc(void)
{
	uint32_t low, high;
	asm volatile("rdtsc" : "=a" (low), "=d" (high));
	return low  | (((uint64_t )high ) << 32);
}
*/

/** Wall-clock based **
 *
 * use: getnstimeofday()
 *  getnstimeofday(&rec->ts_start);
 *  getnstimeofday(&rec->ts_stop);
 */


/** PMU (Performance Monitor Unit) based **
 *
 * Needed for calculating: Instructions Per Cycle (IPC)
 * - The IPC number tell how efficient the CPU pipelining were
 */
//lookup: perf_event_create_kernel_counter()

bool time_bench_PMU_config(bool enable);

/* Raw reading via rdpmc() using fixed counters
 *
 * From: https://github.com/andikleen/simple-pmu
 */
enum {
	FIXED_SELECT = (1U << 30), /* == 0x40000000 */
	FIXED_INST_RETIRED_ANY      = 0,
	FIXED_CPU_CLK_UNHALTED_CORE = 1,
	FIXED_CPU_CLK_UNHALTED_REF  = 2,
};

static __always_inline unsigned long long p_rdpmc(unsigned in)
{
	unsigned d, a;

	asm volatile("rdpmc" : "=d" (d), "=a" (a) : "c" (in) : "memory");
	return ((unsigned long long)d << 32) | a;
}

/* These PMU counter needs to be enabled, but I don't have the
 * configure code implemented.  My current hack is running:
 *  sudo perf stat -e cycles:k -e instructions:k insmod lib/ring_queue_test.ko
 */
/* Reading all pipelined instruction */
static __always_inline unsigned long long pmc_inst(void)
{
	return p_rdpmc(FIXED_SELECT|FIXED_INST_RETIRED_ANY);
}

/* Reading CPU clock cycles */
static __always_inline unsigned long long pmc_clk(void)
{
	return p_rdpmc(FIXED_SELECT|FIXED_CPU_CLK_UNHALTED_CORE);
}

/* Raw reading via MSR rdmsr() is likely wrong
 * FIXME: How can I know which raw MSR registers are conf for what?
 */
#define MSR_IA32_PCM0 0x400000C1 /* PERFCTR0 */
#define MSR_IA32_PCM1 0x400000C2 /* PERFCTR1 */
#define MSR_IA32_PCM2 0x400000C3
inline uint64_t msr_inst(unsigned long long *msr_result)
{
	return rdmsrl_safe(MSR_IA32_PCM0, msr_result);
}


/** Generic functions **
 */
bool time_bench_loop(uint32_t loops, int step, char *txt, void *data,
		     int (*func)(struct time_bench_record *rec, void *data)
	);
bool time_bench_calc_stats(struct time_bench_record *rec);

//FIXME: use rec->flags to select measurement, should be MACRO
static __always_inline void
time_bench_start(struct time_bench_record *rec) {
	getnstimeofday(&rec->ts_start);
	if (rec->flags & TIME_BENCH_PMU) {
		rec->pmc_inst_start = pmc_inst();
		rec->pmc_clk_start  = pmc_clk();
	}
	rec->tsc_start = tsc_start_clock();
}

static __always_inline void
time_bench_stop(struct time_bench_record *rec, uint64_t invoked_cnt) {
	rec->tsc_stop = tsc_stop_clock();
	if (rec->flags & TIME_BENCH_PMU) {
		rec->pmc_inst_stop = pmc_inst();
		rec->pmc_clk_stop  = pmc_clk();
	}
	getnstimeofday(&rec->ts_stop);
	rec->invoked_cnt = invoked_cnt;
}


#endif /* _LINUX_TIME_BENCH_H */

#include <arch/amd64/cpu.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/gdt.h>
#include <arch/amd64/apic.h>
#include <arch/amd64/hpet.h>
#include <arch/amd64/smp.h>
#include <arch/amd64/debug.h>

#include <core/debug.h>

#include <fayt/sched.h>

uint64_t HIGH_VMA = 0xffff800000000000;

extern void syscall_main(void);

struct cpuid_state cpuid(size_t leaf, size_t subleaf)
{
	struct cpuid_state ret = { .leaf = leaf, .subleaf = subleaf };

	uint64_t cpuid_max;
	__asm__ volatile("cpuid"
					 : "=a"(cpuid_max)
					 : "a"(leaf & 0x80000000)
					 : "rbx", "rcx", "rdx");

	if (leaf > cpuid_max)
		return ret;

	__asm__ volatile("cpuid"
					 : "=a"(ret.rax), "=b"(ret.rbx), "=c"(ret.rcx),
					   "=d"(ret.rdx)
					 : "a"(leaf), "c"(subleaf));

	return ret;
}

struct timer invariant_tsc;

void amd64_tsc_calibrate(void)
{
	struct cpuid_state cpuid_state = cpuid(1, 0);
	if ((cpuid_state.rdx & (1 << 4)) == 0)
		panic("dufay: cpuid: tsc/rdtsc unsupported");

	cpuid_state = cpuid(0x80000007, 0);
	if ((cpuid_state.rdx & (1 << 8)) == 0)
		panic("dufay: cpuid: tsc-invariant unsupported\n");

	uint64_t a = rdtsc();
	hpet_msleep(50);
	uint64_t b = rdtsc();

	uint64_t freq = ((b - a) * 1000000000) / 50000000;

	invariant_tsc = (struct timer){ .source = TIME_SOURCE_INVARIANT_TSC,
									.freq = freq,
									.read = invariant_tsc_read };
}

void amd64_system_init(void)
{
	struct cpuid_state cpuid_state = cpuid(1, 0);
	if ((cpuid_state.rdx & (1 << 25)) == 0)
		panic("dufay: cpuid: sse unsupported");

	cpuid_state = cpuid(0x80000001, 0);
	if ((cpuid_state.rdx & (1 << 24)) == 0)
		panic("dufay: cpuid: fxsave/fxrstor unsupported\n");

	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | (1 << 0) | (1 << 11)); // set SCE and NX
	wrmsr(MSR_STAR, 0x33ull << 48 | 0x28ull << 32);
	wrmsr(MSR_LSTAR, (uintptr_t)syscall_main);
	wrmsr(MSR_SFMASK, ~(uint32_t)2);

	uint64_t cr0;
	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

	cr0 &= ~(1 << 2); // disable x87 emulation
	cr0 |= (1 << 1); // enables sse;

	__asm__ volatile("mov %0, %%cr0" ::"r"(cr0));

	uint64_t cr4;
	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

	cr4 &= ~(1 << 2);
	cr4 |= (1 << 7) | // allow for global pages
		   (1 << 9) | // enables xsave/xstore
		   (1 << 10); // enables XM exceptions

	__asm__ volatile("mov %0, %%cr4" ::"r"(cr4));

	serial_init();
}

void amd64_fpu_init(struct cpu_local *cpu_local)
{
	cpu_local->arch_cb.fpu_thread_size = 512;
	cpu_local->arch_cb.fpu_save = fxsave;
	cpu_local->arch_cb.fpu_rstor = fxrstor;
}

void amd64_system_tables(void)
{
	gdt_init();
	idt_init();
	hpet_init();
	apic_init();
	apic_timer_init(SCHED_TICK_RATE_MS);
	amd64_tsc_calibrate();
	boot_aps();
}

void arch_enable_interrupts(void)
{
	__asm__ volatile("sti");
}

void arch_disable_interrupts(void)
{
	__asm__ volatile("cli");
}

void arch_halt()
{
	__asm__ volatile("hlt");
}

bool arch_interrupt_state(void)
{
	return get_interrupt_state();
}
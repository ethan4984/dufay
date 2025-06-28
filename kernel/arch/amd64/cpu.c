#include <arch/amd64/port.h>
#include "arch/port.h"
#include <arch/amd64/cpu.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/gdt.h>
#include <arch/amd64/apic.h>
#include <arch/amd64/hpet.h>
#include <arch/amd64/smp.h>
#include <arch/amd64/debug.h>
#include <core/cpu.h>
#include <aria/sched.h>
#include <aria/time.h>
#include <core/debug.h>
#include <aria/string.h>

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
		panic("fuga: cpuid: tsc/rdtsc unsupported");

	cpuid_state = cpuid(0x80000007, 0);
	if ((cpuid_state.rdx & (1 << 8)) == 0)
		panic("fuga: cpuid: tsc-invariant unsupported\n");

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
		panic("fuga: cpuid: sse unsupported");

	cpuid_state = cpuid(0x80000001, 0);
	if ((cpuid_state.rdx & (1 << 24)) == 0)
		panic("fuga: cpuid: fxsave/fxrstor unsupported\n");

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
	apic_timer_init(1000 / HZ);
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

void arch_set_hardware_ipl(ipl_t ipl)
{
	asm volatile("mov %0, %%cr8" : : "a"((uint64_t)ipl));
}

extern void amd64_context_switch(struct thread *old, struct thread *new);
extern void amd64_load_context(struct thread *td);

extern void _amd64_thread_entry(void);

void amd64_thread_entry(void (*fn)(void), struct thread *prev)
{
	if (prev)
		spinrelease(&prev->lock);
	ipl_lower(IPL_ZERO);

	fn();
	panic("thread shouldn't return!");
}

void arch_context_switch(struct thread *old, struct thread *next)
{
	/// FIXME: do FPU save/restore here
	amd64_context_switch(old, next);
}

void arch_load_context(struct thread *td)
{
	amd64_load_context(td);
}

void arch_context_init(struct arch_thread_context *context,
					   uintptr_t kernel_stack, uintptr_t entry)
{
	struct arch_thread_regs *sp =
		(struct arch_thread_regs *)(kernel_stack -
									sizeof(struct arch_thread_regs));

	memset(sp, 0, sizeof(*sp));

	context->rsp = (uintptr_t)sp;
	sp->rip = (uintptr_t)_amd64_thread_entry;
	sp->r12 = (uintptr_t)entry;
}

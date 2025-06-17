#include <fayt/debug.h>
#include <stdint.h>

struct cpu_local *CORE_LOCAL;

void aarch64_entry(void)
{
	print("aarch64: welcome\n");

	for (;;) {
		__asm__("wfi");
	}
}

void arch_enable_interrupts(void)
{
	__asm__ volatile("msr daifclr, #2");
}

void arch_disable_interrupts(void)
{
	__asm__ volatile("msr daifset, #2");
}

bool arch_interrupt_state(void)
{
	uint64_t daif;
	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	return !(daif & 0x2);
}

void arch_halt(void)
{
	__asm__ volatile("wfi");
}
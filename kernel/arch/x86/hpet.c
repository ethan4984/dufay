#include <arch/x86/hpet.h>
#include <arch/x86/cpu.h>

#include <core/debug.h>

static volatile struct hpet_table *hpet_table;
static volatile struct hpet_regs *hpet_regs;

void hpet_msleep(size_t ms) {
	uint32_t period = hpet_regs->capabilities >> 32;

	volatile size_t ticks = hpet_regs->counter_value + (ms * (1000000000000 / period));

	while(hpet_regs->counter_value < ticks) {
		__asm__ ("pause");
	}
}

void hpet_usleep(size_t us) {
	uint32_t period = hpet_regs->capabilities >> 32;

	volatile size_t ticks = hpet_regs->counter_value + (us * (1000000000 / period));

	while(hpet_regs->counter_value < ticks) {
		__asm__ ("pause");
	}
}

void hpet_nsleep(size_t ns) {
	uint32_t period = hpet_regs->capabilities >> 32;

	volatile size_t ticks = hpet_regs->counter_value + (ns * (1000000 / period));

	while(hpet_regs->counter_value < ticks) {
		__asm__ ("pause");
	}
}

void hpet_init(void) {
	hpet_table = acpi_find_sdt("HPET");
	hpet_regs = (struct hpet_regs*)(hpet_table->address + HIGH_VMA);

	hpet_regs->counter_value = 0;
	hpet_regs->general_config = 1;
} 

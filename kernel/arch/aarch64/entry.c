#include "arch/port.h"
#include "core/cpu.h"
#include "mm/virtual.h"
#include <aria/debug.h>
#include <stdint.h>

struct cpu_local *CORE_LOCAL;

uint64_t HIGH_VMA = 0xffff800000000000;
size_t logical_processor_cnt = 0;
struct cpu_local *logical_processor_locales = NULL;

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

uint64_t arch_read_timestamp_ns()
{
	return 0;
}

size_t arch_largest_page_size()
{
	return 0;
}

void arch_devices_init()
{
}

struct pmap *pmap_new()
{
	return NULL;
}

void pmap_init_kernel()
{
}

void pmap_tlb_flush(uintptr_t)
{
}

void pmap_map(struct pmap *pmap, uint64_t vaddr, uint64_t paddr,
			  enum vm_prot prot, enum vm_flags flags)
{
}

void pmap_activate(struct pmap *pmap)
{
	(void)pmap;
}

void arch_context_init(struct arch_thread_context *ctx, uintptr_t, uintptr_t,
					   void *)
{
}

void arch_context_switch(struct thread *, struct thread *)
{
}

void arch_load_context(struct thread *)
{
}

void arch_send_ipi(struct cpu_local *, uint8_t)
{
}

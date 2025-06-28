#include <mm/address.h>
#include <arch/amd64/paging.h>
#include <arch/amd64/smp.h>
#include <arch/amd64/apic.h>
#include <arch/amd64/cpu.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/gdt.h>

#include <core/scheduler/processor.h>
#include <core/memory/physical.h>
#include <core/memory/virtual.h>
#include <core/debug.h>

#include <fayt/string.h>
#include <fayt/lock.h>
#include <fayt/debug.h>

#include <acpi/madt.h>

static void chain_aps();
static void core_bootstrap(struct cpu_local *);

static struct spinlock core_init_lock;

size_t logical_processor_cnt = 0;
static _Atomic size_t cpus_up = 0;
struct cpu_local *logical_processor_locales;

static void core_bootstrap(struct cpu_local *cpu_local)
{
	amd64_system_init();
	gdt_init();

	print("initialising core: apic_id %x\n",
		  xapic_read(XAPIC_ID_REG_OFF) >> 24);

	spinrelease(&core_init_lock);

	amd64_fpu_init(cpu_local);
	cpu_local->ipl = 0;

	wrmsr(MSR_GS_BASE, (uintptr_t)cpu_local);

	xapic_write(XAPIC_TPR_OFF, 0);
	xapic_write(XAPIC_SINT_OFF, xapic_read(XAPIC_SINT_OFF) | 0x1ff);

	apic_timer_init(SCHED_TICK_RATE_MS);

	atomic_fetch_add(&cpus_up, 1);

	logical_processor_cnt++;

	__asm__ volatile("mov %0, %%cr8\nsti" ::"r"(0ull));

	chain_aps();

	for (;;) {
		__asm__("hlt");
	}
}

__asm__(".global smp_init_begin\n\t"
		"smp_init_begin: .incbin \"build/smp.real.bin\"\n\t"
		".global smp_init_end\n\t"
		"smp_init_end:\n\t");

extern uint64_t smp_init_begin[];
extern uint64_t smp_init_end[];

size_t bootable_processor_cnt;

static void chain_aps()
{
	struct idtr idtr;
	__asm__("sidtq %0" ::"m"(idtr));

	if ((logical_processor_cnt) >= bootable_processor_cnt) {
		pmap_unmap(kernel_mappings.page_table->pmap, 0);
		return;
	}

	struct madt_ent0 *madt0 = &madt_ent0_list.data[logical_processor_cnt];
	struct cpu_local *cpu_local =
		&logical_processor_locales[logical_processor_cnt];

	cpu_local->arch_cb.kernel_stack = pmm_alloc(4, 1) + HIGH_VMA + 0x4000;
	if (!cpu_local->arch_cb.kernel_stack) {
		REPORT_ERROR;
		panic("");
	}
	cpu_local->core_id = madt0->apic_id;
	cpu_local->ipl = 0;

	if (cpu_local->core_id == (xapic_read(XAPIC_ID_REG_OFF) >> 24)) {
		amd64_fpu_init(cpu_local);
		wrmsr(MSR_GS_BASE, (uintptr_t)cpu_local);
		logical_processor_cnt++;

		return chain_aps();
	}

	spinlock(&core_init_lock);

	uint64_t *parameters = (uint64_t *)0x81000;

	*(parameters + 0) = cpu_local->arch_cb.kernel_stack;
	*(parameters + 1) = (uint64_t)kernel_mappings.page_table->pmap->pmlt;
	*(parameters + 2) = (uint64_t)core_bootstrap;
	*(parameters + 3) = (uint64_t)cpu_local;
	*(parameters + 4) = (uint64_t)&idtr;
	*(parameters + 5) = 0; // la57

	struct cpuid_state cpuid_state = cpuid(7, 0);
	if (cpuid_state.rcx & (1 << 16)) {
		parameters[5] = 1;
	}

	uint8_t apic_id = madt0->apic_id;

	xapic_write(XAPIC_ICR_OFF + 0x10, (apic_id << 24));
	xapic_write(XAPIC_ICR_OFF, 0x500); // MT = 0b101 init ipi

	xapic_write(XAPIC_ICR_OFF + 0x10, (apic_id << 24));
	xapic_write(XAPIC_ICR_OFF, 0x600 | 0x80); // MT = 0b11 V=0x80 for 0x80000

	spinlock(&core_init_lock);
	spinrelease(&core_init_lock);
}

void boot_aps(void)
{
	pmap_map(kernel_mappings.page_table->pmap, 0, 0,
			 VM_PROT_PRESENT | VM_PROT_WRITE | VM_PROT_EXECUTE, VM_LARGE_PAGE);

	memcpy8((void *)0x80000, (void *)(uintptr_t)smp_init_begin,
			(uintptr_t)smp_init_end - (uintptr_t)smp_init_begin);

	bootable_processor_cnt = madt_ent0_list.length;
	logical_processor_locales =
		alloc(sizeof(struct cpu_local) * (bootable_processor_cnt + 1));

	if (logical_processor_locales == NULL) {
		REPORT_ERROR;
		panic("");
	}

	for (size_t i = 0; i < bootable_processor_cnt; i++) {
		logical_processor_locales[i].arch_cb.self =
			&logical_processor_locales[i];
		logical_processor_locales[i].ipl = 0;
	}

	atomic_store(&cpus_up, 1);
	logical_processor_cnt = 0;

	chain_aps();

	while (atomic_load(&cpus_up) != bootable_processor_cnt) {
	}

	print("All %d logical processors are up\n", logical_processor_cnt);
}

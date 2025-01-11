#include <arch/x86/paging.h>
#include <arch/x86/smp.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu.h>
#include <arch/x86/idt.h>
#include <arch/x86/gdt.h>

#include <core/physical.h>
#include <core/virtual.h>

#include <fayt/string.h>
#include <core/debug.h>
#include <fayt/lock.h>

#include <acpi/madt.h>

static void chain_aps();
static void core_bootstrap(struct cpu_local*);

static struct spinlock core_init_lock;

size_t logical_processor_cnt = 0;
struct cpu_local *logical_processor_locales;

static void core_bootstrap(struct cpu_local *cpu_local) {
	x86_system_init();
	gdt_init();

	print("initialising core: apic_id %x\n", xapic_read(XAPIC_ID_REG_OFF) >> 24);

	spinrelease(&core_init_lock);

	x86_fpu_init(cpu_local);

	wrmsr(MSR_GS_BASE, (uintptr_t)cpu_local);

	xapic_write(XAPIC_TPR_OFF, 0);
	xapic_write(XAPIC_SINT_OFF, xapic_read(XAPIC_SINT_OFF) | 0x1ff);

	apic_timer_init(SCHED_TICK_RATE_MS);

	__asm__ volatile ("mov %0, %%cr8\nsti" :: "r"(0ull));

	logical_processor_cnt++; chain_aps();

	for(;;) {
		__asm__ ("hlt");
	}
}

__asm__ (
	".global smp_init_begin\n\t"
	"smp_init_begin: .incbin \"arch/x86/smp.bin\"\n\t"
	".global smp_init_end\n\t"
	"smp_init_end:\n\t"
);

extern uint64_t smp_init_begin[];
extern uint64_t smp_init_end[];

size_t bootable_processor_cnt;

static void chain_aps() {
	struct idtr idtr;
	__asm__ ("sidtq %0" :: "m"(idtr));

	if((logical_processor_cnt) >= bootable_processor_cnt) {
		kernel_mappings.unmap_page(&kernel_mappings, 0);
		return;
	}

	struct madt_ent0 *madt0 = &madt_ent0_list.data[logical_processor_cnt];
	struct cpu_local *cpu_local = &logical_processor_locales[logical_processor_cnt];

	cpu_local->kernel_stack = pmm_alloc(4, 1) + HIGH_VMA + 0x4000;
	cpu_local->apic_id = madt0->apic_id;

	if(cpu_local->apic_id == (xapic_read(XAPIC_ID_REG_OFF) >> 24)) {
		x86_fpu_init(cpu_local);
		wrmsr(MSR_GS_BASE, (uintptr_t)cpu_local);
		logical_processor_cnt++; return chain_aps();
	}
	
	spinlock(&core_init_lock);

	uint64_t *parameters = (uint64_t*)0x81000;

	*(parameters + 0) = cpu_local->kernel_stack;
	*(parameters + 1) = (uint64_t)kernel_mappings.pmlt - HIGH_VMA;
	*(parameters + 2) = (uint64_t)core_bootstrap;
	*(parameters + 3) = (uint64_t)cpu_local;
	*(parameters + 4) = (uint64_t)&idtr;
	*(parameters + 5) = 0; // la57

	struct cpuid_state cpuid_state = cpuid(7, 0);
	if(cpuid_state.rcx & (1 << 16)) {
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

void boot_aps(void) {
	kernel_mappings.map_page(&kernel_mappings, 0, 0, X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_PS);
	memcpy8((void*)0x80000, (void*)(uintptr_t)smp_init_begin, (uintptr_t)smp_init_end - (uintptr_t)smp_init_begin);

	bootable_processor_cnt = madt_ent0_list.length;
	logical_processor_locales = alloc(sizeof(struct cpu_local) * (bootable_processor_cnt + 1));

	chain_aps();
}

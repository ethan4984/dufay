#include "arch/port.h"
#include <arch/amd64/cpu.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/apic.h>
#include <core/scheduler/processor.h>

#include <mm/portal.h>
#include <core/debug.h>
#include <core/irq.h>
#include <core/syscall.h>
#include <core/ipl.h>

#include <fayt/lock.h>
#include <fayt/debug.h>

struct idt_descriptor {
	uint16_t offset_low;
	uint16_t cs;
	uint8_t ist;
	uint8_t attributes;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t zero;
} __attribute__((packed));

struct vector {
	void (*handler)(struct registers *, void *);
	void *ptr;
	int reserved;
	struct irq_cortex *irq_cortex;
};

static struct vector interrupt_vectors[256];
static struct idt_descriptor idt[256];

static void set_idt_descriptor(uint16_t cs, uint8_t ist, uint8_t attributes,
							   uint64_t offset, uint8_t index)
{
	idt[index] = (struct idt_descriptor){
		.offset_low = offset & 0xffff,
		.cs = cs,
		.ist = ist,
		.attributes = attributes,
		.offset_mid = offset >> 16 & 0xffff,
		.offset_high = offset >> 32 & 0xffffffff,
	};
}

int idt_reserve_vector(void)
{
	for (size_t i = 0; i < 256; i++) {
		if (!interrupt_vectors[i].reserved) {
			interrupt_vectors[i].reserved = true;
			return i;
		}
	}

	return -1;
}

void idt_release_vector(uint8_t vector)
{
	interrupt_vectors[vector].reserved = false;
}

int idt_instantiate_vector(uint8_t vector,
						   void (*handler)(struct registers *, void *),
						   void *ptr, struct irq_cortex *irq_cortex)
{
	interrupt_vectors[vector].handler = handler;
	interrupt_vectors[vector].ptr = ptr;
	interrupt_vectors[vector].reserved = 1;
	interrupt_vectors[vector].irq_cortex = irq_cortex;

	return 0;
}

const char *exception_messages[] = { "Divide by zero",
									 "Debug",
									 "NMI",
									 "Breakpoint",
									 "Overflow",
									 "Bound Range Exceeded",
									 "Invaild Opcode",
									 "Device Not Available",
									 "Double fault",
									 "Co-processor Segment Overrun",
									 "Invaild TSS",
									 "Segment not present",
									 "Stack-Segment Fault",
									 "GPF",
									 "Page Fault",
									 "Reserved",
									 "x87 Floating Point Exception",
									 "allignement check",
									 "Machine check",
									 "SIMD floating-point exception",
									 "Virtualization Excpetion",
									 "Deadlock",
									 "Reserved",
									 "Reserved",
									 "Reserved",
									 "Reserved",
									 "Reserved",
									 "Reserved",
									 "Reserved",
									 "Reserved",
									 "Reserved",
									 "Security Exception",
									 "Reserved",
									 "Triple Fault",
									 "FPU error" };

extern void isr_handler_main(struct registers *regs)
{
	SWAP_TLS(regs);

	/** TODO: Make this architecture-independant somewhat and support setting different priorities for different vectors */
	//	ipl_t oldipl = CORE_LOCAL->ipl;

	//arch_set_hardware_ipl(IPL_DEVICE);

	if (regs->isr_number < 32) {
		static struct spinlock exception_lock;

		uint64_t cr2;
		__asm__ volatile("mov %%cr2, %0" : "=a"(cr2));

		uint64_t cr3;
		__asm__ volatile("mov %%cr3, %0" : "=a"(cr3));

		if (regs->isr_number == 0xe) {
			uint64_t faulting_address;
			__asm__ volatile("mov %%cr2, %0" : "=a"(faulting_address));

			int ret =
				irq_cortex_resolve_fault(faulting_address, regs->error_code);
			if (ret == 0)
				goto done;

			ret = portal_resolve_fault(faulting_address, regs->error_code);
			if (ret == 0)
				goto done;
		}

		spinlock(&exception_lock);

		print("DEBUG: Kowalski analysis: \"%s\" with error code: %x\n",
			  exception_messages[regs->isr_number], regs->error_code);
		print("DEBUG: rax: %x | rbx: %x | rcx: %x | rdx: %x\n", regs->rax,
			  regs->rbx, regs->rcx, regs->rdx);
		print("DEBUG: rsi: %x | rdi: %x | rbp: %x | rsp: %x\n", regs->rsi,
			  regs->rdi, regs->rbp, regs->rsp);
		print("DEBUG: r8: %x | r9: %x | r10: %x | r11: %x\n", regs->r8,
			  regs->r9, regs->r10, regs->r11);
		print("DEBUG: r12: %x | r13: %x | r14: %x | r15: %x\n", regs->r12,
			  regs->r13, regs->r14, regs->r15);
		print("DEBUG: cs: %x | ss: %x | cr2: %x | rip: %x\n", regs->cs,
			  regs->ss, cr2, regs->rip);
		print("DEBUG: cr3: %x\n", cr3);

		spinrelease(&exception_lock);

		for (;;) {
			__asm__("hlt");
		}
	}

	if (interrupt_vectors[regs->isr_number].handler != NULL) {
		struct irq_cortex *cortex =
			interrupt_vectors[regs->isr_number].irq_cortex;
		if (cortex) {
			uint64_t error_code = regs->error_code;
			regs->error_code = cortex->flush;
			interrupt_vectors[regs->isr_number].handler(
				regs, interrupt_vectors[regs->isr_number].ptr);
			regs->error_code = error_code;
			if (cortex->flush)
				cortex->flush = false;
		} else {
			interrupt_vectors[regs->isr_number].handler(
				regs, interrupt_vectors[regs->isr_number].ptr);
		}
	}
done:
	//arch_set_hardware_ipl(oldipl);

	// if (is_softint_pending(oldipl)) {
	// 	dispatch_software_interrupts(oldipl);
	// }

	SWAP_TLS(regs);

	xapic_write(XAPIC_EOI_OFF, 0);
}

void syscall_handler(struct registers *, void *);

void idt_init()
{
	for (int i = 0; i < 48; i++) {
		interrupt_vectors[i].reserved = 1;
	}

	interrupt_vectors[32].handler = reschedule;
	interrupt_vectors[32].ptr = NULL;
	interrupt_vectors[32].reserved = 1;

	interrupt_vectors[128].handler = syscall_handler;
	interrupt_vectors[128].ptr = NULL;
	interrupt_vectors[128].reserved = 1;

	extern void isr0();
	extern void isr1();
	extern void isr2();
	extern void isr3();
	extern void isr4();
	extern void isr5();
	extern void isr6();
	extern void isr7();
	extern void error_isr8();
	extern void isr9();
	extern void error_isr10();
	extern void error_isr11();
	extern void error_isr12();
	extern void error_isr13();
	extern void error_isr14();
	extern void isr15();
	extern void isr16();
	extern void isr17();
	extern void isr18();
	extern void isr19();
	extern void isr20();
	extern void isr21();
	extern void isr22();
	extern void isr23();
	extern void isr24();
	extern void isr25();
	extern void isr26();
	extern void isr27();
	extern void isr28();
	extern void isr29();
	extern void isr30();
	extern void isr31();
	extern void isr32();
	extern void isr33();
	extern void isr34();
	extern void isr35();
	extern void isr36();
	extern void isr37();
	extern void isr38();
	extern void isr39();
	extern void isr40();
	extern void isr41();
	extern void isr42();
	extern void isr43();
	extern void isr44();
	extern void isr45();
	extern void isr46();
	extern void isr47();
	extern void isr48();
	extern void isr49();
	extern void isr50();
	extern void isr51();
	extern void isr52();
	extern void isr53();
	extern void isr54();
	extern void isr55();
	extern void isr56();
	extern void isr57();
	extern void isr58();
	extern void isr59();
	extern void isr60();
	extern void isr61();
	extern void isr62();
	extern void isr63();
	extern void isr64();
	extern void isr65();
	extern void isr66();
	extern void isr67();
	extern void isr68();
	extern void isr69();
	extern void isr70();
	extern void isr71();
	extern void isr72();
	extern void isr73();
	extern void isr74();
	extern void isr75();
	extern void isr76();
	extern void isr77();
	extern void isr78();
	extern void isr79();
	extern void isr80();
	extern void isr81();
	extern void isr82();
	extern void isr83();
	extern void isr84();
	extern void isr85();
	extern void isr86();
	extern void isr87();
	extern void isr88();
	extern void isr89();
	extern void isr90();
	extern void isr91();
	extern void isr92();
	extern void isr93();
	extern void isr94();
	extern void isr95();
	extern void isr96();
	extern void isr97();
	extern void isr98();
	extern void isr99();
	extern void isr100();
	extern void isr101();
	extern void isr102();
	extern void isr103();
	extern void isr104();
	extern void isr105();
	extern void isr106();
	extern void isr107();
	extern void isr108();
	extern void isr109();
	extern void isr110();
	extern void isr111();
	extern void isr112();
	extern void isr113();
	extern void isr114();
	extern void isr115();
	extern void isr116();
	extern void isr117();
	extern void isr118();
	extern void isr119();
	extern void isr120();
	extern void isr121();
	extern void isr122();
	extern void isr123();
	extern void isr124();
	extern void isr125();
	extern void isr126();
	extern void isr127();
	extern void isr128();
	extern void isr129();
	extern void isr130();
	extern void isr131();
	extern void isr132();
	extern void isr133();
	extern void isr134();
	extern void isr135();
	extern void isr136();
	extern void isr137();
	extern void isr138();
	extern void isr139();
	extern void isr140();
	extern void isr141();
	extern void isr142();
	extern void isr143();
	extern void isr144();
	extern void isr145();
	extern void isr146();
	extern void isr147();
	extern void isr148();
	extern void isr149();
	extern void isr150();
	extern void isr151();
	extern void isr152();
	extern void isr153();
	extern void isr154();
	extern void isr155();
	extern void isr156();
	extern void isr157();
	extern void isr158();
	extern void isr159();
	extern void isr160();
	extern void isr161();
	extern void isr162();
	extern void isr163();
	extern void isr164();
	extern void isr165();
	extern void isr166();
	extern void isr167();
	extern void isr168();
	extern void isr169();
	extern void isr170();
	extern void isr171();
	extern void isr172();
	extern void isr173();
	extern void isr174();
	extern void isr175();
	extern void isr176();
	extern void isr177();
	extern void isr178();
	extern void isr179();
	extern void isr180();
	extern void isr181();
	extern void isr182();
	extern void isr183();
	extern void isr184();
	extern void isr185();
	extern void isr186();
	extern void isr187();
	extern void isr188();
	extern void isr189();
	extern void isr190();
	extern void isr191();
	extern void isr192();
	extern void isr193();
	extern void isr194();
	extern void isr195();
	extern void isr196();
	extern void isr197();
	extern void isr198();
	extern void isr199();
	extern void isr200();
	extern void isr201();
	extern void isr202();
	extern void isr203();
	extern void isr204();
	extern void isr205();
	extern void isr206();
	extern void isr207();
	extern void isr208();
	extern void isr209();
	extern void isr210();
	extern void isr211();
	extern void isr212();
	extern void isr213();
	extern void isr214();
	extern void isr215();
	extern void isr216();
	extern void isr217();
	extern void isr218();
	extern void isr219();
	extern void isr220();
	extern void isr221();
	extern void isr222();
	extern void isr223();
	extern void isr224();
	extern void isr225();
	extern void isr226();
	extern void isr227();
	extern void isr228();
	extern void isr229();
	extern void isr230();
	extern void isr231();
	extern void isr232();
	extern void isr233();
	extern void isr234();
	extern void isr235();
	extern void isr236();
	extern void isr237();
	extern void isr238();
	extern void isr239();
	extern void isr240();
	extern void isr241();
	extern void isr242();
	extern void isr243();
	extern void isr244();
	extern void isr245();
	extern void isr246();
	extern void isr247();
	extern void isr248();
	extern void isr249();
	extern void isr250();
	extern void isr251();
	extern void isr252();
	extern void isr253();
	extern void isr254();
	extern void isr255();

	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr0, 0);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr1, 1);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr2, 2);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr3, 3);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr4, 4);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr5, 5);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr6, 6);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr7, 7);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)error_isr8, 8);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr9, 9);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)error_isr10, 10);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)error_isr11, 11);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)error_isr12, 12);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)error_isr13, 13);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)error_isr14, 14);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr15, 15);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr16, 16);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr17, 17);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr18, 18);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr19, 19);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr20, 20);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr21, 21);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr22, 22);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr23, 23);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr24, 24);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr25, 25);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr26, 26);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr27, 27);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr28, 28);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr29, 29);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr30, 30);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr31, 31);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr32, 32);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr33, 33);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr34, 34);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr35, 35);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr36, 36);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr37, 37);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr38, 38);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr39, 39);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr40, 40);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr41, 41);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr42, 42);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr43, 43);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr44, 44);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr45, 45);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr46, 46);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr47, 47);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr48, 48);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr49, 49);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr50, 50);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr51, 51);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr52, 52);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr53, 53);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr54, 54);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr55, 55);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr56, 56);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr57, 57);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr58, 58);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr59, 59);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr60, 60);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr61, 61);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr62, 62);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr63, 63);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr64, 64);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr65, 65);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr66, 66);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr67, 67);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr68, 68);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr69, 69);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr70, 70);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr71, 71);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr72, 72);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr73, 73);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr74, 74);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr75, 75);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr76, 76);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr77, 77);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr78, 78);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr79, 79);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr80, 80);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr81, 81);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr82, 82);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr83, 83);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr84, 84);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr85, 85);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr86, 86);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr87, 87);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr88, 88);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr89, 89);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr90, 90);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr91, 91);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr92, 92);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr93, 93);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr94, 94);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr95, 95);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr96, 96);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr97, 97);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr98, 98);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr99, 99);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr100, 100);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr101, 101);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr102, 102);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr103, 103);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr104, 104);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr105, 105);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr106, 106);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr107, 107);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr108, 108);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr109, 109);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr110, 110);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr111, 111);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr112, 112);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr113, 113);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr114, 114);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr115, 115);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr116, 116);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr117, 117);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr118, 118);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr119, 119);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr120, 120);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr121, 121);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr122, 122);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr123, 123);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr124, 124);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr125, 125);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr126, 126);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr127, 127);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr128, 128);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr129, 129);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr130, 130);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr131, 131);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr132, 132);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr133, 133);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr134, 134);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr135, 135);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr136, 136);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr137, 137);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr138, 138);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr139, 139);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr140, 140);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr141, 141);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr142, 142);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr143, 143);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr144, 144);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr145, 145);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr146, 146);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr147, 147);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr148, 148);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr149, 149);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr150, 150);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr151, 151);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr152, 152);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr153, 153);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr154, 154);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr155, 155);

	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr156, 156);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr157, 157);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr158, 158);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr159, 159);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr160, 160);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr161, 161);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr162, 162);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr163, 163);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr164, 164);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr165, 165);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr166, 166);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr167, 167);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr168, 168);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr169, 169);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr170, 170);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr171, 171);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr172, 172);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr173, 173);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr174, 174);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr175, 175);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr176, 176);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr177, 177);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr178, 178);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr179, 179);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr180, 180);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr181, 181);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr182, 182);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr183, 183);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr184, 184);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr185, 185);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr186, 186);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr187, 187);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr188, 188);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr189, 189);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr190, 190);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr191, 191);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr192, 192);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr193, 193);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr194, 194);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr195, 195);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr196, 196);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr197, 197);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr198, 198);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr199, 199);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr200, 200);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr201, 201);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr202, 202);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr203, 203);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr204, 204);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr205, 205);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr206, 206);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr207, 207);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr208, 208);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr209, 209);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr210, 210);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr211, 211);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr212, 212);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr213, 213);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr214, 214);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr215, 215);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr216, 216);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr217, 217);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr218, 218);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr219, 219);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr220, 220);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr221, 221);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr222, 222);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr223, 223);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr224, 224);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr225, 225);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr226, 226);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr227, 227);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr228, 228);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr229, 229);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr230, 230);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr231, 231);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr232, 232);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr233, 233);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr234, 234);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr235, 235);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr236, 236);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr237, 237);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr238, 238);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr239, 239);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr240, 240);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr241, 241);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr242, 242);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr243, 243);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr244, 244);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr245, 245);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr246, 246);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr247, 247);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr248, 248);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr249, 249);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr250, 250);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr251, 251);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr252, 252);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr253, 253);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr254, 254);
	set_idt_descriptor(0x28, 0, 0x8e, (uintptr_t)isr255, 255);

	volatile struct idtr idtr = { .limit = sizeof(idt) - 1,
								  .offset = (uintptr_t)idt };

	__asm__ volatile("lidtq %0" : "=m"(idtr));
}

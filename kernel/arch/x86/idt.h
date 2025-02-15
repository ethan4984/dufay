#ifndef IDT_H_
#define IDT_H_

#include <core/irq.h>

#include <arch/x86/cpu.h>

struct idtr {
	uint16_t limit;
	uint64_t offset;
} __attribute__((packed));

int idt_instantiate_vector(uint8_t, void (*handler)(struct registers *, void *),
						   void *ptr, struct irq_cortex *irq_cortex);
int idt_reserve_vector(void);
void idt_init(void);

#endif

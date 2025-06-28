#ifndef IRQ_H_
#define IRQ_H_

#include <mm/virtual.h>
#include <core/lock.h>

#include <aria/pci.h>
#include <aria/irq.h>
#include <aria/aslr.h>

struct irq_family {};

struct irq_cortex {
	const char *identifier;
	struct anchor *anchor_root;
	struct aslr_layout *aslr_layout;
	struct elf64_file *elf;
	int flush;
};

int irq_cortex_resolve_fault(uintptr_t, uint64_t);

#endif

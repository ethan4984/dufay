#ifndef IRQ_H_
#define IRQ_H_

#include <core/virtual.h>
#include <core/server.h>
#include <core/lock.h>
#include <core/aslr.h>

#include <fayt/pci.h>
#include <fayt/irq.h>

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

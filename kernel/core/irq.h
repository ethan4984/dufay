#ifndef IRQ_H_
#define IRQ_H_

#include <core/virtual.h>
#include <core/server.h>
#include <core/lock.h>
#include <core/aslr.h>

#include <fayt/pci.h>

struct anchor {
	const char *identifier;

	struct frame frame;
	size_t offset;

	struct {
		struct pci_bar bar;
	} mmio;

	struct anchor *next;
	struct anchor *last;

	int refcnt;
	struct spinlock lock;
};

struct irq_cortex {
	struct anchor *anchor_cluster;
	struct aslr_layout *aslr_layout;
	struct elf64_file *elf;
};

int irq_cortex_resolve_fault(uintptr_t, uint64_t);

#endif

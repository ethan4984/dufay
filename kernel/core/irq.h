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

	int refcnt;
	struct spinlock lock;
};

struct anchor_cluster {
	int length;
	struct anchor anchors[];
};

struct irq_cortex {
	struct anchor_cluster *cluster;
	struct aslr_layout *aslr_layout;
};

int irq_cortex_resolve_fault(uintptr_t, uint64_t);
int irq_cortex_instantiate(const char*);

#endif

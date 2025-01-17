#ifndef IRQ_H_
#define IRQ_H_

#include <core/virtual.h>
#include <core/server.h>
#include <core/lock.h>

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
	struct server *server;
	struct anchor_cluster *cluster;
};

int instantiate_irq_cortex(const char*);

#endif

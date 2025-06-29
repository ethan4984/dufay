#ifndef IRQ_H_
#define IRQ_H_

#include <mm/virtual.h>
#include <core/lock.h>

#include <aria/aslr.h>

struct anchor {
	int identifier;

	uint64_t paddr;

	struct anchor *next;
	struct anchor *last;

	int refcnt;
	struct spinlock lock;
};

struct irq_state {
	uint64_t padding0[15];
	uint64_t vector;
	uint64_t flush;
	uint64_t padding1[5];
};

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

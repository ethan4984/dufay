#ifndef FAYT_IRQ_H_
#define FAYT_IRQ_H_

#include <fayt/lock.h>

#include <stdint.h>

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

#endif

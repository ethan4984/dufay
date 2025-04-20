#ifndef CORE_FUTEX_H_
#define CORE_FUTEX_H_

#include <core/events.h>

struct futex {
	struct equeue equeue;
	struct etrigger etrigger;

	uint64_t paddr;
	int expected;

	struct spinlock lock;
	int refcnt;
};

#endif

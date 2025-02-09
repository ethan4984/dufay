#ifndef FUTEX_H_ 
#define FUTEX_H_

#include <core/events.h>

struct futex {
	struct equeue equeue;
	struct etrigger etrigger;

	uint64_t paddr;

	int locked;
	int expected;
	int operation;
};

#endif

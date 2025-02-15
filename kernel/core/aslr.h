#ifndef ASLR_H_
#define ASLR_H_

#include <stdint.h>
#include <stddef.h>

struct aslr_layout {
	uintptr_t lower_bound;
	uintptr_t upper_bound;

	struct aslr_layout *next;
	struct aslr_layout *last;
};

struct aslr {
	struct aslr_layout *layout;

	uintptr_t minimum_vaddr;
	uintptr_t maximum_vaddr;
};

int aslr_generate_layout(struct aslr *aslr, struct aslr_layout **ret,
						 size_t length);

#endif

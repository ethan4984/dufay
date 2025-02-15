#ifndef FAYT_ADDRESS_H_
#define FAYT_ADDRESS_H_

#include <stddef.h>
#include <stdint.h>

constexpr int PAGE_SIZE = 0x1000;

struct address_hole {
	uintptr_t base;
	size_t limit;

	struct address_hole *next;
	struct address_hole *last;
};

struct address_space {
	struct address_hole *hole_root;
	struct address_hole *hole_tail;

	uintptr_t current;
	uintptr_t base; 
	size_t limit; 
};

int as_allocate(struct address_space*, uintptr_t*, size_t);
int as_insert_hole(struct address_space*, struct address_hole*);
int as_delete_hole(struct address_space*, uintptr_t, size_t);
int as_address(struct address_space*, uintptr_t*, size_t);

extern struct address_space address_space;

#endif

#ifndef CORE_MEMORY_ADDRESS_H_
#define CORE_MEMORY_ADDRESS_H_

#include <core/memory/virtual.h>

#include <fayt/aslr.h>

struct address_space_capability {
	int asid;
};

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

	struct page_table *page_table;

	int asid;
	struct aslr aslr;
	struct spinlock lock;
};

extern struct address_space kernel_mappings;

int address_find_as(int asid, struct address_space **as);
int address_push_as(struct address_space *as);
int address_space_construct(int *asid);
int address_space_allocate(struct address_space *as, uintptr_t *address,
						   size_t length);
int address_space_free(struct address_space *as, uintptr_t address,
					   size_t length);

#endif

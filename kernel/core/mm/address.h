#ifndef CORE_MM_ADDRESS_H_
#define CORE_MM_ADDRESS_H_

#include <core/mm/virtual.h>

struct address_space_handle {
	int asid;
};

struct address_space {
	struct page_table *page_table;
	int asid;
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

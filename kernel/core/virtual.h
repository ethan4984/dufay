#pragma once

#include <core/portal.h>

#include <fayt/vector.h>
#include <fayt/lock.h>
#include <fayt/hash.h>

#include <stdint.h>
#include <stddef.h>

struct frame {
	uint64_t paddr;
	int refcnt;
};

struct page {
	uint64_t vaddr;
	uint64_t paddr;
	uint64_t flags;

	struct frame *frame;
	uint64_t *pmle;

	int *refcnt;
};

struct page_table {
	struct portal *portal_root;
	struct hash_table *pages;

	uint64_t *(*map_page)(struct page_table *page_table, uint64_t vaddr,
						  uint64_t paddr, uint64_t flags);
	uint64_t *(*page_entry)(struct page_table *page_table, uint64_t vaddr);
	uint64_t (*unmap_page)(struct page_table *page_table, uint64_t vaddr);

	uint64_t *pmlt;
	int asid;

	struct spinlock lock;
};

extern struct page_table kernel_mappings;

int vmm_init(void);
int vmm_init_page_table(struct page_table *page_table);
int vmm_map_range(struct page_table *page_table, uint64_t vaddr, uint64_t cnt,
				  uint64_t flags);
int vmm_unmap_range(struct page_table *page_table, uint64_t vaddr,
					uint64_t cnt);
int vmm_default_table(struct page_table *page_table);

int vmm_as_find(int, struct page_table **);
int vmm_as_push(struct page_table *);

#pragma once

#include <core/portal.h>

#include <fayt/vector.h>
#include <fayt/lock.h>

#include <stdint.h>
#include <stddef.h>

struct frame {
	uint64_t paddr;
	int refcnt;
};

struct page {
	uint64_t vaddr;
	uint64_t paddr;
	struct frame *frame;
};

struct page_table {
	struct portal *portal_root;
	struct hash_table *pages;
	
	uint64_t *(*map_page)(struct page_table *page_table, uint64_t vaddr, uint64_t paddr, uint64_t flags);
	uint64_t *(*page_entry)(struct page_table *page_table, uint64_t vaddr);
	uint64_t (*unmap_page)(struct page_table *page_table, uint64_t vaddr);

	uint64_t *pmlt;

	struct spinlock lock;
};

extern struct page_table kernel_mappings;

void vmm_init(void);
void vmm_init_page_table(struct page_table *page_table);
void vmm_map_range(struct page_table *page_table, uint64_t vaddr, uint64_t cnt, uint64_t flags);
void vmm_unmap_range(struct page_table *page_table, uint64_t vaddr, uint64_t cnt);
void vmm_default_table(struct page_table *page_table);

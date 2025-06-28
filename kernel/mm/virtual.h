#ifndef CORE_MEMORY_VIRTUAL_H_
#define CORE_MEMORY_VIRTUAL_H_

#include <mm/portal.h>

#include <fayt/vector.h>
#include <fayt/lock.h>
#include <fayt/dictionary.h>

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
	struct dictionary *pages;

	uint64_t *(*page_entry)(struct page_table *page_table, uint64_t vaddr);

	struct pmap *pmap;

	struct spinlock lock;
};

enum vm_prot {
	VM_PROT_PRESENT = (1 << 0),
	VM_PROT_WRITE = (1 << 1),
	VM_PROT_EXECUTE = (1 << 2),
	VM_PROT_USER = (1 << 3),
	VM_PROT_ALL =
		(VM_PROT_PRESENT | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_USER),
};

enum vm_flags {
	VM_LARGE_PAGE = (1 << 0),
	VM_HUGE_PAGE = (1 << 1),
	VM_GLOBAL = (1 << 2),
};

/* Architecture-dependent pagemap (pmap) definitions */
/* TODO: maybe have a per-architecture vaddr_t? */

struct pmap *pmap_new(void);

void pmap_map(struct pmap *pmap, uint64_t vaddr, uint64_t paddr,
			  enum vm_prot prot, enum vm_flags flags);

void pmap_unmap(struct pmap *pmap, uint64_t vaddr);

void pmap_activate(struct pmap *pmap);

void pmap_destroy(struct pmap *pmap);

int pmap_translate(struct pmap *pmap, uint64_t vaddr, uint64_t *outpaddr,
				   enum vm_flags *flags, enum vm_prot *prot);

void pmap_tlb_flush(uint64_t vaddr);

size_t arch_largest_page_size();

void pmap_init_kernel(void);

int vmm_init(void);
int vmm_init_page_table(struct page_table *page_table);
int vmm_map_range(struct page_table *page_table, uint64_t vaddr, uint64_t cnt,
				  enum vm_prot prot, enum vm_flags flags);
int vmm_unmap_range(struct page_table *page_table, uint64_t vaddr,
					uint64_t cnt);
int vmm_default_table(struct page_table *page_table);

#endif

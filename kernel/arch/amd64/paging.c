#include <arch/amd64/port.h>
#include <mm/address.h>
#include <arch/port.h>

#include <arch/amd64/paging.h>
#include <arch/amd64/cpu.h>

#include <mm/physical.h>
#include <mm/virtual.h>

#include <aria/lock.h>
#include <aria/debug.h>

#define PTE_ADDR_MASK 0x000ffffffffff000
#define PTE_GET_ADDR(VALUE) ((VALUE) & PTE_ADDR_MASK)
#define PTE_GET_FLAGS(VALUE) ((VALUE) & ~PTE_ADDR_MASK)

struct pml_indices {
	uint16_t pml5_index;
	uint16_t pml4_index;
	uint16_t pml3_index;
	uint16_t pml2_index;
	uint16_t pml1_index;
};

static bool pml5_supported = false;
static bool gib_pages_supported = false;

static struct pml_indices compute_table_indices(uint64_t vaddr)
{
	struct pml_indices ret;

	ret.pml5_index = (vaddr >> 48) & 0x1ff;
	ret.pml4_index = (vaddr >> 39) & 0x1ff;
	ret.pml3_index = (vaddr >> 30) & 0x1ff;
	ret.pml2_index = (vaddr >> 21) & 0x1ff;
	ret.pml1_index = (vaddr >> 12) & 0x1ff;

	return ret;
}

static uint64_t vm_prot_to_x86(enum vm_prot prot, enum vm_flags flags)
{
	uint64_t ret = 0;

	if (prot & VM_PROT_PRESENT)
		ret |= X86_FLAGS_P;
	if (prot & VM_PROT_WRITE)
		ret |= X86_FLAGS_RW;
	if (!(prot & VM_PROT_EXECUTE))
		ret |= X86_FLAGS_NX;
	if (prot & VM_PROT_USER)
		ret |= X86_FLAGS_US;
	if (flags & VM_GLOBAL)
		ret |= X86_FLAGS_G;
	if ((flags & VM_LARGE_PAGE) || (flags & VM_HUGE_PAGE)) {
		ret |= X86_FLAGS_PS;
	}

	return ret;
}

static enum vm_prot x86_to_vm_prot(uint64_t x86_flags)
{
	enum vm_prot prot = 0;

	if (x86_flags & X86_FLAGS_P)
		prot |= VM_PROT_PRESENT;
	if (x86_flags & X86_FLAGS_RW)
		prot |= VM_PROT_WRITE;
	if (!(x86_flags & X86_FLAGS_NX))
		prot |= VM_PROT_EXECUTE;
	if (x86_flags & X86_FLAGS_US)
		prot |= VM_PROT_USER;

	return prot;
}

static uint64_t *descend_table(uint64_t *table, uint16_t index, bool alloc,
							   uint64_t prot)
{
	(void)prot;

	uint64_t *entry = &table[index];

	/* Entry is already present */
	if (*entry & X86_FLAGS_P) {
		return (uint64_t *)P2V(PTE_GET_ADDR(*entry));
	}

	if (!alloc) {
		return NULL;
	}

	/* Allocate a new page for the entry */
	*entry = pmm_alloc(1, 1) | vm_prot_to_x86(VM_PROT_ALL, 0);

	if (!*entry) {
		REPORT_ERROR;
		panic("");
	}

	return (uint64_t *)P2V(PTE_GET_ADDR(*entry));
}

void pmap_map(struct pmap *pmap, uint64_t vaddr, uint64_t paddr,
			  enum vm_prot prot, enum vm_flags flags)
{
	struct pml_indices indices = compute_table_indices(vaddr);
	uint64_t *pml5, *pml4, *pml3, *pml2, *pml1;

	pml5 = pmap->pmlt;

	if (pml5_supported) {
		pml4 = descend_table(pml5, indices.pml5_index, true,
							 vm_prot_to_x86(prot, flags));
	} else {
		pml4 = pml5;
	}

	pml3 = descend_table(pml4, indices.pml4_index, true,
						 vm_prot_to_x86(prot, flags));

	if ((flags & VM_HUGE_PAGE) && gib_pages_supported) {
		/* 1 gb page mapping */
		pml3[indices.pml3_index] = paddr | vm_prot_to_x86(prot, flags);
		return;
	}

	pml2 = descend_table(pml3, indices.pml3_index, true,
						 vm_prot_to_x86(prot, flags));

	if ((flags & VM_HUGE_PAGE) | (flags & VM_LARGE_PAGE)) {
		/* 2 mb page mapping */
		pml2[indices.pml2_index] = paddr | vm_prot_to_x86(prot, flags);
		return;
	}

	pml1 = descend_table(pml2, indices.pml2_index, true,
						 vm_prot_to_x86(prot, flags));

	pml1[indices.pml1_index] = paddr | vm_prot_to_x86(prot, flags);
}

void pmap_unmap(struct pmap *pmap, uint64_t vaddr)
{
	struct pml_indices indices = compute_table_indices(vaddr);
	uint64_t *pml5, *pml4, *pml3, *pml2, *pml1;

	pml5 = pmap->pmlt;

	if (pml5_supported) {
		pml4 = descend_table(pml5, indices.pml5_index, false, 0);
	} else {
		pml4 = pml5;
	}

	if (!pml4)
		return;

	pml3 = descend_table(pml4, indices.pml4_index, false, 0);

	if (!pml3)
		return;

	if (pml3[indices.pml3_index] & X86_FLAGS_PS) {
		/* 1 gb page mapping */
		pml3[indices.pml3_index] = 0;
		invlpg(vaddr);
		return;
	}

	pml2 = descend_table(pml3, indices.pml3_index, false, 0);

	if (!pml2)
		return;

	if (pml2[indices.pml2_index] & X86_FLAGS_PS) {
		/* 2 mb page mapping */
		pml2[indices.pml2_index] = 0;
		invlpg(vaddr);
		return;
	}

	pml1 = descend_table(pml2, indices.pml2_index, false, 0);

	if (!pml1)
		return;

	pml1[indices.pml1_index] = 0;
	invlpg(vaddr);
}

int pmap_translate(struct pmap *pmap, uint64_t vaddr, uint64_t *outpaddr,
				   enum vm_flags *flags, enum vm_prot *prot)
{
	struct pml_indices indices = compute_table_indices(vaddr);
	uint64_t *pml5, *pml4, *pml3, *pml2, *pml1;
	pml5 = pmap->pmlt;
	if (pml5_supported) {
		pml4 = descend_table(pml5, indices.pml5_index, false, 0);
	} else {
		pml4 = pml5;
	}

	if (!pml4)
		return -1;
	pml3 = descend_table(pml4, indices.pml4_index, false, 0);
	if (!pml3)
		return -1;
	if (pml3[indices.pml3_index] & X86_FLAGS_PS) {
		/* 1 gb page mapping */
		if (outpaddr) {
			*outpaddr = PTE_GET_ADDR(pml3[indices.pml3_index]);
		}
		if (flags) {
			*flags = VM_HUGE_PAGE;
		}

		if (prot) {
			*prot = x86_to_vm_prot(PTE_GET_FLAGS(pml3[indices.pml3_index]));
		}
		return 0;
	}

	pml2 = descend_table(pml3, indices.pml3_index, false, 0);
	if (!pml2)
		return -1;
	if (pml2[indices.pml2_index] & X86_FLAGS_PS) {
		/* 2 mb page mapping */
		if (outpaddr) {
			*outpaddr = PTE_GET_ADDR(pml2[indices.pml2_index]);
		}
		if (flags) {
			*flags = VM_LARGE_PAGE;
		}
		if (prot) {
			*prot = x86_to_vm_prot(PTE_GET_FLAGS(pml3[indices.pml3_index]));
		}

		return 0;
	}

	pml1 = descend_table(pml2, indices.pml2_index, false, 0);
	if (!pml1)
		return -1;

	if (!(pml1[indices.pml1_index] & X86_FLAGS_P)) {
		return -1; // Page not present
	}

	if (outpaddr) {
		*outpaddr = PTE_GET_ADDR(pml1[indices.pml1_index]);
	}

	if (prot) {
		*prot = x86_to_vm_prot(PTE_GET_FLAGS(pml1[indices.pml1_index]));
	}

	return 0;
}

void pmap_activate(struct pmap *pmap)
{
	__asm__ volatile("mov %0, %%cr3" ::"r"(V2P(pmap->pmlt)) : "memory");
}

void pmap_tlb_flush(uint64_t vaddr)
{
	invlpg(vaddr);
}

size_t arch_largest_page_size()
{
	return gib_pages_supported ? HUGE_PAGE_SIZE : LARGE_PAGE_SIZE;
}

static uint64_t *amd64_page_entry(struct page_table *page_table, uint64_t vaddr)
{
	struct pmap *pmap = page_table->pmap;
	struct pml_indices indices = compute_table_indices(vaddr);
	uint64_t *pml5, *pml4, *pml3, *pml2, *pml1;

	pml5 = pmap->pmlt;

	if (pml5_supported) {
		pml4 = descend_table(pml5, indices.pml5_index, false, 0);
	} else {
		pml4 = pml5;
	}

	if (!pml4)
		return NULL;

	pml3 = descend_table(pml4, indices.pml4_index, false, 0);
	if (!pml3)
		return NULL;

	if (pml3[indices.pml3_index] & X86_FLAGS_PS) {
		return &pml3[indices.pml3_index];
	}

	pml2 = descend_table(pml3, indices.pml3_index, false, 0);
	if (!pml2)
		return NULL;

	if (pml2[indices.pml2_index] & X86_FLAGS_PS) {
		return &pml2[indices.pml2_index];
	}

	pml1 = descend_table(pml2, indices.pml2_index, false, 0);
	if (!pml1)
		return NULL;

	return &pml1[indices.pml1_index];
}

void amd64_paging_init()
{
	struct cpuid_state cpuid_state = cpuid(7, 0);

	pml5_supported = (cpuid_state.rcx & (1 << 16));

	cpuid_state = cpuid(0x80000001, 0);

	gib_pages_supported = (cpuid_state.rdx & (1 << 26));
}

static struct pmap kernel_pmap = {};

void pmap_init_kernel(void)
{
	kernel_mappings.page_table->pmap = &kernel_pmap;
	kernel_mappings.page_table->page_entry = amd64_page_entry;

	kernel_pmap.pmlt = (uint64_t *)P2V(pmm_alloc(1, 1));

	if (!kernel_pmap.pmlt) {
		REPORT_ERROR;
		panic("Failed to allocate kernel page map");
	}

	/* Preallocate the top 256 entries as they are copied into every process */
	for (size_t i = 256; i < 512; i++) {
		descend_table(kernel_pmap.pmlt, i, true,
					  X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_US);
	}
}

struct pmap *pmap_new(void)
{
	struct pmap *pmap = alloc(sizeof(struct pmap));
	if (!pmap) {
		REPORT_ERROR;
		panic("Failed to allocate page map");
	}

	pmap->pmlt = (uint64_t *)P2V(pmm_alloc(1, 1));
	if (!pmap->pmlt) {
		REPORT_ERROR;
		panic("Failed to allocate page map table");
	}

	for (size_t i = 256; i < 512; i++) {
		pmap->pmlt[i] = kernel_pmap.pmlt[i];
	}

	return pmap;
}

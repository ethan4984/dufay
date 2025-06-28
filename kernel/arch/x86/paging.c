#include <arch/x86/paging.h>
#include <arch/x86/cpu.h>

#include <core/memory/physical.h>
#include <core/memory/virtual.h>

#include <aria/lock.h>
#include <aria/debug.h>

#define PML5_FLAGS_MASK ~(X86_FLAGS_PS | X86_FLAGS_G | X86_FLAGS_NX)
#define PML4_FLAGS_MASK ~(X86_FLAGS_PS | X86_FLAGS_G | X86_FLAGS_NX)
#define PML3_FLAGS_MASK ~(X86_FLAGS_PS | X86_FLAGS_G | X86_FLAGS_NX)
#define PML2_FLAGS_MASK ~(X86_FLAGS_PS | X86_FLAGS_G | X86_FLAGS_NX)

struct pml_indices {
	uint16_t pml5_index;
	uint16_t pml4_index;
	uint16_t pml3_index;
	uint16_t pml2_index;
	uint16_t pml1_index;
};

uint64_t *(*x86_map_page)(struct page_table *, uint64_t, uint64_t,
						  uint64_t) = NULL;
uint64_t *(*x86_page_entry)(struct page_table *, uint64_t) = NULL;
uint64_t (*x86_unmap_page)(struct page_table *, uint64_t) = NULL;
void (*x86_swap_page_table)(struct page_table *) = NULL;

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

static uint64_t *pml4_map_page(struct page_table *page_table, uint64_t vaddr,
							   uint64_t paddr, uint64_t flags)
{
	struct pml_indices pml_indices = compute_table_indices(vaddr);
	spinlock(&page_table->lock);

	if ((page_table->pmlt[pml_indices.pml4_index] & X86_FLAGS_P) == 0) {
		page_table->pmlt[pml_indices.pml4_index] =
			pmm_alloc(1, 1) | (flags & PML4_FLAGS_MASK) | X86_FLAGS_RW;
		if (!page_table->pmlt[pml_indices.pml4_index]) {
			REPORT_ERROR;
			panic("");
		}
	}

	uint64_t *pml3 =
		(uint64_t *)((page_table->pmlt[pml_indices.pml4_index] & ~(0xfff)) +
					 HIGH_VMA);

	if ((pml3[pml_indices.pml3_index] & X86_FLAGS_P) == 0) {
		pml3[pml_indices.pml3_index] = pmm_alloc(1, 1) |
									   (flags & PML3_FLAGS_MASK) | X86_FLAGS_RW;
		if (!pml3[pml_indices.pml3_index]) {
			REPORT_ERROR;
			panic("");
		}
	}

	uint64_t *pml2 =
		(uint64_t *)((pml3[pml_indices.pml3_index] & ~(0xfff)) + HIGH_VMA);

	if (flags & X86_FLAGS_PS) {
		pml2[pml_indices.pml2_index] = paddr | flags;
		spinrelease(&page_table->lock);
		return NULL;
	}

	if ((pml2[pml_indices.pml2_index] & X86_FLAGS_P) == 0) {
		pml2[pml_indices.pml2_index] = pmm_alloc(1, 1) |
									   (flags & PML2_FLAGS_MASK) | X86_FLAGS_RW;
		if (!pml2[pml_indices.pml2_index]) {
			REPORT_ERROR;
			panic("");
		}
	}

	uint64_t *pml1 =
		(uint64_t *)((pml2[pml_indices.pml2_index] & ~(0xfff)) + HIGH_VMA);

	pml1[pml_indices.pml1_index] = paddr | flags;

	spinrelease(&page_table->lock);

	return &pml1[pml_indices.pml1_index];
}

static size_t pml4_unmap_page(struct page_table *page_table, uint64_t vaddr)
{
	struct pml_indices pml_indices = compute_table_indices(vaddr);

	spinlock(&page_table->lock);

	if ((page_table->pmlt[pml_indices.pml4_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return 0;
	}

	uint64_t *pml3 =
		(uint64_t *)((page_table->pmlt[pml_indices.pml4_index] & ~(0xfff)) +
					 HIGH_VMA);

	if ((pml3[pml_indices.pml3_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return 0;
	}

	uint64_t *pml2 =
		(uint64_t *)((pml3[pml_indices.pml3_index] & ~(0xfff)) + HIGH_VMA);

	if ((pml2[pml_indices.pml2_index] & 0xfff) & X86_FLAGS_PS) {
		pml2[pml_indices.pml2_index] &= ~(X86_FLAGS_P);
		invlpg(vaddr);
		spinrelease(&page_table->lock);
		return 0x200000;
	}

	if ((pml2[pml_indices.pml2_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return 0;
	}

	uint64_t *pml1 =
		(uint64_t *)((pml2[pml_indices.pml2_index] & ~(0xfff)) + HIGH_VMA);

	pml1[pml_indices.pml1_index] &= ~(X86_FLAGS_P);
	invlpg(vaddr);

	spinrelease(&page_table->lock);

	return 0x1000;
}

static uint64_t *pml4_page_entry(struct page_table *page_table, uint64_t vaddr)
{
	struct pml_indices pml_indices = compute_table_indices(vaddr);

	spinlock(&page_table->lock);

	if ((page_table->pmlt[pml_indices.pml4_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return NULL;
	}

	uint64_t *pml3 =
		(uint64_t *)((page_table->pmlt[pml_indices.pml4_index] & ~(0xfff)) +
					 HIGH_VMA);

	if ((pml3[pml_indices.pml3_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return NULL;
	}

	uint64_t *pml2 =
		(uint64_t *)((pml3[pml_indices.pml3_index] & ~(0xfff)) + HIGH_VMA);

	if (pml2[pml_indices.pml2_index] & X86_FLAGS_PS) {
		spinrelease(&page_table->lock);
		return &pml2[pml_indices.pml2_index];
	}

	if ((pml2[pml_indices.pml2_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return NULL;
	}

	uint64_t *pml1 =
		(uint64_t *)((pml2[pml_indices.pml2_index] & ~(0xfff)) + HIGH_VMA);

	spinrelease(&page_table->lock);

	return pml1 + pml_indices.pml1_index;
}

static uint64_t *pml5_page_entry(struct page_table *page_table, uint64_t vaddr)
{
	struct pml_indices pml_indices = compute_table_indices(vaddr);

	spinlock(&page_table->lock);

	if ((page_table->pmlt[pml_indices.pml5_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return NULL;
	}

	uint64_t *pml4 =
		(uint64_t *)((page_table->pmlt[pml_indices.pml5_index] & ~(0xfff)) +
					 HIGH_VMA);

	if ((pml4[pml_indices.pml4_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return NULL;
	}

	uint64_t *pml3 =
		(uint64_t *)((pml4[pml_indices.pml4_index] & ~(0xfff)) + HIGH_VMA);

	if ((pml3[pml_indices.pml3_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return NULL;
	}

	uint64_t *pml2 =
		(uint64_t *)((pml3[pml_indices.pml3_index] & ~(0xfff)) + HIGH_VMA);

	if (pml2[pml_indices.pml2_index] & X86_FLAGS_PS) {
		spinrelease(&page_table->lock);
		return &pml2[pml_indices.pml2_index];
	}

	if ((pml2[pml_indices.pml2_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return NULL;
	}

	uint64_t *pml1 =
		(uint64_t *)((pml2[pml_indices.pml2_index] & ~(0xfff)) + HIGH_VMA);

	spinrelease(&page_table->lock);

	return pml1 + pml_indices.pml1_index;
}

static uint64_t *pml5_map_page(struct page_table *page_table, uint64_t vaddr,
							   uint64_t paddr, uint64_t flags)
{
	struct pml_indices pml_indices = compute_table_indices(vaddr);

	spinlock(&page_table->lock);

	if ((page_table->pmlt[pml_indices.pml5_index] & X86_FLAGS_P) == 0) {
		page_table->pmlt[pml_indices.pml5_index] = pmm_alloc(1, 1) |
												   (flags & PML5_FLAGS_MASK);
		if (!page_table->pmlt[pml_indices.pml5_index]) {
			REPORT_ERROR;
			panic("");
		}
	}

	uint64_t *pml4 =
		(uint64_t *)((page_table->pmlt[pml_indices.pml5_index] & ~(0xfff)) +
					 HIGH_VMA);

	if ((pml4[pml_indices.pml4_index] & X86_FLAGS_P) == 0) {
		pml4[pml_indices.pml4_index] = pmm_alloc(1, 1) |
									   (flags & PML4_FLAGS_MASK);
		if (!pml4[pml_indices.pml4_index]) {
			REPORT_ERROR;
			panic("");
		}
	}

	uint64_t *pml3 =
		(uint64_t *)((pml4[pml_indices.pml4_index] & ~(0xfff)) + HIGH_VMA);

	if ((pml3[pml_indices.pml3_index] & X86_FLAGS_P) == 0) {
		pml3[pml_indices.pml3_index] = pmm_alloc(1, 1) |
									   (flags & PML3_FLAGS_MASK);
		if (!pml3[pml_indices.pml3_index]) {
			REPORT_ERROR;
			panic("");
		}
	}

	uint64_t *pml2 =
		(uint64_t *)((pml3[pml_indices.pml3_index] & ~(0xfff)) + HIGH_VMA);

	if (flags & X86_FLAGS_PS) {
		pml2[pml_indices.pml2_index] = paddr | flags;
		spinrelease(&page_table->lock);
		return NULL;
	}

	if ((pml2[pml_indices.pml2_index] & X86_FLAGS_P) == 0) {
		pml2[pml_indices.pml2_index] = pmm_alloc(1, 1) |
									   (flags & PML2_FLAGS_MASK);
		if (!pml2[pml_indices.pml2_index]) {
			REPORT_ERROR;
			panic("");
		}
	}

	uint64_t *pml1 =
		(uint64_t *)((pml2[pml_indices.pml2_index] & ~(0xfff)) + HIGH_VMA);

	pml1[pml_indices.pml1_index] = paddr | flags;

	spinrelease(&page_table->lock);

	return &pml1[pml_indices.pml1_index];
}

static size_t pml5_unmap_page(struct page_table *page_table, uint64_t vaddr)
{
	struct pml_indices pml_indices = compute_table_indices(vaddr);

	spinlock(&page_table->lock);

	if ((page_table->pmlt[pml_indices.pml4_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return 0;
	}

	uint64_t *pml4 =
		(uint64_t *)((page_table->pmlt[pml_indices.pml4_index] & ~(0xfff)) +
					 HIGH_VMA);

	if ((pml4[pml_indices.pml4_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return 0;
	}

	uint64_t *pml3 =
		(uint64_t *)((pml4[pml_indices.pml4_index] & ~(0xfff)) + HIGH_VMA);

	if ((pml3[pml_indices.pml3_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return 0;
	}

	uint64_t *pml2 =
		(uint64_t *)((pml3[pml_indices.pml3_index] & ~(0xfff)) + HIGH_VMA);

	if ((pml2[pml_indices.pml2_index] & 0xfff) & X86_FLAGS_PS) {
		pml2[pml_indices.pml2_index] &= ~(X86_FLAGS_P);
		invlpg(vaddr);
		spinrelease(&page_table->lock);
		return 0x200000;
	}

	if ((pml2[pml_indices.pml2_index] & X86_FLAGS_P) == 0) {
		spinrelease(&page_table->lock);
		return 0;
	}

	uint64_t *pml1 =
		(uint64_t *)((pml2[pml_indices.pml2_index] & ~(0xfff)) + HIGH_VMA);

	pml1[pml_indices.pml1_index] &= ~(X86_FLAGS_P);
	invlpg(vaddr);

	spinrelease(&page_table->lock);

	return 0x1000;
}

void x86_swap_tables(struct page_table *page_table)
{
	__asm__ volatile(
		"mov %0, %%cr3" ::"r"((uint64_t)page_table->pmlt - HIGH_VMA)
		: "memory");
}

void x86_paging_init()
{
	struct cpuid_state cpuid_state = cpuid(7, 0);

	if (cpuid_state.rcx & (1 << 16)) {
		x86_map_page = pml5_map_page;
		x86_unmap_page = pml5_unmap_page;
		x86_page_entry = pml5_page_entry;
	} else {
		x86_map_page = pml4_map_page;
		x86_unmap_page = pml4_unmap_page;
		x86_page_entry = pml4_page_entry;
	}
}

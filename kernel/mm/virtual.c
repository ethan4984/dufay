#include <arch/port.h>
#include <aria/lock.h>

#include <mm/virtual.h>
#include <mm/physical.h>
#include <mm/address.h>
#include <core/debug.h>

#include <aria/string.h>
#include <aria/dictionary.h>
#include <aria/debug.h>

#include <limine.h>

static volatile struct limine_kernel_address_request
	limine_kernel_address_request = { .id = LIMINE_KERNEL_ADDRESS_REQUEST,
									  .revision = 0 };

int vmm_default_table(struct page_table *page_table)
{
	if (page_table == NULL)
		RETURN_ERROR;

	page_table->pages = alloc(sizeof(struct dictionary));
	if (page_table->pages == NULL)
		RETURN_ERROR;

	page_table->pmap = pmap_new();

	return 0;
}

int vmm_map_range(struct page_table *page_table, uint64_t vaddr, uint64_t cnt,
				  enum vm_prot prot, enum vm_flags flags)
{
	if (page_table == NULL)
		RETURN_ERROR;

	size_t max_page_size = arch_largest_page_size();
	spinlock(&page_table->lock);

	if (flags & VM_HUGE_PAGE) {
		for (size_t i = 0; i < cnt; i++) {
			pmap_map(page_table->pmap, vaddr,
					 pmm_alloc(1, max_page_size / PAGE_SIZE), prot, flags);

			vaddr += max_page_size;
		}
	}

	else if (flags & VM_LARGE_PAGE) {
		for (size_t i = 0; i < cnt; i++) {
			pmap_map(page_table->pmap, vaddr,
					 pmm_alloc(1, LARGE_PAGE_SIZE / PAGE_SIZE), prot, flags);

			vaddr += LARGE_PAGE_SIZE;
		}
	}

	else {
		for (size_t i = 0; i < cnt; i++) {
			pmap_map(page_table->pmap, vaddr, pmm_alloc(1, 1), prot, flags);
			vaddr += PAGE_SIZE;
		}
	}

	spinrelease(&page_table->lock);

	return 0;
}

int vmm_unmap_range(struct page_table *page_table, uint64_t vaddr, uint64_t cnt)
{
	if (page_table == NULL)
		RETURN_ERROR;

	// TODO
	(void)vaddr;
	(void)cnt;

	return 0;
}

int vmm_init(void)
{
	kernel_mappings.page_table = alloc(sizeof(struct page_table));

	if (kernel_mappings.page_table == NULL)
		RETURN_ERROR;

	spinlock(&kernel_mappings.lock);

	pmap_init_kernel();

	uintptr_t kernel_vaddr =
		limine_kernel_address_request.response->virtual_base;
	uintptr_t kernel_paddr =
		limine_kernel_address_request.response->physical_base;

	// FIXME: actually map the kernel properly!
	for (size_t i = 0; i < 0x6400; i++) {
		pmap_map(kernel_mappings.page_table->pmap, kernel_vaddr, kernel_paddr,
				 VM_PROT_ALL, VM_GLOBAL);

		kernel_vaddr += PAGE_SIZE;
		kernel_paddr += PAGE_SIZE;
	}

	uint64_t phys = 0;
	size_t max_page_size = arch_largest_page_size();
	size_t gib4 = (4 * (1UL << 30UL));
	uint64_t highest_page = 0;

	struct limine_memmap_entry **mmap = limine_memmap_request.response->entries;
	uint64_t entry_count = limine_memmap_request.response->entry_count;
	for (uint64_t i = 0; i < entry_count; i++) {
		if ((mmap[i]->base + mmap[i]->length) > highest_page &&
			mmap[i]->type != LIMINE_MEMMAP_RESERVED)
			highest_page = mmap[i]->base + mmap[i]->length;
	}

	size_t identity_map_size = (highest_page > gib4) ? highest_page : gib4;

	print("Identity mapping %d bytes of memory using %d bytes pages\n",
		  identity_map_size, max_page_size);

	for (size_t i = 0; i < identity_map_size / max_page_size; i++) {
		pmap_map(kernel_mappings.page_table->pmap, P2V(phys), phys, VM_PROT_ALL,
				 VM_GLOBAL | VM_HUGE_PAGE);

		phys += max_page_size;
	}

	kernel_mappings.page_table->pages = alloc(sizeof(struct dictionary));

	spinrelease(&kernel_mappings.lock);

	pmap_activate(kernel_mappings.page_table->pmap);

	return 0;
}

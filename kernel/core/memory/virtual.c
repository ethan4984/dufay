#include <arch/x86/paging.h>
#include <arch/x86/cpu.h>

#include <core/memory/virtual.h>
#include <core/memory/physical.h>
#include <core/memory/address.h>
#include <core/debug.h>

#include <fayt/string.h>
#include <fayt/dictionary.h>
#include <fayt/debug.h>

#include <limine.h>

static volatile struct limine_kernel_address_request
	limine_kernel_address_request = { .id = LIMINE_KERNEL_ADDRESS_REQUEST,
									  .revision = 0 };

int vmm_default_table(struct page_table *page_table)
{
	if (page_table == NULL)
		RETURN_ERROR;

	x86_paging_init();

	page_table->map_page = x86_map_page;
	page_table->unmap_page = x86_unmap_page;
	page_table->page_entry = x86_page_entry;
	page_table->pages = alloc(sizeof(struct dictionary));
	if (page_table->pages == NULL)
		RETURN_ERROR;

	page_table->pmlt = (uint64_t *)(pmm_alloc(1, 1) + HIGH_VMA);
	if (page_table->pmlt == (void *)HIGH_VMA)
		RETURN_ERROR;

	uintptr_t kernel_vaddr =
		limine_kernel_address_request.response->virtual_base;
	uintptr_t kernel_paddr =
		limine_kernel_address_request.response->physical_base;

	for (size_t i = 0; i < 0x6400; i++) {
		page_table->map_page(page_table, kernel_vaddr, kernel_paddr,
							 X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_G |
								 X86_FLAGS_US);
		kernel_vaddr += 0x1000;
		kernel_paddr += 0x1000;
	}

	uint64_t phys = 0;
	for (size_t i = 0; i < 0x800; i++) {
		page_table->map_page(page_table, phys + HIGH_VMA, phys,
							 X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_PS |
								 X86_FLAGS_G | X86_FLAGS_US);
		phys += 0x200000;
	}

	struct limine_memmap_entry **mmap = limine_memmap_request.response->entries;
	uint64_t entry_count = limine_memmap_request.response->entry_count;

	for (uint64_t i = 0; i < entry_count; i++) {
		phys = (mmap[i]->base / 0x200000) * 0x200000;
		for (size_t j = 0; j < DIV_ROUNDUP(mmap[i]->length, 0x200000); j++) {
			page_table->map_page(page_table, phys + HIGH_VMA, phys,
								 X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_PS |
									 X86_FLAGS_G | X86_FLAGS_US);
			phys += 0x200000;
		}
	}

	return 0;
}

int vmm_map_range(struct page_table *page_table, uint64_t vaddr, uint64_t cnt,
				  uint64_t flags)
{
	if (page_table == NULL)
		RETURN_ERROR;

	if (flags & X86_FLAGS_PS) {
		for (size_t i = 0; i < cnt; i++) {
			page_table->map_page(page_table, vaddr, pmm_alloc(1, 0x200), flags);
			vaddr += 0x200000;
		}
	} else {
		for (size_t i = 0; i < cnt; i++) {
			page_table->map_page(page_table, vaddr, pmm_alloc(1, 1), flags);
			vaddr += 0x1000;
		}
	}

	return 0;
}

int vmm_unmap_range(struct page_table *page_table, uint64_t vaddr, uint64_t cnt)
{
	if (page_table == NULL)
		RETURN_ERROR;

	for (size_t i = 0; i < cnt; i++) {
		size_t page_size = page_table->unmap_page(page_table, vaddr);
		if (!page_size)
			return -1;

		vaddr += page_size;
	}

	return 0;
}

int vmm_init(void)
{
	kernel_mappings.page_table = alloc(sizeof(struct page_table));
	if (kernel_mappings.page_table == NULL)
		RETURN_ERROR;
	if (kernel_mappings.page_table == NULL)
		RETURN_ERROR;

	int ret = vmm_default_table(kernel_mappings.page_table);
	if (ret == -1)
		RETURN_ERROR;

	x86_swap_tables(kernel_mappings.page_table);

	return 0;
}

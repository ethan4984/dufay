#include <arch/x86/paging.h>
#include <arch/x86/smp.h>

#include <core/irq.h>
#include <core/debug.h>
#include <core/syscall.h>
#include <core/server.h>
#include <core/elf.h>

#include <fayt/debug.h>
#include <fayt/compiler.h>

static struct aslr aslr_irq = {
	.layout = NULL,
	.minimum_vaddr = 0xffffe00000000000,
	.maximum_vaddr = 0xfffff00000000000
};

static VECTOR(struct irq_cortex*) cortex_table;

int irq_cortex_resolve_fault(uintptr_t faulting_address, uint64_t error_code) {
	if((error_code & X86_FLAGS_P) != 0) return -1;
	if(faulting_address < aslr_irq.minimum_vaddr ||
		faulting_address >= aslr_irq.maximum_vaddr) return -1;

	struct context *context = CORE_LOCAL->current_context;
	if(unlikely(context == NULL)) return -1;

	struct page_table *page_table = context->page_table;
	if(unlikely(page_table == NULL)) return -1;

	struct irq_cortex *cortex;
	for(int i = 0; i < cortex_table.length; i++) {
		cortex = cortex_table.data[i];
		if(cortex == NULL) continue;
		if(faulting_address >= cortex->aslr_layout->lower_bound &&
			faulting_address < cortex->aslr_layout->upper_bound) goto found;
	}
	return -1;
found:
	uint64_t faulting_page = faulting_address & ~(0xfff);
	uint64_t *pml_entry = page_table->page_entry(&kernel_mappings, faulting_page);
	if(pml_entry == NULL) return -1;

	page_table->map_page(page_table, faulting_page, *pml_entry & 0xfff, *pml_entry & ~(0xfff));

	return 0;
}

int irq_cortex_instantiate(const char *identifier) {
	if(identifier == NULL) RETURN_ERROR;

	struct limine_file *module = limine_search_module(identifier);
	if(module == NULL) RETURN_ERROR;

	struct elf64_file *elf = alloc(sizeof(struct elf64_file));

	elf->data.buffer = module->address;
	elf->data.length = module->size;
	elf->page_table = &kernel_mappings;
	elf->aslr = &aslr_irq;

	int ret = elf64_file_init(elf);
	if(ret == -1) RETURN_ERROR;

	ret = elf64_file_aux(elf, &elf->aux);
	if(ret == -1) RETURN_ERROR;

	ret = elf64_file_load(elf);
	if(ret == -1) RETURN_ERROR;

	return 0;
}

SYSCALL_DEFINE1(spawn_irq_cortex, const char*, identifier, {
	int ret = irq_cortex_instantiate(identifier);
	if(ret == -1) return -1;
})

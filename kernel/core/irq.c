#include <arch/x86/paging.h>
#include <arch/x86/smp.h>
#include <arch/x86/idt.h>

#include <core/irq.h>
#include <core/debug.h>
#include <core/syscall.h>
#include <core/server.h>
#include <core/elf.h>

#include <fayt/debug.h>
#include <fayt/compiler.h>
#include <fayt/string.h>
#include <fayt/hash.h>

static struct aslr aslr_irq = {
	.layout = NULL,
	.minimum_vaddr = 0xffffe00000000000,
	.maximum_vaddr = 0xfffff00000000000
};

static struct hash_table cortex_table;

int irq_cortex_resolve_fault(uintptr_t faulting_address, uint64_t error_code) {
	if((error_code & X86_FLAGS_P) != 0) RETURN_ERROR;
	if(faulting_address < aslr_irq.minimum_vaddr ||
		faulting_address >= aslr_irq.maximum_vaddr) return -1;

	struct context *context = CORE_LOCAL->current_context;
	if(unlikely(context == NULL)) RETURN_ERROR;

	struct page_table *page_table = context->page_table;
	if(unlikely(page_table == NULL)) RETURN_ERROR;

	struct irq_cortex *cortex;
	for(int i = 0; i < cortex_table.capacity; i++) {
		cortex = cortex_table.data[i];
		if(cortex == NULL) continue;
		if(faulting_address >= cortex->aslr_layout->lower_bound &&
			faulting_address < cortex->aslr_layout->upper_bound) goto found;
	}
	RETURN_ERROR;
found:
	uint64_t faulting_page = faulting_address & ~(0xfff);
	uint64_t *pml_entry = kernel_mappings.page_entry(&kernel_mappings, faulting_page);
	if(pml_entry == NULL) RETURN_ERROR;

	struct page *page = alloc(sizeof(struct page));
	if(unlikely(page == NULL)) RETURN_ERROR;

	page->vaddr = faulting_page;
	page->paddr = *pml_entry & ~(0xfff);
	page->flags = *pml_entry & 0xfff;
	page->frame = NULL;
	page->pmle = page_table->map_page(page_table, faulting_page,
		*pml_entry & ~(0xfff), *pml_entry & 0xfff);
	page->refcnt = alloc(sizeof(*page->refcnt));

	int ret = hash_table_push(page_table->pages, &page->vaddr, page, sizeof(page->vaddr));
	if(ret == -1) RETURN_ERROR;

	*page->refcnt = 1;

	return 0;
}

static int irq_cortex_instantiate(const char *path, const char *identifier, int vector) {
	if(identifier == NULL) RETURN_ERROR;

	struct irq_cortex *cortex = alloc(sizeof(struct irq_cortex));
	if(cortex == NULL) RETURN_ERROR;

	struct limine_file *module = limine_search_module(path);
	if(module == NULL) RETURN_ERROR;

	cortex->elf = alloc(sizeof(struct elf64_file));
	if(cortex->elf == NULL) RETURN_ERROR; 

	cortex->elf->data.buffer = module->address;
	cortex->elf->data.length = module->size;
	cortex->elf->page_table = &kernel_mappings;
	cortex->elf->aslr = &aslr_irq;
	cortex->identifier = alloc(strlen(identifier) + 1);
	strcpy((void*)cortex->identifier, identifier);

	int ret = elf64_file_init(cortex->elf);
	if(ret == -1) RETURN_ERROR;

	cortex->aslr_layout = cortex->elf->aslr_layout;

	ret = elf64_file_aux(cortex->elf, &cortex->elf->aux);
	if(ret == -1) RETURN_ERROR;

	ret = elf64_file_load(cortex->elf);
	if(ret == -1) RETURN_ERROR;

	ret = hash_table_push(&cortex_table, (void*)cortex->identifier, cortex, strlen(cortex->identifier));
	if(ret == -1) RETURN_ERROR;

	ret = idt_instantiate_vector(vector, (void*)cortex->elf->aux.at_entry, &cortex->anchor_root, cortex);
	if(ret == -1) RETURN_ERROR;

	return 0;
}

static int irq_cortex_anchor(const char *identifier, struct anchor *anchor) {
	if(identifier == NULL || anchor == NULL) RETURN_ERROR;

	struct irq_cortex *cortex = NULL;
	int ret = hash_table_search(&cortex_table, (void*)identifier, strlen(identifier), (void**)&cortex);
	if(ret == -1 || cortex == NULL) RETURN_ERROR;

	cortex->flush = true;
	{
		struct anchor *tmp = anchor;
		anchor = alloc(sizeof(struct anchor));
		*anchor = *tmp;
	}

	anchor->next = cortex->anchor_root;
	anchor->last = NULL;
	if(cortex->anchor_root) cortex->anchor_root->last = anchor;
	cortex->anchor_root = anchor;

	return 0;
}

SYSCALL_DEFINE3(irq_cortex_instantiate, const char*, path, const char*, identifier, int, vector, {
	int ret = irq_cortex_instantiate(path, identifier, vector);
	if(ret == -1) return -1;
})

SYSCALL_DEFINE2(irq_cortex_anchor, const char*, identifier, struct anchor*, anchor, {
	int ret = irq_cortex_anchor(identifier, anchor);
	if(ret == -1) return -1;
})

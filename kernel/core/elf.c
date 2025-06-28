#include <arch/x86/paging.h>
#include <arch/x86/cpu.h>

#include <core/memory/virtual.h>
#include <core/memory/physical.h>
#include <core/elf.h>
#include <core/debug.h>

#include <aria/slab.h>
#include <aria/compiler.h>
#include <aria/string.h>
#include <aria/debug.h>

int elf64_read(struct elf64_file *file, void *buffer, int offset, size_t cnt)
{
	struct elf64_file_buffer *file_buffer = file->private;
	if (unlikely(file_buffer == NULL))
		RETURN_ERROR;

	if (unlikely(file_buffer->data == NULL || buffer == NULL))
		RETURN_ERROR;
	if (unlikely(file_buffer->length < offset))
		return 0;

	int data_to_read = (file_buffer->length < (offset + cnt)) ?
						   file_buffer->length - offset :
						   cnt;
	memcpy(buffer, file_buffer->data + offset, data_to_read);

	return data_to_read;
}

int elf64_write(struct elf64_file *file, const void *buffer, int offset,
				size_t cnt)
{
	struct elf64_file_buffer *file_buffer = file->private;
	if (unlikely(file_buffer == NULL))
		RETURN_ERROR;

	if (unlikely(file_buffer->data == NULL || buffer == NULL))
		RETURN_ERROR;
	if (unlikely(file_buffer->length < offset))
		return 0;

	int data_to_write = (file_buffer->length < (offset + cnt)) ?
							file_buffer->length - offset :
							cnt;
	memcpy(file_buffer->data + offset, buffer, data_to_write);

	return data_to_write;
}

int elf64_map(struct elf64_file *file, struct elf64_phdr *phdr,
			  struct elf64_shdr *shdr, bool place)
{
	if (file == NULL || (phdr == NULL && shdr == NULL))
		RETURN_ERROR;

	struct elf64_file_buffer *file_buffer = file->private;
	if (unlikely(file_buffer == NULL))
		RETURN_ERROR;

	uintptr_t addr =
		(phdr ? phdr->p_vaddr : shdr->sh_addr) + file->aslr_layout->lower_bound;
	size_t size = phdr ? phdr->p_filesz : shdr->sh_size;
	size_t flags = phdr ? phdr->p_flags : shdr->sh_flags;

	size_t misalignment = addr & (PAGE_SIZE - 1);
	size_t page_cnt = DIV_ROUNDUP(misalignment + size, PAGE_SIZE);

	if ((misalignment + size) > ((size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))) {
		page_cnt++;
	}

	if (page_cnt == 0)
		return 0;
	uintptr_t physical[page_cnt];

	for (int j = 0; j < page_cnt; j++) {
		physical[j] = pmm_alloc(1, 1);
		uintptr_t virtual = addr - misalignment + j * PAGE_SIZE;

		uint64_t page_flags = X86_FLAGS_P | X86_FLAGS_US | X86_FLAGS_NX;
		if ((flags & ELF_PF_W) == ELF_PF_W)
			page_flags |= X86_FLAGS_RW;
		if ((flags & ELF_PF_X) == ELF_PF_X)
			page_flags &= ~X86_FLAGS_NX;

		file_buffer->address_space->page_table->map_page(
			file_buffer->address_space->page_table, virtual, physical[j],
			page_flags);
	}

	if (place) {
		for (int j = 0; j < page_cnt; j++) {
			size_t cnt = ((PAGE_SIZE + j * PAGE_SIZE) > size) ?
							 (size % (j * PAGE_SIZE)) :
							 PAGE_SIZE;

			elf64_read(file,
					   (void *)physical[j] + HIGH_VMA +
						   ((j == 0) ? misalignment : 0),
					   phdr->p_offset + j * PAGE_SIZE,
					   cnt - ((j == 0) ? misalignment : 0));
		}
	}

	return 0;
}

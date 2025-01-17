#include <arch/x86/paging.h>
#include <arch/x86/cpu.h>

#include <core/virtual.h>
#include <core/physical.h>
#include <core/server.h>
#include <core/elf.h>
#include <core/debug.h>

#include <fayt/slab.h>
#include <fayt/compiler.h>
#include <fayt/string.h>
#include <fayt/debug.h>

static int elf64_read(struct elf64_file *file, void *buffer, int offset, size_t cnt) {
	if(unlikely(file->data.buffer == NULL)) RETURN_ERROR;
	if(unlikely(file->data.length < offset)) return 0;

	int data_to_read = (file->data.length < (offset + cnt))
		? file->data.length - offset : cnt;
	memcpy(buffer, file->data.buffer + offset, data_to_read);

	return data_to_read;
}

static int elf64_map(struct elf64_file *file, struct elf64_phdr *phdr, struct elf64_shdr *shdr, bool place) {
	if(file == NULL || (phdr == NULL && shdr == NULL)) RETURN_ERROR;

	uintptr_t addr = phdr ? phdr->p_vaddr : shdr->sh_addr;
	size_t size = phdr ? phdr->p_filesz : shdr->sh_size;
	size_t flags = phdr ? phdr->p_flags : shdr->sh_flags;

	size_t misalignment = addr & (PAGE_SIZE - 1);
	size_t page_cnt = DIV_ROUNDUP(misalignment + size, PAGE_SIZE);

	if((misalignment + size) >
		((size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))) {
		page_cnt++;
	}
	
	if(page_cnt == 0) return 0;
	uintptr_t physical[page_cnt];

	for(int j = 0; j < page_cnt; j++) {
		physical[j] = pmm_alloc(1, 1);
		uintptr_t virtual = addr + file->load_offset - misalignment + j * PAGE_SIZE;

		uint64_t page_flags = X86_FLAGS_P | X86_FLAGS_US | X86_FLAGS_NX;
		if((flags & ELF_PF_W) == ELF_PF_W) page_flags |= X86_FLAGS_RW;
		if((flags & ELF_PF_X) == ELF_PF_X) page_flags &= ~X86_FLAGS_NX;

		file->page_table->map_page(file->page_table, virtual, physical[j], page_flags);
	}

	if(place) {
		for(int j = 0; j < page_cnt; j++) {
			size_t cnt = ((PAGE_SIZE + j * PAGE_SIZE) > size) ?
				(size % (j * PAGE_SIZE)) : PAGE_SIZE;

			elf64_read(file, (void*)physical[j] + HIGH_VMA + ((j == 0) ? misalignment : 0),
					phdr->p_offset + j * PAGE_SIZE, cnt - ((j == 0) ? misalignment : 0));
		}
	}

	return 0;
}

static int elf64_validate(struct elf64_hdr *hdr) {
	uint32_t signature = *(uint32_t*)hdr;
	if(signature != ELF_SIGNATURE) RETURN_ERROR;

	if(hdr->ident[ELF_EI_OSABI] != ELF_EI_SYSTEM_V &&
		hdr->ident[ELF_EI_OSABI] != ELF_EI_LINUX) RETURN_ERROR;
	if(hdr->ident[ELF_EI_DATA] != ELF_LITTLE_ENDIAN) RETURN_ERROR;
	if(hdr->ident[ELF_EI_CLASS] != ELF_ELF64) RETURN_ERROR;
	if(hdr->machine != ELF_MACH_X86_64 && hdr->machine != 0) RETURN_ERROR;

	return 0;
}

static int elf64_is_relocatable(struct elf64_file *file) {
	if(file == NULL) RETURN_ERROR;
	
	for(int i = 0; i < file->hdr->phdr_size; i++) {
		struct elf64_phdr *phdr = &file->phdr[i];
		if(phdr->p_type == ELF_PT_DYNAMIC && phdr->p_filesz) return 0;
	}

	return -1;
}

static int elf64_find_section(struct elf64_file *file, struct elf64_shdr **shdr, const char *name) {
	if(unlikely(file == NULL || shdr == NULL)) RETURN_ERROR;

	for(int i = 0; i < file->hdr->sh_num; i++) {
		if(memcmp(&file->strtab[file->shdr[i].sh_name], name, strlen(name)) == 0) {
			*shdr = &file->shdr[i];
			return 0;
		}
	}

	*shdr = NULL;

	return 0;
}

int elf64_load_section(struct elf64_file *file, const char *name) {
	struct elf64_shdr *shdr;
	int ret = elf64_find_section(file, &shdr, name);
	if(ret == -1) RETURN_ERROR;
	if(shdr == NULL) return -1;

	ret = elf64_map(file, NULL, shdr, false);
	if(ret == -1) return -1;

	return 0;
}

int elf64_file_init(struct elf64_file *file) {
	file->hdr = alloc(sizeof(struct elf64_hdr));

	int ret = elf64_read(file, file->hdr, 0, sizeof(struct elf64_hdr));
	if(unlikely(ret != sizeof(struct elf64_hdr))) RETURN_ERROR;

	file->phdr = alloc(sizeof(struct elf64_phdr) * file->hdr->ph_num);
	file->shdr = alloc(sizeof(struct elf64_shdr) * file->hdr->sh_num);

	ret = elf64_read(file, file->shdr, file->hdr->shoff, sizeof(struct elf64_shdr) * file->hdr->sh_num);
	if(unlikely(ret != sizeof(struct elf64_shdr) * file->hdr->sh_num)) RETURN_ERROR;

	ret = elf64_validate(file->hdr);
	if(unlikely(ret == -1)) RETURN_ERROR;

	ret = elf64_read(file, file->phdr, file->hdr->phoff, sizeof(struct elf64_phdr) * file->hdr->ph_num);
	if(unlikely(ret != sizeof(struct elf64_phdr) * file->hdr->ph_num)) RETURN_ERROR;

	file->strtab_hdr = &file->shdr[file->hdr->shstrndx];
	file->strtab = alloc(file->strtab_hdr->sh_size);

	ret = elf64_read(file, (char*)file->strtab, file->strtab_hdr->sh_offset, file->strtab_hdr->sh_size);
	if(unlikely(ret != file->strtab_hdr->sh_size)) RETURN_ERROR;

	return 0;
}

int elf64_file_load(struct elf64_file *file) {
	int ret = elf64_load_section(file, ".bss");
	if(ret == -1) RETURN_ERROR;

	int relocatable = elf64_is_relocatable(file);
	if(!relocatable) panic("");

	for(size_t i = 0; i < file->hdr->ph_num; i++) {
		if(file->phdr[i].p_type != ELF_PT_LOAD) continue;

		struct elf64_phdr *phdr = &file->phdr[i];

		ret = elf64_map(file, phdr, NULL, true);
		if(ret == -1) return -1;
	}

	return 0;
}

int elf64_file_aux(struct elf64_file *file, struct aux *aux) {
	aux->at_phdr = 0;
	aux->at_phent = sizeof(struct elf64_phdr);
	aux->at_phnum = file->hdr->ph_num;
	aux->at_entry = file->load_offset + file->hdr->entry;

	for(size_t i = 0; i < file->hdr->ph_num; i++) {
		if(file->phdr[i].p_type == ELF_PT_PHDR) {
			aux->at_phdr = file->load_offset + file->phdr[i].p_vaddr;
		}
	}

	return 0;
}

#ifndef ELF_H_
#define ELF_H_

#include <fayt/elf.h>

struct elf64_file_buffer {
	void *data;
	size_t length;
	struct page_table *page_table;
};

int elf64_read(struct elf64_file *file, void *buffer, int offset, size_t cnt);
int elf64_write(struct elf64_file *file, const void *buffer, int offset,
				size_t cnt);
int elf64_map(struct elf64_file *file, struct elf64_phdr *phdr,
			  struct elf64_shdr *shdr, bool place);

#endif

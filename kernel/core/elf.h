#ifndef ELF_H_
#define ELF_H_

#include <core/virtual.h>
#include <core/aslr.h>

#include <stdint.h>
#include <stddef.h>

#define ELF_PF_R 1
#define ELF_PF_W 2
#define ELF_PF_X 4

#define ELF_ET_NONE 0
#define ELF_ET_REL 1
#define ELF_ET_EXEC 2
#define ELF_ET_DYN 3

#define ELF_DT_NULL 0
#define ELF_DT_NEEDED 1
#define ELF_DT_PLTGOT 3
#define ELF_DT_PLTRELSZ 2
#define ELF_DT_HASH 4
#define ELF_DT_SYMTAB 6
#define ELF_DT_RELA 7
#define ELF_DT_RELASZ 8
#define ELF_DT_RELAENT 9
#define ELF_DT_STRSZ 10
#define ELF_DT_SYMENT 11
#define ELF_DT_INIT 12
#define ELF_DT_FINI 13
#define ELF_DT_PLTREL 20
#define ELF_DT_SONAME 14
#define ELF_DT_RPATH 15
#define ELF_DT_SYMBOLIC 16
#define ELF_DT_JMPREL 23
#define ELF_DT_DEBUG 21
#define ELF_DT_TEXTREL 22
#define ELF_DT_BIND_NOW 26
#define ELF_DT_RUNPATH 27
#define ELF_DT_RELR 36
#define ELF_DT_RELRSZ 35
#define ELF_DT_RELRENT 37
#define ELF_DT_LOPROC 0x70000000
#define ELF_DT_HIPROC 0x7fffffff

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_GOT32 3
#define R_X86_64_PLT32 4
#define R_X86_64_COPY 5
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_GOTPCREL 9

#define R_INTERNAL_REL 0xfffffff0

#define ELF_SIGNATURE 0x464C457F
#define ELF_ELF64 2

#define ELF_EI_CLASS 4
#define ELF_EI_DATA 5
#define ELF_EI_VERSION 6
#define ELF_EI_OSABI 7

#define ELF_EI_SYSTEM_V 0
#define ELF_EI_LINUX 3

#define ELF_LITTLE_ENDIAN 1
#define ELF_MACH_X86_64 62

#define ELF_AT_ENTRY 9
#define ELF_AT_PHDR 3
#define ELF_AT_PHENT 4
#define ELF_AT_PHNUM 5

#define ELF_PT_NULL 0
#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2
#define ELF_PT_INTERP 3
#define ELF_PT_NOTE 4
#define ELF_PT_SHLIB 5
#define ELF_PT_PHDR 6
#define ELF_PT_LTS 7
#define ELF_PT_LOOS 0x60000000
#define ELF_PT_HOIS 0x6fffffff
#define ELF_PT_LOPROC 0x70000000
#define ELF_PT_HIPROC 0x7fffffff

#define ELF_SHN_UNDEF 0
#define ELF_STB_WEAK 2
#define ELF_SHT_RELA 4

struct aux {
	uint64_t at_phnum;
	uint64_t at_phent;
	uint64_t at_phdr;
	uint64_t at_entry;
};

struct elf64_hdr {
	uint8_t ident[16];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint64_t entry;
	uint64_t phoff;
	uint64_t shoff;
	uint32_t flags;
	uint16_t hdr_size;
	uint16_t phdr_size;
	uint16_t ph_num;
	uint16_t shdr_size;
	uint16_t sh_num;
	uint16_t shstrndx;
} __attribute__((packed));

struct elf64_phdr {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
} __attribute__((packed));

struct elf64_shdr {
	uint32_t sh_name;
	uint32_t sh_type;
	uint64_t sh_flags;
	uint64_t sh_addr;
	uint64_t sh_offset;
	uint64_t sh_size;
	uint32_t sh_link;
	uint32_t sh_info;
	uint64_t sh_addr_align;
	uint64_t sh_entsize;
} __attribute__((packed));

struct elf64_symtab {
	uint32_t st_name;
	unsigned char st_info;
	unsigned char st_other;
	uint16_t st_shndx;
	uint64_t st_value;
	uint64_t st_size;
} __attribute__((packed));

struct elf64_rela {
	uint64_t r_offset;
	uint64_t r_info;
	uint64_t r_addend;
};

struct elf64_dyn {
	uint64_t d_tag;
	uint64_t d_un;
};

struct elf64_file {
	struct elf64_hdr *hdr;

	struct elf64_phdr *phdr;
	struct elf64_shdr *shdr;
	struct elf64_shdr *shstrtab_hdr;
	struct elf64_shdr *strtab_hdr;
	struct elf64_shdr *symtab_hdr;

	struct aslr *aslr;
	struct aslr_layout *aslr_layout;

	struct {
		size_t rel_offset;
		size_t rel_size;
		size_t rel_ent;
		size_t rela_offset;
		size_t rela_size;
		size_t rela_ent;
		size_t symtab_offset;
		size_t symtab_ent;
		size_t pltrel;
		size_t pltrel_size;
		size_t jmprel;
	} dynamic;

	const char *strtab;

	struct {
		void *buffer;
		int length;
	} data;

	struct aux aux;

	struct page_table *page_table;
};

int elf64_file_init(struct elf64_file *file);
int elf64_file_load(struct elf64_file *file);
int elf64_file_runtime(struct elf64_file *file, char **runtime_path);
int elf64_file_aux(struct elf64_file *file, struct aux *aux);

#endif

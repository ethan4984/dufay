#ifndef X86_PAGING_H_
#define X86_PAGING_H_

#include <stdint.h>

#define X86_FLAGS_P (1 << 0)
#define X86_FLAGS_RW (1 << 1)
#define X86_FLAGS_US (1 << 2)
#define X86_FLAGS_PWT (1 << 3)
#define X86_FLAGS_PCD (1 << 4)
#define X86_FLAGS_A (1 << 5)
#define X86_FLAGS_D (1 << 6)
#define X86_FLAGS_PS (1 << 7)
#define X86_FLAGS_G (1 << 8)
#define X86_FLAGS_NX (1ull << 63)

#define X86_PAT_UC 0
#define X86_PAT_WC 1
#define X86_PAT_WT 4
#define X86_PAT_WP 5
#define X86_PAT_WB 6
#define X86_PAT_UCM 7

struct pmap {
	uint64_t *pmlt;
};

void amd64_paging_init();

#endif

#pragma once

#include <limine.h>

void pmm_init(void);
uint64_t pmm_alloc(uint64_t cnt, uint64_t align);
void pmm_free(uint64_t base, uint64_t cnt);

extern volatile struct limine_memmap_request limine_memmap_request;

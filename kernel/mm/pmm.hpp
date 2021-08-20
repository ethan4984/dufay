#ifndef PMM_HPP_
#define PMM_HPP_

#include <stivale.hpp>
#include <memutils.hpp>
#include <cpu.hpp>

namespace pmm {

inline size_t total_mem = 0;
inline size_t total_used_mem = 0;

void init(stivale *stivale);
size_t alloc(size_t cnt, size_t align = 1);
size_t calloc(size_t cnt, size_t align = 1);
void free(size_t base, size_t cnt);

}

#endif

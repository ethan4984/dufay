#include <mm/vmm.hpp>
#include <mm/pmm.hpp>
#include <cpu.hpp>
#include <debug.hpp>
#include <mm/slab.hpp>
#include <types.hpp>
#include <mm/mmap.hpp>

namespace vmm {

pml5_table::virtual_address::virtual_address(uint64_t *pml5_raw, uint64_t vaddr, uint64_t paddr) :
    _pml5e(NULL),
    _pml4e(NULL),
    _pml3e(NULL),
    _pml2e(NULL),
    _pml1e(NULL),
    vaddr(vaddr),
    paddr(paddr),
    pml5_raw(pml5_raw) {
    compute_indices();
}

pml4_table::virtual_address::virtual_address(uint64_t *pml4_raw, uint64_t vaddr, uint64_t paddr) : 
    _pml4e(NULL),
    _pml3e(NULL),
    _pml2e(NULL),
    _pml1e(NULL),
    vaddr(vaddr),
    paddr(paddr),
    pml4_raw(pml4_raw) {
    compute_indices();
}

pml5_table::virtual_address::virtual_address(uint64_t *pml5_raw, uint64_t vaddr) :
    _pml5e(NULL),
    _pml4e(NULL),
    _pml3e(NULL),
    _pml2e(NULL),
    _pml1e(NULL),
    vaddr(vaddr),
    paddr(0),
    pml5_raw(pml5_raw) {
    compute_indices();

    if(!pml5_raw[pml5_idx]) 
        return;

    _pml5e = new pml5e(&pml5_raw[pml5_idx]);
    if(!_pml5e->is_present())
        return;

    pml4_raw = reinterpret_cast<uint64_t*>((pml5_raw[pml5_idx] & ~(0xfff)) + high_vma);

    if(!pml4_raw[pml4_idx])
        return;

    _pml4e = new pml4e(&pml4_raw[pml4_idx]);
    if(!_pml4e->is_present())
        return;

    pml3_raw = reinterpret_cast<uint64_t*>((pml4_raw[pml4_idx] & ~(0xfff)) + high_vma);

    if(!pml3_raw[pml3_idx])
        return;

    _pml3e = new pml3e(&pml3_raw[pml3_idx]);

    if(_pml3e->is_huge()) {
        page_length = 0x40000000;
        return;
    } 

    if(!_pml3e->is_present())
        return;

    pml2_raw = reinterpret_cast<uint64_t*>((pml3_raw[pml3_idx] & ~(0xfff)) + high_vma);

    if(!pml2_raw[pml2_idx])
        return;

    _pml2e = new pml2e(&pml2_raw[pml2_idx]);

    if(_pml2e->is_huge()) {
        page_length = 0x200000;
        return;
    }

    if(!_pml2e->is_present())
        return;

    pml1_raw = reinterpret_cast<uint64_t*>((pml2_raw[pml2_idx] & ~(0xfff)) + high_vma);

    if(!pml1_raw[pml1_idx])
        return;

    _pml1e = new pml1e(&pml1_raw[pml1_idx]);

    page_length = 0x1000;
}

pml4_table::virtual_address::virtual_address(uint64_t *pml4_raw, uint64_t vaddr) : 
    _pml4e(NULL),
    _pml3e(NULL),
    _pml2e(NULL),
    _pml1e(NULL),
    vaddr(vaddr),
    paddr(0),
    pml4_raw(pml4_raw) {
    compute_indices();

    if(!pml4_raw[pml4_idx])
        return;

    _pml4e = new pml4e(&pml4_raw[pml4_idx]);
    if(!_pml4e->is_present()) 
        return;

    pml3_raw = reinterpret_cast<uint64_t*>((pml4_raw[pml4_idx] & ~(0xfff)) + high_vma);

    if(!pml3_raw[pml3_idx])
        return;

    _pml3e = new pml3e(&pml3_raw[pml3_idx]);

    if(_pml3e->is_huge()) {
        page_length = 0x40000000;
        return;
    }

    if(!_pml3e->is_present())
        return;

    pml2_raw = reinterpret_cast<uint64_t*>((pml3_raw[pml3_idx] & ~(0xfff)) + high_vma);

    if(!pml2_raw[pml2_idx])
        return;

    _pml2e = new pml2e(&pml2_raw[pml2_idx]);
   
    if(_pml2e->is_huge()) { 
        page_length = 0x200000; 
        return;
    }

    if(!_pml2e->is_present())
        return;

    pml1_raw = reinterpret_cast<uint64_t*>((pml2_raw[pml2_idx] & ~(0xfff)) + high_vma);

    if(!pml1_raw[pml1_idx])
        return;

    _pml1e = new pml1e(&pml1_raw[pml1_idx]);

    page_length = 0x1000;
}

void pml4_table::virtual_address::map(uint64_t pml4_flags, uint64_t pml3_flags, uint64_t pml2_flags, uint64_t pml1_flags, ssize_t pa) {
    if(pml4_raw == NULL)
        return;

    if(!pml4_raw[pml4_idx]) {
        if(!pml4_flags)
            return;
        pml4_raw[pml4_idx] = pmm::calloc(1) | pml4_flags;
    }

    _pml4e = new pml4e(&pml4_raw[pml4_idx]);
    pml3_raw = reinterpret_cast<uint64_t*>((pml4_raw[pml4_idx] & ~(0xfff)) + high_vma);
    if(!pml3_raw[pml3_idx]) {
        if(!pml3_flags)
            return;

        if(pml3_flags & (1 << 7)) { // PS
            pml3_raw[pml3_idx] = paddr | pml3_flags; 
        } else {
            pml3_raw[pml3_idx] = pmm::calloc(1) | pml3_flags; 
        }
    }

    _pml3e = new pml3e(&pml3_raw[pml3_idx]);
    if(_pml3e->is_huge()) {
        page_length = 0x40000000;
        if(pa != -1)
            _pml3e->set_pa(pa);
        return;
    }

    pml2_raw = reinterpret_cast<uint64_t*>((pml3_raw[pml3_idx] & ~(0xfff)) + high_vma);

    if(!pml2_raw[pml2_idx]) {
        if(!pml2_flags)
            return;

        if(pml2_flags & (1 << 7)) { // PS
            pml2_raw[pml2_idx] = paddr | pml2_flags; 
        } else {
            pml2_raw[pml2_idx] = pmm::calloc(1) | pml2_flags; 
        }
    }

    _pml2e = new pml2e(&pml2_raw[pml2_idx]);
    if(_pml2e->is_huge()) {
        page_length = 0x200000;
        if(pa != -1)
            _pml2e->set_pa(pa);
        return;
    }

    pml1_raw = reinterpret_cast<uint64_t*>((pml2_raw[pml2_idx] & ~(0xfff)) + high_vma);

    if(!pml1_raw[pml1_idx]) {
        if(!pml1_flags)
            return;

        pml1_raw[pml1_idx] = paddr | pml1_flags;
    }

    _pml1e = new pml1e(&pml1_raw[pml1_idx]);
    page_length = 0x1000;

    if(pa != -1)
        _pml1e->set_pa(pa);
}

uint64_t pml4_table::virtual_address::unmap() {
    if(_pml3e->is_huge()) {
        uint64_t paddr = _pml3e->get_base();
        _pml3e->set_present(0);
        return paddr;
    }

    if(_pml2e->is_huge()) {
        uint64_t paddr = _pml2e->get_base();
        _pml2e->set_present(0);
        return paddr;
    }

    uint64_t paddr = _pml1e->get_base();
    _pml1e->set_present(0);
    return paddr;
}

void pml5_table::virtual_address::map(uint64_t pml5_flags, uint64_t pml4_flags, uint64_t pml3_flags, uint64_t pml2_flags, uint64_t pml1_flags, ssize_t pa) {
    if(pml5_raw == NULL)
        return;

    if(!pml5_raw[pml5_idx]) {
        if(!pml5_flags)
            return;
        pml5_raw[pml5_idx] = pmm::calloc(1) | pml5_flags;
    }

    _pml5e = new pml5e(&pml5_raw[pml5_idx]);
    pml4_raw = reinterpret_cast<uint64_t*>((pml5_raw[pml5_idx] & ~(0xfff)) + high_vma);

    if(!pml4_raw[pml4_idx]) {
        if(!pml4_flags)
            return;
        pml4_raw[pml4_idx] = pmm::calloc(1) | pml4_flags;
    }

    _pml4e = new pml4e(&pml4_raw[pml3_idx]);
    pml3_raw = reinterpret_cast<uint64_t*>((pml4_raw[pml4_idx] &  ~(0xfff)) + vmm::high_vma);

    if(!pml3_raw[pml3_idx]) {
        if(!pml3_flags)
            return;

        if(pml3_flags & (1 << 7)) { // PS
            pml3_raw[pml3_idx] = paddr | pml3_flags;
        } else {
            pml3_raw[pml3_idx] = pmm::calloc(1) + pml3_flags;
        }
    } 

    _pml3e = new pml3e(&pml3_raw[pml3_idx]);
    if(_pml3e->is_huge()) {
        page_length = 0x40000000;
        if(pa != -1)
            _pml3e->set_pa(pa);
        return;
    }
    
    pml2_raw = reinterpret_cast<uint64_t*>((pml3_raw[pml3_idx] & ~(0xfff)) + high_vma);

    if(!pml2_raw[pml2_idx]) {
        if(!pml2_flags)
            return;

        if(pml2_flags & (1 << 7)) { // PS
            pml2_raw[pml2_idx] = paddr | pml2_flags; 
        } else {
            pml2_raw[pml2_idx] = pmm::calloc(1) | pml2_flags; 
        }
    }

    _pml2e = new pml2e(&pml2_raw[pml2_idx]);
    
    if(_pml2e->is_huge()) {
        page_length = 0x200000;
        if(pa != -1)
            _pml2e->set_pa(pa);
        return;
    }

    pml1_raw = reinterpret_cast<uint64_t*>((pml2_raw[pml2_idx] & ~(0xfff)) + high_vma);

    if(!pml1_raw[pml1_idx]) {
        if(!pml1_flags)
            return;

        pml1_raw[pml1_idx] = paddr | pml1_flags;
    }

    _pml1e = new pml1e(&pml1_raw[pml1_idx]);

    page_length = 0x1000;
    if(pa != -1)
        _pml1e->set_pa(pa);
}

uint64_t pml5_table::virtual_address::unmap() {
    if(_pml3e->is_huge()) {
        uint64_t paddr = _pml3e->get_base();
        _pml3e->set_present(0);
        return paddr;
    }

    if(_pml2e->is_huge()) {
        uint64_t paddr = _pml2e->get_base();
        _pml2e->set_present(0);
        return paddr;
    }

    uint64_t paddr = _pml1e->get_base();
    _pml1e->set_present(0);
    return paddr;
}

void pml4_table::map_range(uint64_t vaddr, size_t cnt, size_t flags, ssize_t pa) {
    spin_lock(&lock);

    if(flags & (1 << 7)) {
        for(size_t i = 0; i < cnt; i++) {
            virtual_address new_addr(highest_raw, vaddr, pmm::alloc(1));
            new_addr.map(0x3, 0x3, flags, 0, pa);
            vaddr += 0x200000;
        }
    } else {
        for(size_t i = 0; i < cnt; i++) {
            virtual_address new_addr(highest_raw, vaddr, pmm::alloc(1));
            new_addr.map(0x3, 0x3, 0x3, flags, pa);
            vaddr += 0x1000;
        }
    }

    spin_release(&lock);
}

void pml4_table::unmap_range(uint64_t vaddr, size_t cnt) {
    spin_lock(&lock);

    for(size_t i = 0; i < cnt; i++) {
        virtual_address addr(highest_raw, vaddr);
        if(!addr.page_length) {
            spin_release(&lock);
            return;
        }
        addr.unmap();
        vaddr += addr.page_length;
    }
    spin_release(&lock);
}

void pml5_table::map_range(uint64_t vaddr, size_t cnt, size_t flags, ssize_t pa) {
    spin_lock(&lock);

    size_t extra = 0;
    if(flags & (1 << 2))
        extra = (1 << 2);

    if(flags & (1 << 7)) {
        for(size_t i = 0; i < cnt; i++) {
            virtual_address new_addr(highest_raw, vaddr, pmm::calloc(1));
            new_addr.map(0x3 | extra, 0x3 | extra, 0x3 | extra, flags, 0, pa);
            vaddr += 0x200000;
        }
    } else {
        for(size_t i = 0; i < cnt; i++) {
            virtual_address new_addr(highest_raw, vaddr, pmm::calloc(1));
            new_addr.map(0x3 | extra, 0x3 | extra, 0x3 | extra, 0x3 | extra, flags, pa);
            vaddr += 0x1000;
        }
    }

    spin_release(&lock);
}

void pml5_table::unmap_range(uint64_t vaddr, size_t cnt) {
    spin_lock(&lock);

    for(size_t i = 0; i < cnt; i++) {
        virtual_address addr(highest_raw, vaddr);
        if(!addr.page_length) {
            spin_release(&lock);
            return;
        }
        addr.unmap();
        vaddr += addr.page_length;
    }

    spin_release(&lock);
}

void pml4_table::map_page(uint64_t vaddr, uint64_t flags, ssize_t pa) {
    spin_lock(&lock);

    if(flags & (1 << 7)) {
        virtual_address addr(highest_raw, vaddr, pmm::calloc(0x200));
        addr.map(0x3, 0x3, flags, 0, pa);
    } else {
        virtual_address addr(highest_raw, vaddr, pmm::calloc(1));
        addr.map(0x3, 0x3, 0x3, flags, pa);
    }

    spin_release(&lock);

    tlb_flush();
}

void pml4_table::map_page_raw(uint64_t vaddr, uint64_t paddr, uint64_t flags1, uint64_t flags0, ssize_t pa) {
    spin_lock(&lock);

    if(flags0 & (1 << 7)) {
        virtual_address addr(highest_raw, vaddr, paddr);
        addr.map(flags1, flags1, flags0, 0, pa);
    } else {
        virtual_address addr(highest_raw, vaddr, paddr);
        addr.map(flags1, flags1, flags1, flags0, pa);
    }

    spin_release(&lock);

    tlb_flush();
}

void pml4_table::unmap_page(uint64_t vaddr) {
    spin_lock(&lock);

    virtual_address addr(highest_raw, vaddr);

    if(!addr.page_length) {
        spin_release(&lock);
        return;
    }

    addr.unmap();

    spin_release(&lock);
}

void pml5_table::map_page(uint64_t vaddr, uint64_t flags, ssize_t pa) {
    spin_lock(&lock);

    if(flags & (1 << 7)) {
        virtual_address addr(highest_raw, vaddr, pmm::calloc(0x200));
        addr.map(0x3, 0x3, 0x3, flags, 0, pa);
    } else {
        virtual_address addr(highest_raw, vaddr, pmm::calloc(1));
        addr.map(0x3, 0x3, 0x3, 0x3, flags, pa);
    }

    spin_release(&lock);

    tlb_flush();
}

void pml5_table::map_page_raw(uint64_t vaddr, uint64_t paddr, uint64_t flags1, uint64_t flags0, ssize_t pa) {
    spin_lock(&lock);

    if(flags0 & (1 << 7)) {
        virtual_address addr(highest_raw, vaddr, paddr);
        addr.map(flags1, flags1, flags1, flags0, 0, pa);
    } else {
        virtual_address addr(highest_raw, vaddr, paddr);
        addr.map(flags1, flags1, flags1, flags1, flags0, pa);
    }

    spin_release(&lock);

    tlb_flush();
}

void pml5_table::unmap_page(uint64_t vaddr) {
    spin_lock(&lock);

    virtual_address addr(highest_raw, vaddr);

    if(!addr.page_length) {
        spin_release(&lock);
        return;
    }

    addr.unmap();

    spin_release(&lock);
}

ssize_t set_pat() {
    cpuid_state cpu_state = cpuid(1, 0); 

    if(!(cpu_state.rdx & (1 << 16)))
        return -1;

    uint64_t pat = rdmsr(pat_msr);

    uint8_t *pat_arr = reinterpret_cast<uint8_t*>(&pat); 

    pat_arr[pa_uc] = uc;
    pat_arr[pa_wc] = wc;
    pat_arr[pa_wt] = wt;
    pat_arr[pa_wp] = wp;
    pat_arr[pa_wb] = wb;
    pat_arr[pa_ucm] = ucm;

    wrmsr(pat_msr, pat);

    return 0;
}

pmlx_table *pml4_table::create_generic() {
    pmlx_table *table = new pml4_table;
    table->highest_raw = reinterpret_cast<uint64_t*>(pmm::calloc(1) + high_vma);

    table->highest_raw[256] = kernel_mapping->highest_raw[256];
    table->highest_raw[511] = kernel_mapping->highest_raw[511];

    return table;
}

pmlx_table *pml5_table::create_generic() {
    pmlx_table *table = new pml5_table;
    table->highest_raw = reinterpret_cast<uint64_t*>(pmm::calloc(1) + high_vma);

    table->highest_raw[256] = kernel_mapping->highest_raw[256];
    table->highest_raw[511] = kernel_mapping->highest_raw[511];

    return table;
}

pmlx_table *pmlx_table::duplicate() {
    pmlx_table *new_table = create_generic();

    for(size_t i = 0; i < mmap.region_list.size(); i++) {
        region &region_cur = mmap.region_list[i];
        auto page_cnt = div_roundup(region_cur.limit, vmm::page_size);
        auto base = region_cur.base;

        for(size_t i = 0; i < page_cnt; i++) {
            uintptr_t phys_addr = pmm::calloc(1);
            memcpy64((uint64_t*)(phys_addr + vmm::high_vma), (uint64_t*)base, vmm::page_size / 8);
            new_table->map_page_raw(base, phys_addr, region_cur.prot, region_cur.prot, -1);
            base += vmm::page_size; 
        }

        new_table->map_range(region_cur.base, page_cnt, region_cur.prot, -1);
    }

    new_table->mmap = mmap;

    return new_table;
}

void init() {
    cpuid_state cpu_id = cpuid(7, 0);

    uint64_t *pml_highest = reinterpret_cast<uint64_t*>(pmm::calloc(1) + high_vma);

    if(cpu_id.rcx & (1 << 16)) {
        kernel_mapping = new pml5_table(pml_highest);
    } else {
        kernel_mapping = new pml4_table(pml_highest);
    }
    
    size_t phys = 0;
    for(size_t i = 0; i < 0x200; i++) {
        kernel_mapping->map_page_raw(phys + kernel_high_vma, phys, 0x3, 0x3 | (1 << 7) | (1 << 8), -1);
        phys += 0x200000;
    }

    phys = 0;
    for(size_t i = 0; i < pmm::total_mem / 0x200000; i++) {
        kernel_mapping->map_page_raw(phys + high_vma, phys, 0x3, 0x3 | (1 << 7) | (1 << 8), -1);
        phys += 0x200000;
    }

    set_pat();

    kernel_mapping->init();
}

}

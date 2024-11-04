#include <arch/x86/cpu.h>

#include <core/physical.h>
#include <core/virtual.h>
#include <fayt/slab.h>
#include <core/scheduler.h>
#include <core/server.h>

#include <core/debug.h>
#include <fayt/string.h>

#include <acpi/rsdp.h> 
#include <acpi/madt.h>

#include <limine.h>

//	TODO
//	More rigorous panic messages (most important thing is a register dump along with the function and line number) also shove them everywhere

struct limine_hhdm_request limine_hhdm_request = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0
};

static volatile struct limine_rsdp_request limine_rsdp_request = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0
};

static void *spalloc(void*, uint64_t s) { return (void*)pmm_alloc(s, 1) + HIGH_VMA; }
static void spfree(void *addr, uint64_t s, uint64_t) { pmm_free((uint64_t)addr - HIGH_VMA, s); }

void dufay_entry(void) {
	if(limine_hhdm_request.response) HIGH_VMA = limine_hhdm_request.response->offset;

	print("dufay: init\n");

	x86_system_init();

	pmm_init();
	vmm_init();

	struct slab_pool pool = {
		.page_size = PAGE_SIZE,
		.page_alloc = spalloc,
		.page_free = spfree
	};

	slab_cache_create(&pool, "CACHE32", 32);
	slab_cache_create(&pool, "CACHE64", 64);
	slab_cache_create(&pool, "CACHE128", 128);
	slab_cache_create(&pool, "CACHE256", 256);
	slab_cache_create(&pool, "CACHE512", 512);
	slab_cache_create(&pool, "CACHE1024", 1024);
	slab_cache_create(&pool, "CACHE2048", 2048);
	slab_cache_create(&pool, "CACHE4096", 4096);
	slab_cache_create(&pool, "CACHE8192", 8192);
	slab_cache_create(&pool, "CACHE16384", 16384);
	slab_cache_create(&pool, "CACHE32768", 32768);
	slab_cache_create(&pool, "CACHE65536", 65536);

	rsdp = limine_rsdp_request.response->address;

	if(rsdp->xsdt_addr) {
		xsdt = (struct xsdt*)(rsdp->xsdt_addr + HIGH_VMA);
		print("acpi: xsdt found at %x\n", (uintptr_t)xsdt);
	} else {
		rsdt = (struct rsdt*)(rsdp->rsdt_addr + HIGH_VMA);
		print("acpi: rsdt found at %x\n", (uintptr_t)rsdt);
	}

	fadt = acpi_find_sdt("FACP");

	x86_system_tables();

	launch_servers();

	__asm__ ("sti"); 

	for(;;) __asm__ ("hlt");
}

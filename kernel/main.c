#include <arch/x86/cpu.h>

#include <core/debug.h>
#include <core/message.h>
#include <core/init.h>
#include <core/mm/physical.h>
#include <core/scheduler.h>
#include <core/mm/virtual.h>

#include <acpi/madt.h>
#include <acpi/rsdp.h>

#include <fayt/slab.h>
#include <fayt/string.h>

#include <limine.h>

struct limine_hhdm_request limine_hhdm_request = { .id = LIMINE_HHDM_REQUEST,
												   .revision = 0 };

static volatile struct limine_rsdp_request limine_rsdp_request = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0
};

static void *spalloc(void *, uint64_t s)
{
	return (void *)pmm_alloc(s, 1) + HIGH_VMA;
}
static void spfree(void *addr, uint64_t s, uint64_t)
{
	pmm_free((uint64_t)addr - HIGH_VMA, s);
}

#include <arch/x86/hpet.h>
#include <fayt/time.h>

void dufay_entry(void)
{
	if (limine_hhdm_request.response)
		HIGH_VMA = limine_hhdm_request.response->offset;

	print("welcome\n");

	x86_system_init();

	pmm_init();

	struct slab_pool pool = { .page_size = PAGE_SIZE,
							  .page_alloc = spalloc,
							  .page_free = spfree };

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
	slab_cache_create(&pool, "CACHE131072", 131072);

	vmm_init();

	rsdp = limine_rsdp_request.response->address;

	if (rsdp->xsdt_addr) {
		xsdt = (struct xsdt *)(rsdp->xsdt_addr + HIGH_VMA);
		print("ACPI: xsdt found at %x\n", (uintptr_t)xsdt);
	} else {
		rsdt = (struct rsdt *)(rsdp->rsdt_addr + HIGH_VMA);
		print("ACPI: rsdt found at %x\n", (uintptr_t)rsdt);
	}

	fadt = acpi_find_sdt("FACP");

	x86_system_tables();

	message_init();
	launch_init();

	__asm__("sti");

	for (;;)
		__asm__("hlt");
}

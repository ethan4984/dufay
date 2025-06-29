#include <aria/lock.h>
#include <arch/port.h>

#include <core/cpu.h>
#include <core/debug.h>
#include <core/message.h>
#include <core/init.h>
#include <mm/physical.h>
#include <mm/virtual.h>
#include <core/ipl.h>
#include <mm/slab.h>

#include <acpi/madt.h>
#include <acpi/rsdp.h>

#include <aria/slab.h>
#include <aria/debug.h>
#include <aria/base.h>

#include <limine.h>
#include <core/mutex.h>

static void *spalloc(void *, uint64_t s)
{
	return (void *)P2V(pmm_alloc(s, 1));
}
static void spfree(void *addr, uint64_t s, uint64_t)
{
	pmm_free(V2P(addr), s);
}

int init_system_tgroup();

void do_sync_test(void);

void sched_init();
void sched_cpu_init();

void fuga_entry(void)
{
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

	arch_devices_init();

	kmem_init();

	//init_system_tgroup();

	int ret = message_init();
	if (ret == -1) {
		REPORT_ERROR;
		panic("message init failed!");
	}

	sched_init();
	sched_cpu_init();

#if 1
	do_sync_test();
#else
	ret = launch_init();
	if (ret == -1) {
		REPORT_ERROR;
		panic("");
	}
#endif

	/* Liftoff! */
	ipl_lower(IPL_ZERO);

	for (;;) {
		arch_halt();
	}
}

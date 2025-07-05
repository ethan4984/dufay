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

struct thread *make_kernel_thread(void (*fn)(void *));

void idle_thread(void *);

static void main_threaded(void *)
{
	do_sync_test();

	idle_thread(NULL);
	sched_wait();
}

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

	kmem_early_init();

	vmm_init();

	sched_init();

	arch_devices_init();

	kmem_init();

	sched_cpu_init();

	//init_system_tgroup();

	int ret = message_init();
	if (ret == -1) {
		REPORT_ERROR;
		panic("message init failed!");
	}

	struct thread *main_thread = make_kernel_thread(main_threaded);

	//	CORE_LOCAL->idle_thread = *main_thread;
	CORE_LOCAL->current_thread = main_thread;

	/* Liftoff! */
	ipl_lower(IPL_ZERO);
	arch_enable_interrupts();

	arch_load_context(main_thread);

	panic("Should not happen %d???\n", CORE_LOCAL->sched_data.load);

	for (;;) {
	}

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
	arch_enable_interrupts();

	panic("Should not happen %d???\n", CORE_LOCAL->sched_data.load);

	for (;;) {
		arch_halt();
	}
}

#include <aria/address.h>
#include <aria/syscall.h>
#include <aria/debug.h>
#include <aria/portal.h>
#include <aria/stream.h>
#include <aria/notification.h>
#include <aria/string.h>
#include <aria/slab.h>
#include <aria/rb_tree.h>
#include <aria/pci.h>
#include <aria/irq.h>

#include <fs.h>

static void *spalloc(void *, uint64_t);
static void spfree(void *, uint64_t, uint64_t);

int main(struct pci_info *pci_info)
{
	print("Booting fs server\n");

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

	print("Slab cache directory initialised\n");

	constexpr int NOTIFICATION_STACK_SIZE = 0x10000;
	for (int i = 0; i < 16; i++) {
		uintptr_t addr;
		int ret = as_vmem_allocate(CAPABILITY_SELF_AS, &addr,
								   NOTIFICATION_STACK_SIZE);
		if (ret == -1) {
			print("ERROR: failed to allocate address for stack\n");
			goto failure;
		}

		struct syscall_response response =
			SYSCALL2(SYSCALL_NOTIFICATION_DEFINE_STACK,
					 addr + NOTIFICATION_STACK_SIZE, NOTIFICATION_STACK_SIZE);

		if (response.ret == -1) {
			print("ERROR: failed to allocate notification stack\n");
		}
	}

	int ret = fs(pci_info);
	if (ret == -1) {
		print("ERROR: internal critical failure\n");
		goto failure;
	}
failure:
	for (;;)
		;
}

#include <stdarg.h>

static void log_write(struct stream_info *, char c)
{
	SYSCALL1(SYSCALL_LOG, c);
}
static struct stream_info print_stream = { .write = log_write };

void print(const char *str, ...)
{
	va_list arg;
	va_start(arg, str);

	const char *prefix = "DUFAY: [FS] ";
	for (; *prefix;) {
		print_stream.write(&print_stream, *prefix);
		prefix++;
	}

	stream_print(&print_stream, str, arg);

	va_end(arg);
}

void panic(const char *str, ...)
{
	print("PANIC [ ");

	va_list arg;
	va_start(arg, str);

	stream_print(&print_stream, str, arg);

	va_end(arg);

	print(" ]\n");

	for (;;)
		;
}

static void *spalloc(void *, uint64_t s)
{
	uintptr_t addr;

	int ret = as_mem_allocate(CAPABILITY_SELF_AS, &addr, s * PAGE_SIZE);
	if (ret == -1)
		return NULL;

	return (void *)addr;
}

static void spfree(void *, uint64_t, uint64_t)
{
}

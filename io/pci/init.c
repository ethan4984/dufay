#include <fayt/address_space.h>
#include <fayt/syscall.h>
#include <fayt/debug.h>
#include <fayt/portal.h>
#include <fayt/stream.h>
#include <fayt/slab.h>
#include <fayt/rb_tree.h>

#include <pci.h>

static void *spalloc(void *, uint64_t);
static void spfree(void *, uint64_t, uint64_t);

int main(struct pci_server_meta *server_meta)
{
	print("Booting PCI server\n");

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

	print("Slab cache directory initialised\n");

	constexpr int NOTIFICATION_STACK_SIZE = 0x10000;
	for (int i = 0; i < 16; i++) {
		uintptr_t addr;
		int ret = as_allocate(&address_space, &addr, NOTIFICATION_STACK_SIZE);
		if (ret == -1) {
			print("ERROR: failed to allocate address for stack\n");
			goto failure;
		}

		struct syscall_response response =
			SYSCALL2(SYSCALL_NOTIFICATION_DEFINE_STACK,
					 addr + NOTIFICATION_STACK_SIZE, NOTIFICATION_STACK_SIZE);

		if (response.ret == -1) {
			print("ERROR: failed to allocate notification stack\n");
			goto failure;
		}
	}

	int ret = pci(server_meta);
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

	const char *prefix = "DUFAY: [PCI] ";
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

struct address_space address_space = { .current = 0xa0000000,
									   .base = 0xa0000000,
									   .limit = 0x0000fffffffff0ff };

static void *spalloc(void *, uint64_t s)
{
	uintptr_t addr;

	int ret = as_allocate(&address_space, &addr, s * PAGE_SIZE);
	if (ret == -1)
		return NULL;

	return (void *)addr;
}

static void spfree(void *, uint64_t, uint64_t)
{
}

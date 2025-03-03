#include <fayt/address_space.h>
#include <fayt/debug.h>
#include <fayt/portal.h>
#include <fayt/rb_tree.h>
#include <fayt/slab.h>
#include <fayt/stream.h>
#include <fayt/syscall.h>
#include <fayt/message.h>

static void *spalloc(void *, uint64_t);
static void spfree(void *, uint64_t, uint64_t);

int main()
{
	print("Bootstrapping\n");

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

	const char *prefix = "DUFAY: [INIT] ";
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

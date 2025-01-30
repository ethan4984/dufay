#include <fayt/address_space.h>
#include <fayt/syscall.h>
#include <fayt/debug.h>
#include <fayt/portal.h>
#include <fayt/stream.h>
#include <fayt/slab.h>
#include <fayt/rb_tree.h>

#include <sched.h>

static void *spalloc(void*, uint64_t);
static void spfree(void*, uint64_t, uint64_t);

int main(struct sched_descriptor *desc) {
	print("Booting server [processor_id=%x]\n", desc->processor_id);

	if(desc->timer.source == TIME_SOURCE_INVARIANT_TSC) {
		print("Using invariant TSC as timer [freq: %d]\n", desc->timer.freq);
		desc->timer.read = invariant_tsc_read;
	} else if(desc->timer.source == TIME_SOURCE_HPET) {
		print("Using HPET as timer [unsupported]\n");
		goto failure;
	} else {
		print("Timer source unknown\n");
		goto failure;
	}

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

	print("Slab cache directory initialised\n");

	constexpr int NOTIFICATION_STACK_SIZE = 0x10000;
	for(int i = 0; i < 4; i++) {
		uintptr_t addr;
		int ret = as_allocate(&address_space, &addr, NOTIFICATION_STACK_SIZE);
		if(ret == -1) { print("ERROR: failed to allocate address for stack\n"); goto failure; }

		struct syscall_response response = SYSCALL2(SYSCALL_NOTIFICATION_DEFINE_STACK,
			addr + NOTIFICATION_STACK_SIZE, NOTIFICATION_STACK_SIZE);

		if(response.ret == -1) print("ERROR: failed to allocate notification stack\n");
		else print("Allocated notificaton stack #%d [%x:%x]\n",
			i, addr, NOTIFICATION_STACK_SIZE);
	}

	uintptr_t addr;
	int ret = as_allocate(&address_space, &addr, 0x10000);
	if(ret == -1) { print("ERROR: failed to allocate address\n"); goto failure; }

	struct portal_resp portal_resp;
	struct portal_req portal_req = {
		.type = PORTAL_REQ_SHARE,
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.length = sizeof(struct portal_req),
		.share = {
			.identifier = "SCHEDULER CORE0",
			.length = sizeof(struct sched_queue_entry),
			.create = 0,
			.type = LINK_CIRCULAR
		},
		.morphology = {
			.addr = addr,
			.length = 0x10000 
		}
	};

	struct syscall_response response = SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
	if(response.ret == -1) { print("ERROR: failed to estabilish link with kernel\n"); goto failure; }

	struct portal_link *link = (void*)portal_resp.base;
	print("Link with kernel has been stablished [%x]\n", link);

	ret = sched(link, desc);
	if(ret == -1) { print("ERROR: critical failure\n"); goto failure; }
failure:
	for(;;);
}

#include <stdarg.h>

static void log_write(struct stream_info*, char c) { SYSCALL1(SYSCALL_LOG, c); }
static struct stream_info print_stream = {
	.write = log_write
};

void print(const char *str, ...) {
	va_list arg; 
	va_start(arg, str);

	const char *prefix = "DUFAY: [SCHED] "; 
	for(; *prefix;) {
		print_stream.write(&print_stream, *prefix);
		prefix++;
	}

	stream_print(&print_stream, str, arg);

	va_end(arg);
}

void panic(const char *str, ...) {
	print("PANIC [ ");

	va_list arg; 
	va_start(arg, str);

	stream_print(&print_stream, str, arg);

	va_end(arg);

	print(" ]\n");

	for(;;);
}

struct address_space address_space = {
	.current = 0xa0000000,
	.base = 0xa0000000,
	.limit = 0x0000fffffffff0ff
};

static void *spalloc(void*, uint64_t s) {
	uintptr_t addr;

	int ret = as_allocate(&address_space, &addr, s * PAGE_SIZE);
	if(ret == -1) return NULL;

	return (void*)addr;
}

static void spfree(void*, uint64_t, uint64_t) { }

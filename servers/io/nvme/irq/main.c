#include <fayt/syscall.h>
#include <fayt/debug.h>
#include <fayt/address_space.h>
#include <fayt/stream.h>
#include <fayt/string.h>

int nvme_irq_handle(void) {
	return 0;
}

#include <stdarg.h>

static void log_write(struct stream_info*, char c) { SYSCALL1(SYSCALL_LOG, c); }
static struct stream_info print_stream = {
	.write = log_write
};

void print(const char *str, ...) {
	va_list arg;
	va_start(arg, str);

	stream_print(&print_stream, str, arg);

	va_end(arg);
}

void panic(const char *str, ...) {
	print("DUFAY: NVME: PANIC < ");

	va_list arg;
	va_start(arg, str);

	stream_print(&print_stream, str, arg);

	va_end(arg);

	print(" >\n");

	for(;;);
}

struct address_space address_space = {
	.current = 0xa0000000,
	.base = 0xa0000000,
	.limit = 0x0000fffffffff0ff
};

#include <fayt/syscall.h>
#include <fayt/debug.h>
#include <fayt/address_space.h>
#include <fayt/stream.h>
#include <fayt/string.h>

#include <stdarg.h>

static void log_write(struct stream_info*, char c) { SYSCALL1(SYSCALL_LOG, c); }
static struct stream_info print_stream = {
	.write = log_write
};

void print(const char *str, ...) {
	va_list arg;
	va_start(arg, str);

	const char *prefix = "DUFAY: [NVME IRQ] "; 
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
	.base = 0xf0000000,
	.limit = 0x0000fffffffff0ff
};

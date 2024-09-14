#include <arch/x86/debug.h>

#include <core/syscall.h>
#include <core/debug.h>

#include <fayt/lock.h>
#include <fayt/string.h>
#include <fayt/stream.h>

#include <stdint.h> 
#include <stddef.h>
#include <stdarg.h>

static void print_write(struct stream_info*, char);

struct stream_info print_stream = {
	.write = print_write
};

SYSCALL_DEFINE1(log, char, character, ({
	print("%c", character);
}))

void print_unlocked(const char *str, ...) {
	va_list arg; 
	va_start(arg, str);

	stream_print(&print_stream, str, arg);

	va_end(arg);
}

void print(const char *str, ...) {
	va_list arg; 
	va_start(arg, str);

	spinlock(&print_stream.lock);
	stream_print(&print_stream, str, arg);
	spinrelease(&print_stream.lock);

	va_end(arg);
}

void panic(const char *str, ...) {
	print("KERNEL PANIC: < ");

	va_list arg;
	va_start(arg, str);

	spinlock(&print_stream.lock);
	stream_print(&print_stream, str, arg);
	
	va_end(arg);

	print_unlocked(" > HALTING\n");

	uint64_t rbp;
	asm volatile ("mov %%rbp, %0" : "=r"(rbp));
	stacktrace((void*)rbp);

	spinrelease(&print_stream.lock);

	for(;;) asm volatile ("cli\nhlt");
}

void stacktrace(uint64_t *rbp) {
	for(;;) {
		if(rbp == NULL) {
			return;
		}

		uint64_t previous_rbp = *rbp;
		rbp++;
		uint64_t return_address = *rbp;

		if(return_address == 0) {
			return;
		}

		print_unlocked("trace: [%x]\n", return_address);

		rbp = (void*)previous_rbp;
	}
}

static void print_write(struct stream_info*, char c) {
	serial_write(c);
}

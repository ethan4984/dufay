#include <core/syscall.h>
#include <core/debug.h>
#include <arch/port.h>

#include <aria/lock.h>
#include <aria/string.h>
#include <aria/stream.h>

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <core/lock.h>

static void print_write(struct stream_info *, char);

struct stream_info print_stream = { .write = print_write };

static struct spinlock print_lock;

SYSCALL_DEFINE1(log, char, character, ({
					spinlock_irqsave(&print_lock);
					print_stream.write(&print_stream, character);
					spinrelease_irqsave(&print_lock);
				}))

void print_unlocked(const char *str, ...)
{
	va_list arg;
	va_start(arg, str);

	stream_print(&print_stream, str, arg);

	va_end(arg);
}

void print(const char *str, ...)
{
	va_list arg;
	va_start(arg, str);

	spinlock_irqsave(&print_lock);
	const char *prefix = "DUFAY: [KERNEL] ";
	for (; *prefix;) {
		print_stream.write(&print_stream, *prefix);
		prefix++;
	}

	stream_print(&print_stream, str, arg);

	va_end(arg);

	spinrelease_irqsave(&print_lock);
}

void panic(const char *str, ...)
{
	print("KERNEL PANIC: < ");

	va_list arg;
	va_start(arg, str);

	stream_print(&print_stream, str, arg);

	va_end(arg);

	print_unlocked(" > HALTING\n");

	//	uint64_t rbp;
	//	__asm__ volatile ("mov %%rbp, %0" : "=r"(rbp));
	//	stacktrace((void*)rbp);

	for (;;) {
		arch_disable_interrupts();
		arch_halt();
	}
}

void stacktrace(uint64_t *rbp)
{
	for (;;) {
		if (rbp == NULL) {
			return;
		}

		uint64_t previous_rbp = *rbp;
		rbp++;
		uint64_t return_address = *rbp;

		if (return_address == 0) {
			return;
		}

		print_unlocked("trace: [%x]\n", return_address);

		rbp = (void *)previous_rbp;
	}
}

static void print_write(struct stream_info *, char c)
{
	arch_debug_write(c);
}

#include <arch/amd64/debug.h>
#include <arch/amd64/cpu.h>

static void serial_write(char data)
{
	for (;;) {
		if (inb(COM1 + 5) & (1 << 5)) {
			break;
		}
	}

	if (__builtin_expect(data == '\n', 0)) {
		outb(COM1, '\r');
	}

	outb(COM1, data);
}

void serial_init(void)
{
	outb(COM1 + 0x3, 0x80);
	outb(COM1 + 0x0, 0x0c);
	outb(COM1 + 0x1, 0x00);
	outb(COM1 + 0x3, 0x03);
	outb(COM1 + 0x2, 0xc7);
	outb(COM1 + 0x4, 0x00);
}

void arch_debug_write(char c)
{
	serial_write(c);
}
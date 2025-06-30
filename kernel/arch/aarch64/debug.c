#include <stdint.h>

void arch_debug_write(char c)
{
	*(uint8_t *)(0x09000000) = c;
}

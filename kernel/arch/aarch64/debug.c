#include <stdint.h>

void arch_debug_output(char c)
{
	*(uint8_t *)(0x09000000) = c;
}
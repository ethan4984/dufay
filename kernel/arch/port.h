#ifndef ARCH_PORT_H_
#define ARCH_PORT_H_

#if defined(__amd64__)
#include "amd64/port.h"
#elif defined(__aarch64__)
#include "aarch64/port.h"
#endif

void arch_debug_write(char c);
void arch_devices_init(void);

void arch_enable_interrupts(void);
void arch_disable_interrupts(void);
bool arch_interrupt_state(void);
void arch_set_hardware_ipl(ipl_t ipl);
void arch_halt(void);

void arch_context_init(struct arch_thread_context *context,
					   uintptr_t entry_point, uintptr_t stack, bool user);

void arch_context_set_arg(struct arch_thread_context *context, uint64_t arg);

void arch_context_save(struct arch_thread_context *in,
					   struct arch_thread_context *out);

void arch_context_restore(struct arch_thread_context *context);

#endif
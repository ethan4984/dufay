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
					   uintptr_t kernel_stack, uintptr_t entry, void *arg);

struct thread;

void arch_context_switch(struct thread *old, struct thread *next);

void arch_load_context(struct thread *td);

#define IPI_DPC 47 /* Disabled when CR8=IPLDPC */

void arch_send_ipi(struct cpu_local *cpu, uint8_t ipi);

uint64_t arch_read_timestamp_ns();

#endif

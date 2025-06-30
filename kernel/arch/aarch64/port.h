#ifndef ARCH_AARCH64_PORT_H_
#define ARCH_AARCH64_PORT_H_
#include <stdint.h>
#include <stddef.h>

#define BITS 64
#define LITTLE_ENDIAN 1
#define PAGE_SIZE 4096
#define LARGE_PAGE_SIZE (2 << 20) // 2 MB
#define HUGE_PAGE_SIZE (1 << 30) // 1GB

#define KERNEL_HIGH_VMA 0xffffffff80000000
#define HZ 100

extern uint64_t HIGH_VMA;

#define P2V(x) ((uintptr_t)(x) + HIGH_VMA)
#define V2P(x) ((uintptr_t)(x) - HIGH_VMA)

extern struct cpu_local *CORE_LOCAL;

struct registers {
	uint64_t x[31]; // x0-x30
	uint64_t sp; // stack pointer
	uint64_t pc; // program counter
	uint64_t pstate; // processor state
	uint64_t cs; // BAD!!!
};

struct __attribute__((packed)) arch_cpu_cb {
	uintptr_t kernel_stack;
	uintptr_t user_stack;
	uint64_t error;

	int fpu_thread_size;
	void (*fpu_save)(void *);
	void (*fpu_rstor)(void *);
};

struct arch_thread_context {
	struct registers regs;
	void *fpu_thread;
};

typedef enum : uint8_t {
	IPL_ZERO = 0, /* All interrupts enabled */
	IPL_DISPATCH = 1, /* Preemption disabled */
	IPL_DEVICE = 13, /* Device interrupts disabled */
	IPL_HIGH = 15, /* All interrupts disabled */
} ipl_t;

#endif

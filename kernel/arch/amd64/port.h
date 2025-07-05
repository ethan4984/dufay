#ifndef ARCH_AMD64_PORT_H_
#define ARCH_AMD64_PORT_H_
#include <stdint.h>
#include <stddef.h>
#include <arch/amd64/cpu.h>

#define BITS 64
#define LITTLE_ENDIAN 1
#define PAGE_SIZE 4096
#define LARGE_PAGE_SIZE (2 << 20) // 2 MB
#define HUGE_PAGE_SIZE (1 << 30) // 1GB
#define HZ 100

#define KERNEL_HIGH_VMA 0xffffffff80000000

extern uint64_t HIGH_VMA;

#define P2V(x) ((uintptr_t)(x) + HIGH_VMA)
#define V2P(x) ((uintptr_t)(x) - HIGH_VMA)

struct __attribute__((packed)) arch_cpu_cb {
	uintptr_t kernel_stack;
	uintptr_t user_stack;
	uint64_t error;

	int fpu_thread_size;
	void (*fpu_save)(void *);
	void (*fpu_rstor)(void *);
	struct cpu_local *self;
};

struct __attribute__((packed)) arch_thread_context {
	uint64_t rsp;

	struct arch_thread_regs {
		uint64_t rbp, rbx, r12, r13, r14, r15, rip;
	} regs;
	void *fpu_thread;
};

/* This is faster than reading from an MSR everytime we access CORE_LOCAL...  */
static inline struct cpu_local *amd64_get_cpu()
{
	struct cpu_local *value;
	asm volatile("mov %%gs:%c1, %0"
				 : "=a"(value)
				 : "i"(offsetof(struct arch_cpu_cb, self)));
	return value;
}

#define CORE_LOCAL ({ amd64_get_cpu(); })

typedef enum : uint8_t {
	IPL_ZERO = 0, /* All interrupts enabled */
	IPL_DISPATCH = 1, /* Preemption disabled */
	IPL_DEVICE = 13, /* Device interrupts disabled */
	IPL_HIGH = 15, /* All interrupts disabled */
} ipl_t;

#endif

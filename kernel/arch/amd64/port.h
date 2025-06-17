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
#define CORE_LOCAL ({ (struct cpu_local *)(rdmsr(MSR_GS_BASE)); })

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
};

struct arch_thread_context {
	struct registers regs;
	void *fpu_thread;
};

#endif
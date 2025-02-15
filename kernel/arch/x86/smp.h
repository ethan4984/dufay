#ifndef X86_SMP_H_
#define X86_SMP_H_

#include <core/scheduler.h>
#include <core/portal.h>
#include <fayt/circular_queue.h>

#include <stddef.h>
#include <stdint.h>

struct cpu_local {
	uintptr_t kernel_stack;
	uintptr_t user_stack;
	uint64_t error;
// EVERYTHING ABOVE MUST REMAIN IN ORDER

	struct server *scheduling_server;
	struct portal_link *thread_enqueue_link;
	struct portal_link *thread_baqueue_link;
	VECTOR(struct context*) delivery_stack;
	struct spinlock sched_lock;

	struct context *current_context;

	int fpu_context_size;
	void (*fpu_save)(void*);
	void (*fpu_rstor)(void*);

	int apic_id;
};

void boot_aps(void);

extern size_t logical_processor_cnt;
extern size_t bootable_processor_cnt;

extern struct cpu_local *logical_processor_locales;

#endif

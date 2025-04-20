#ifndef CORE_SCHEDULER_PROCESSOR_H_
#define CORE_SCHEDULER_PROCESSOR_H_

#include <core/scheduler/thread.h>
#include <core/memory/portal.h>

#include <fayt/circular_queue.h>

#include <stddef.h>
#include <stdint.h>

struct cpu_local {
	uintptr_t kernel_stack;
	uintptr_t user_stack;
	uint64_t error;
	// EVERYTHING ABOVE MUST REMAIN IN ORDER

	struct scheduler *scheduler;
	struct delivery_queue delivery_queue;
	struct spinlock sched_lock;

	struct thread *current_thread;

	int fpu_thread_size;
	void (*fpu_save)(void *);
	void (*fpu_rstor)(void *);

	int apic_id;
};

extern size_t logical_processor_cnt;
extern size_t bootable_processor_cnt;

extern struct cpu_local *logical_processor_locales;

#endif

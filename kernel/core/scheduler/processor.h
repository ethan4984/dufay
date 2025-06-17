#ifndef CORE_SCHEDULER_PROCESSOR_H_
#define CORE_SCHEDULER_PROCESSOR_H_

#include <core/scheduler/thread.h>
#include <core/memory/portal.h>
#include <arch/port.h>

#include <fayt/circular_queue.h>

#include <stddef.h>
#include <stdint.h>

struct cpu_local {
	struct arch_cpu_cb
		arch_cb; /* Architecture-dependent CPU control-block. This must ALWAYS be the first member of the struct */
	// EVERYTHING ABOVE MUST REMAIN IN ORDER

	struct scheduler *scheduler;
	struct delivery_queue delivery_queue;
	struct spinlock sched_lock;

	struct thread *current_thread;

	int core_id;
};

extern size_t logical_processor_cnt;
extern size_t bootable_processor_cnt;

extern struct cpu_local *logical_processor_locales;

#endif

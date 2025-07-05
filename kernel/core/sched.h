#ifndef CORE_SCHED_H_
#define CORE_SCHED_H_
#include "core/capability.h"
#include <aria/lock.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/queue.h>
#include <core/sched/impl.h>
#include <arch/port.h>
#include <core/wait.h>

#define PRIO_IDLE 0
#define PRIO_LOW_BATCH 1 /* Low-priority user threads */
#define PRIO_HIGH_BATCH 32 /* High-priority user threads */
#define PRIO_BATCH_RANGE \
	(PRIO_HIGH_BATCH - PRIO_LOW_BATCH + 1) /* Range of batch priorities */

#define PRIO_REALTIME 33 /* Priorities 32-63 are realtime */
#define PRIO_MAX 63

#define PRIO_DEFAULT 16 /* In the mid-range of user threads */
#define PRIO_INTERACTIVE \
	20 /* Interactive threads have a slightly higher priority */

#define PRIO_IS_REALTIME(prio) (prio >= PRIO_REALTIME && prio <= PRIO_MAX)
#define PRIO_IS_BATCH(prio) (prio >= PRIO_LOW_BATCH && prio <= PRIO_HIGH_BATCH)
#define PRIO_IS_IDLE(prio) (prio == PRIO_IDLE)

#define N_PRIO_BATCH \
	(PRIO_HIGH_BATCH - PRIO_LOW_BATCH + 1) /* Number of batch priorities */

#define SCHED_NAME_LENGTH 32

enum thread_state {
	READY,
	RUNNING,
	WAITING,
	ZOMBIE,
	TERMINATED,
};

struct thread {
	struct arch_thread_context ctx; /* Architecture-dependent context */

	struct spinlock lock; /* Thread lock */

	int8_t nice; /* Niceness value */
	uint8_t priority; /* Priority of the thread */
	uint8_t priority_class; /* Priority class (realtime, batch, idle) */

	uint64_t sleep_start; /* When the sleep started sleeping */
	uint64_t sleeptime; /* Ticks spent voluntarily sleeping recently */
	uint64_t runtime; /* Ticks spent running recently */
	uint64_t ticks; /* Overall ticks of runtime */
	bool interactive; /* Whether the thread is interactive or not */
	bool pinned; /* Whether the thread is pinned to this CPU */
	int id;

	enum thread_state state; /* Current state of the thread */

	TAILQ_ENTRY(thread) runqueue_hook; /* Linkage into a runqueue */

#if defined(CONFIG_SCHED_ULE)
	struct runqueue *runq; /* Queue this thread is enqueued on */
#endif

	TAILQ_ENTRY(thread)
	proc_threads_hook; /* Linkage into a process's threads */

	struct cpu_local *last_cpu; /* Last cpu this thread ran on */
	struct process *process; /* The process this threads belongs to */

	uintptr_t kernel_stack_base;

	char name[SCHED_NAME_LENGTH];

	_Atomic enum wait_status wait_status;
	struct waitblock waitblocks[4];
};

struct process {
	char name[SCHED_NAME_LENGTH];

	TAILQ_HEAD(, thread) threads; /* List of threads in the process */

	struct spinlock lock;
	struct capability_table *
		capability_table; /* Table of capabilities associated with this process */

	struct address_space *as;
};

/* Readies a thread */
void sched_ready(struct thread *td);

/* Wakes a thread */
void sched_wake(struct thread *td);

/* Puts the current thread to sleep */
void sched_wait();

/* Yields control of the CPU */
void sched_yield();

/* Pins the thread `td` to `cpu` */
void sched_pin(struct thread *td, struct cpu_local *cpu);

/* This is called by hardclock() at a frequency of HZ (typically 10ms) */
void sched_reschedule();

/* Called in a software interrupt when preemption_reason=PREEMPT_PRIORITY_CHANGE */
void sched_priority_change();

void sched_switch(struct thread *cur, struct thread *next);

#endif

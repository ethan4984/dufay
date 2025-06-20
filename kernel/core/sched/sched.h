#ifndef CORE_SCHED_H_
#define CORE_SCHED_H_
#include <core/memory/address.h>
#include <arch/port.h>
#include <sys/queue.h>
#include <stdatomic.h>
#include <core/wait.h>
#include <fayt/lock.h>

#define PRIO_IDLE 0
#define PRIO_LOW_BATCH 1 /* Low-priority user threads */
#define PRIO_HIGH_BATCH 16 /* High-priority user threads */
#define PRIO_REALTIME 17 /* Priorities 17-32 are realtime */
#define PRIO_MAX 32

#define PRIO_DEFAULT 8 /* In the mid-range of user threads */
#define PRIO_INTERACTIVE \
	10 /* Interactive threads have a slightly higher priority */

#define N_PRIO_BATCH \
	(PRIO_HIGH_BATCH - PRIO_LOW_BATCH + 1) /* Number of batch priorities */

#define RUNQUEUES_N 16

#define SCHED_NAME_LENGTH 32

enum thread_state {
	READY,
	RUNNING,
	WAITING,
	TERMINATED,
};

struct thread {
	struct waitblock *
		waitblocks; /* Pointer to the internal waitblocks when more than INTERNAL_WAITBLOCKS_N (4) are needed */
	struct arch_thread_context
		context; /* Architecture-dependent thread context */

	_Atomic enum wait_status wait_status; /* Atomically updated wait status */

	struct spinlock lock; /* Thread lock */

	uint8_t priority; /* Current priority */
	uint8_t base_priority; /* Base priority for the thread */
	bool interactive; /* Whether or not the thread is interactive */

	enum thread_state state; /* Current state of the thread */

	struct dispatch_header dispatch_hdr; /* Threads are waitable */

	struct waitblock internal_waitblocks[INTERNAL_WAITBLOCKS_N];

	TAILQ_ENTRY(thread) runqueue_hook; /* Linkage into a runqueue */
	TAILQ_ENTRY(thread)
	proc_threads_hook; /* Linkage into a process's threads */

	char name[SCHED_NAME_LENGTH];
};

TAILQ_HEAD(threadqueue, thread);

struct runqueue {
	uint32_t
		status; /* Bitmap of non-empty queues (1 for non-empty, 0 for empty) */
	struct threadqueue queues[RUNQUEUES_N];
};

struct process {
	char name[SCHED_NAME_LENGTH];

	TAILQ_HEAD(, thread) threads; /* List of threads in the process */

	struct spinlock lock;

	struct address_space *as;
};

void sched_suspend(struct thread *thread);

void sched_enqueue(struct thread *thread);
void sched_dequeue(struct thread *thread);

/* This is called by hardclock() at a frequency of HZ (typically 10ms) */
void sched_clock();

/* Yield control of the CPU to the scheduler. */
void sched_yield();

#endif

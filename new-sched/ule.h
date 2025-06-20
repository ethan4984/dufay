#ifndef CORE_SCHED_H_
#define CORE_SCHED_H_
#include <fayt/lock.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/queue.h>

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

#define RUNQUEUES_N (PRIO_MAX + 1)

#define SCHED_NAME_LENGTH 32

enum thread_state {
	READY,
	RUNNING,
	WAITING,
	TERMINATED,
};

struct thread {
	struct spinlock lock; /* Thread lock */

	int8_t nice; /* Niceness value */
	uint8_t priority; /* Priority of the thread */
	uint8_t priority_class; /* Priority class (realtime, batch, idle) */

	int cpu; /* CPU we have affinity on */
	int slice; /* Ticks of slice remaining */
	uint64_t sleeptime; /* Ticks spent voluntarily sleeping recently */
	uint64_t runtime; /* Ticks spent running recently */
	uint64_t ticks; /* Overall ticks of runtime */
	bool interactive; /* Whether the thread is interactive or not */

	enum thread_state state; /* Current state of the thread */

	TAILQ_ENTRY(thread) runqueue_hook; /* Linkage into a runqueue */
	TAILQ_ENTRY(thread)
	proc_threads_hook; /* Linkage into a process's threads */

	struct cpu_local *last_cpu; /* Last cpu this thread ran on */

	void (*entry)(void);

	char name[SCHED_NAME_LENGTH];
};

TAILQ_HEAD(threadqueue, thread);

struct runqueue {
	uint64_t
		status; /* Bitmap of non-empty queues (1 for non-empty, 0 for empty) */
	struct threadqueue queues[RUNQUEUES_N];
};

struct process {
	char name[SCHED_NAME_LENGTH];

	TAILQ_HEAD(, thread) threads; /* List of threads in the process */

	struct spinlock lock;
};

void sched_suspend(struct thread *thread);

void sched_enqueue(struct thread *thread);
void sched_dequeue(struct thread *thread);

/* This is called by hardclock() at a frequency of HZ (typically 10ms) */
void sched_clock();

/* Yield control of the CPU to the scheduler. */
//void sched_yield();

void sched_set_priority(struct thread *thread, uint8_t priority);

#endif

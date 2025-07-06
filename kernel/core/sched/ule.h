#ifndef CORE_SCHED_ULE_H_
#define CORE_SCHED_ULE_H_
#include <aria/lock.h>
#include <sys/queue.h>
#include <stdint.h>
#include <stddef.h>

#define RUNQUEUES_N 64 /* PRIO_MAX + 1 */

TAILQ_HEAD(threadqueue, thread);

struct runqueue {
	uint64_t
		status; /* Bitmap of non-empty queues (1 for non-empty, 0 for empty) */
	struct threadqueue queues[RUNQUEUES_N];
};

struct sched_percpu {
	size_t ticks; /* How many ticks have elapsed */
	size_t load; /* Current load on this CPU (number of ready threads) */

	struct spinlock thread_queues_lock; /* Lock on this CPU's run queues */

	/*
   * Array of runqueues from which to pick PRIO_REALTIME-priority threads from.
   * The scheduler will always try to pick from these queues in priority order
   * before moving on to the calendar queue.
   */
	struct runqueue realtime_runq;

	/*
   * Threads are picked from the queue pointed by `calendar_queue_runidx`, and
   * when the queue is exhausted, the index is incremented. Threads that get
   * inserted are inserted into the index calculated via `calendar_queue_insq`:
   * (calendar_queue_insidx + N_PRIO_BATCH - thread::priority) % RUNQUEUES_N.
   * This ensures that threads with lower priority are put further away on the
   * array and are ran less frequently than threads with higher priority. If
   * `calendar_queue_insidx` is equal to `calendar_queue_runidx`, the insert
   * head will be incremented. `calendar_queue_insidx` is also incremented on
   * every tick (10ms) to ensure all threads get the chance to run.
   * `insix` is therefore ALWAYS one queue ahead of `runidx`, 
   * which ensures that if a new thread gets enqueued it is not
   * inserted in the queue that is currently being looked at by the scheduler.
   * See `sched/ule.c` for more details regarding scheduling.
   */
	struct runqueue calendar_queue;

	size_t calendar_queue_runidx; /* The index of the queue from which to pick the
                                   next thread */
	size_t calendar_queue_insidx; /* The index of the queue where the next thread
                                   is going to be inserted */

	/*
   * Queue of idle priority threads (PRIO_IDLE).
   * Threads from this queue are only picked when both the realtime queues and
   * calendar queues are exhausted.
   */
	TAILQ_HEAD(, thread) idle_queue;

	TAILQ_HEAD(, thread) blocked_queue;

	bool steal_work;
};

#endif

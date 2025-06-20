#ifndef CORE_CPU_H_
#define CORE_CPU_H_

#include <stddef.h>
#include <stdint.h>

#include <fayt/lock.h>
#include <fayt/pairing_heap.h>
#include <stdint.h>

#include "ule.h"
#include <sys/queue.h>

struct cpu_local {
	int core_id; /* ID of this CPU */

	struct thread
		*current_thread; /* Thread that's currently running on this CPU */

	struct thread *next_thread; /* Thread that will run next on this CPU */

	struct spinlock timers_lock;
	struct pairing_heap timers;

	size_t ticks; /* How many ticks have elapsed */
	size_t load; /* Current load on this CPU (number of ready threads) */

	struct thread idle_thread; /* Per-CPU idle thread, executed when there's
                                nothing else to do */

	struct spinlock thread_queues_lock;

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
   * every tick (10ms). So `insix` is ALWAYS at least one queue ahead of
   * `runidx`, this ensures that if a new thread gets enqueued it is not
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
};

extern size_t logical_processor_cnt;
extern size_t bootable_processor_cnt;

extern struct cpu_local *logical_processor_locales;

#endif

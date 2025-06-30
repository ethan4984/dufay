#ifndef CORE_SCHED_RR_H_
#define CORE_SCHED_RR_H_
#include <sys/queue.h>
#include <aria/lock.h>

/* 
 * Trivial scheduler using a round-robin algorithm, where all threads are scheduled successively without care for priorities.
 * This is meant as a debugging aid (as the implementation is simpler) and not for actual use, which is why ULE is selected by default.
*/
struct sched_percpu {
	TAILQ_HEAD(, thread) runq;
	struct spinlock lock;
};

#endif
#ifndef CORE_SCHEDULER_RR_H
#define CORE_SCHEDULER_RR_H
#include <sys/queue.h>
#include <core/scheduler/thread.h>

struct unit {
	struct thread *t;
	TAILQ_ENTRY(unit) entry;
};

struct rr {
	TAILQ_HEAD(, unit) queue; /* The queue of threads */
	struct unit *cur;
	struct unit *idle;
};

int rr_enqueue(struct scheduler *, struct thread *);
int rr_dequeue(struct scheduler *, struct thread *);
int rr_traverse(struct scheduler *, struct thread **);
int rr_init(struct scheduler *);
int rr_destroy(struct scheduler *);

#endif

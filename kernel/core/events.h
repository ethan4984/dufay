#ifndef EVENTS_H_
#define EVENTS_H_

#include <fayt/lock.h>
#include <fayt/vector.h>

struct ucontext;

struct equeue;
struct etrigger {
	VECTOR(struct equeue*) equeue;

	struct ucontext *ucontext;
	int fired;

	int refcnt;
	struct spinlock lock;
};

struct equeue {
	VECTOR(struct ucontext*) ucontext;

	struct spinlock lock;
};

int equeue_block(struct equeue *equeue, struct etrigger **waking_object);
int equeue_wake(struct etrigger *equeue, struct ucontext *waking_ucontext);
int equeue_add(struct equeue *equeue, struct etrigger *trigger);
int equeue_remove(struct equeue *equeue, struct etrigger *trigger);

#endif

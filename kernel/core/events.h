#ifndef CORE_EVENTS_H_
#define CORE_EVENTS_H_

#include <fayt/lock.h>
#include <fayt/vector.h>
#include <sys/queue.h>
#include <core/wait.h>

struct context;

struct equeue;

/* A generic kernel event */
struct event {
	struct dispatch_header hdr;
};

struct etrigger {
	VECTOR(struct equeue *) equeue;

	struct context *context;
	int fired;

	int refcnt;
	struct spinlock lock;
};

struct equeue {
	VECTOR(struct context *) context;

	struct spinlock lock;
};

void event_init(struct event *event, const char *name, bool notification);
void event_signal(struct event *event);

int equeue_block(struct equeue *equeue, struct etrigger **waking_object);
int equeue_wake(struct etrigger *equeue, struct context *waking_context);
int equeue_add(struct equeue *equeue, struct etrigger *trigger);
int equeue_remove(struct equeue *equeue, struct etrigger *trigger);

#endif

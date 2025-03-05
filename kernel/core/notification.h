#ifndef NOTIFICATION_H_
#define NOTIFICATION_H_

#include <core/scheduler.h>
#include <core/events.h>

#include <fayt/lock.h>
#include <fayt/notification.h>
#include <fayt/vector.h>

#define NOTIFICATION_MAX 32
#define NOTIFICATION_MASK(NOT) (1ull << ((NOT) - 1))
#define NOTIFICATION_INDEX(NOT) ((NOT) - 1)
#define NOTIFICATION_PENDING_CAPACITY 8

struct notification_parameter {
	uintptr_t vaddr;
	uint64_t paddr;
	int page_cnt;
	int share;
};

//	WE NEED SOME FLAGS TO INDICATE WHETHER OR NOT A NOTIFICATION HAS BEEN
//	SERVICED, WHETHER OR NOT IT HAS BEEN FINISHED, WHETHER OR NOT IT IS READY
//	TO BE SERVICED.

struct ucontext;
struct notification_queue;
struct notification {
	int refcnt;
	int notnum;
	int weight;
	int serviced;
	int serviceable;
	int done;

	struct notification_parameter parameter;

	struct notification_info *info;
	struct notification_queue *queue;

	VECTOR(struct etrigger *) etrigger;
	struct context *source;
};

struct notification_queue {
	struct notification *queue[NOTIFICATION_MAX][NOTIFICATION_PENDING_CAPACITY];

	bool active;
	int pending;
	int mask;

	struct spinlock lock;
};

struct notification_channel_handle {
	struct sched_proc_id proc_id;
};

int notification_queue(struct context *, struct context *, int, int, int,
					   uintptr_t, uint64_t, int);
int notification_dispatch(struct context *);

#endif

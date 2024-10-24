#ifndef NOTIFICATION_H_
#define NOTIFICATION_H_

#include <core/scheduler.h>

#include <fayt/lock.h>
#include <fayt/notification.h>

#include <stdbool.h>

#define NOTIFICATION_MAX 64
#define NOTIFICATION_MASK(NOT) (1ull << ((NOT) - 1))

struct notification_queue;

struct notification {
	int refcnt;
	int notnum;

	struct {
		void *vaddr;
		uintptr_t paddr;
		int page_cnt;
	} share_region;

	struct notification_info *info;
	struct notification_queue *queue;
};

struct notification_queue {
	struct notification queue[NOTIFICATION_MAX];
	int mask;

	bool active;
	int pending; 
	int delivered;

	struct spinlock lock;
};

int notification_send(struct context*, struct context*, int);
int notification_dispatch(struct context*);

#endif

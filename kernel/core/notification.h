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

	struct notification *next;
	struct notification *last;
};

#define NOTIFICATION_PUSH(QUEUE, NOTIFICATION) ({ \
	__label__ finish; \
	int ret = 0; \
	if((NOTIFICATION) == NULL || (QUEUE) == NULL) { ret = -1; goto finish; } \
	if((QUEUE)->queue[(NOTIFICATION)->notnum - 1]) { \
		(QUEUE)->queue[(NOTIFICATION)->notnum - 1]->last = (NOTIFICATION); \
		(NOTIFICATION)->next = (QUEUE)->queue[(NOTIFICATION)->notnum - 1]; \
	} \
	(QUEUE)->queue[(NOTIFICATION)->notnum - 1] = (NOTIFICATION); \
	(QUEUE)->pending |= NOTIFICATION_MASK((NOTIFICATION)->notnum); \
finish: \
	ret; \
})

#define NOTIFICATION_POP(QUEUE, NOTIFICATION, NOT) ({ \
	__label__ finish; \
	int ret = 0; \
	if((QUEUE) == NULL) { ret = -1; goto finish; } \
	typeof(NOTIFICATION) root = (QUEUE)->queue[NOT - 1]; \
	for(; root;) { \
		if(root->next == NULL) break; \
		root->next; \
	} \
	if(root->last == NULL) (QUEUE)->pending &= ~NOTIFICATION_MASK(NOT - 1); \
	if(root->last) root->last->next = NULL; \
	(NOTIFICATION) = root; \
finish: \
	ret; \
})

struct notification_queue { 
	struct notification *queue[NOTIFICATION_MAX]; 
	int mask;

	bool active;
	int pending; 
	int delivered;

	struct spinlock lock;
};

int notification_send(struct context*, struct context*, int, int);
int notification_dispatch(struct context*);

#endif

#ifndef SCHEDULE_H_
#define SCHEDULE_H_

#include <arch/x86/cpu.h>

#include <core/events.h>
#include <core/server.h>

#include <fayt/vector.h>
#include <fayt/lock.h>
#include <fayt/sched.h>

#define CONTEXT_DEFAULT_STACK_SIZE 0x200000
#define SCHEDULER_DEFAULT_QUEUE_SIZE 0x10000

struct stack {
	uintptr_t sp;
	size_t size;
	int flags;
};

struct ustack {
	struct stack kernel_stack;
	struct stack user_stack;

	int active;

	struct ustack *next;
	struct ustack *last;
};

#define USTACK_CLAIM(CONTEXT, USTACK) ({ \
	__label__ finish; \
	int ret = 0; \
	if((CONTEXT) == NULL) { ret = -1; goto finish; } \
	(USTACK) = (CONTEXT)->stack_tree; \
	for(; (USTACK);) { \
		if(!(USTACK)->active) break; \
		else (USTACK) = (USTACK)->next; \
	} \
	if((USTACK)) (USTACK)->active = 1; \
finish: \
	ret; \
})

#define USTACK_PUSH(CONTEXT, USTACK) ({ \
	__label__ finish; \
	int ret = 0; \
	if((CONTEXT) == NULL || (USTACK) == NULL) { ret = -1; goto finish; } \
	(USTACK)->next = (CONTEXT)->stack_tree; \
	(USTACK)->last = NULL; \
	if((CONTEXT)->stack_tree) (CONTEXT)->stack_tree->last = (USTACK); \
	(CONTEXT)->stack_tree = (USTACK); \
finish: \
	ret; \
})

struct context;
struct ucontext {
	struct ustack *stack;

	struct registers regs;
	void *fpu_context;

	struct etrigger *last_etrigger;
	struct etrigger *etrigger;
	int blocking;

	struct {
		uintptr_t user_stack;
		uint64_t error;
	} sysctx;

	struct notification *notification;
	int ready;
	int delivered;

	struct context *context;

	struct ucontext *next; 
	struct ucontext *last;
};

#define UCONTEXT_PUSH(CONTEXT, UCONTEXT) ({ \
	__label__ finish; \
	int ret = 0; \
	if((CONTEXT) == NULL || (UCONTEXT) == NULL) { ret = -1; goto finish; } \
	(UCONTEXT)->next = NULL; \
	(UCONTEXT)->last = (CONTEXT)->ucontext_top; \
	if((CONTEXT)->ucontext_top) (CONTEXT)->ucontext_top->next = (UCONTEXT); \
	(CONTEXT)->ucontext_top = (UCONTEXT); \
finish: \
	ret; \
})

struct context {
	struct spinlock lock;

	uintptr_t user_gs_base;
	uintptr_t user_fs_base;

	struct ustack *stack_tree;
	struct ucontext *ucontext_queue;

	struct ucontext *ucontext_active;
	struct ucontext *ucontext_top;

	struct {
		struct notification_action *actions;
		struct notification_queue *queue;

		struct spinlock lock;
	} notification;

	struct {
		uint64_t sysperm;

		const char *namespace;
		const char *server;

		int cid;
	} comms;

	struct page_table *page_table;
};

#define yield() __asm__("int $32");

void reschedule(struct registers*, void*);

int create_blank_context(struct context*);
int destroy_ucontext(struct context*, struct ucontext*);
int sched_establish_shared_link(struct context*, struct cpu_local*, const char*);
int sched_dequeue_context(struct server*, struct context*, struct sched_queue_config_set*, int);
int sched_enqueue_context(struct server*, struct context*, struct sched_queue_config_set*, int);

#endif

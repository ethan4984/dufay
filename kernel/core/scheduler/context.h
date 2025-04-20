#ifndef CORE_SCHEDULER_CONTEXT_H_
#define CORE_SCHEDULER_CONTEXT_H_

#include <arch/x86/cpu.h>

#include <core/capability.h>
#include <core/events.h>
#include <core/memory/address.h>

#include <fayt/lock.h>
#include <fayt/sched.h>
#include <fayt/vector.h>

constexpr int CONTEXT_DEFAULT_STACK_SIZE = 0x200000;
constexpr int SCHED_TICK_RATE_MS = 20;

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

#define USTACK_CLAIM(CONTEXT, USTACK)      \
	({                                     \
		__label__ finish;                  \
		int ret = 0;                       \
		if ((CONTEXT) == NULL) {           \
			ret = -1;                      \
			goto finish;                   \
		}                                  \
		(USTACK) = (CONTEXT)->stack_tree;  \
		for (; (USTACK);) {                \
			if (!(USTACK)->active)         \
				break;                     \
			else                           \
				(USTACK) = (USTACK)->next; \
		}                                  \
		if ((USTACK))                      \
			(USTACK)->active = 1;          \
finish:                                    \
		ret;                               \
	})

#define USTACK_PUSH(CONTEXT, USTACK)                 \
	({                                               \
		__label__ finish;                            \
		int ret = 0;                                 \
		if ((CONTEXT) == NULL || (USTACK) == NULL) { \
			ret = -1;                                \
			goto finish;                             \
		}                                            \
		(USTACK)->next = (CONTEXT)->stack_tree;      \
		(USTACK)->last = NULL;                       \
		if ((CONTEXT)->stack_tree)                   \
			(CONTEXT)->stack_tree->last = (USTACK);  \
		(CONTEXT)->stack_tree = (USTACK);            \
finish:                                              \
		ret;                                         \
	})

struct thread;
struct context {
	struct ustack *stack;

	struct registers regs;
	void *fpu_thread;

	struct etrigger *last_etrigger;
	struct etrigger *etrigger;
	int blocking;

	struct {
		uintptr_t user_stack;
		uint64_t error;
	} sysctx;

	struct notification *notification;

	struct thread *thread;

	struct context *next;
	struct context *last;
};

#define CONTEXT_PUSH(CONTEXT, UCONTEXT)                \
	({                                                 \
		__label__ finish;                              \
		int ret = 0;                                   \
		if ((CONTEXT) == NULL || (UCONTEXT) == NULL) { \
			ret = -1;                                  \
			goto finish;                               \
		}                                              \
		(UCONTEXT)->next = NULL;                       \
		(UCONTEXT)->last = (CONTEXT)->context_top;     \
		if ((CONTEXT)->context_top)                    \
			(CONTEXT)->context_top->next = (UCONTEXT); \
		(CONTEXT)->context_top = (UCONTEXT);           \
finish:                                                \
		ret;                                           \
	})

void reschedule(struct registers *, void *);
int destroy_context(struct thread *, struct context *);

#endif

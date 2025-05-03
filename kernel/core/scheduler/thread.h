#ifndef CORE_SCHEDULER_THREAD_H_
#define CORE_SCHEDULER_THREAD_H_

#include <core/scheduler/context.h>

#include <fayt/rb_tree.h>

struct thread_capability {
	int tid;
	int tgid;
};

struct thread;
struct delivery_queue {
	struct thread *list;
	struct thread *top;
};

struct thread {
	struct spinlock lock;

	uintptr_t user_gs_base;
	uintptr_t user_fs_base;

	struct address_space *address_space;

	struct ustack *stack_tree;
	struct context *context_queue;

	struct context *context_active;
	struct context *context_top;

	struct {
		struct notification_action *actions;
		struct notification_queue *queue;
		struct spinlock lock;
	} notification;

	struct capability_table *capability_table;
	struct thread_capability thread_capability;

	struct scheduler *scheduler;
	void *private;

	struct thread *next;
	struct thread *last;
};

struct scheduler {
	int (*enqueue)(struct scheduler *, struct thread *);
	int (*dequeue)(struct scheduler *, struct thread *);
	int (*traverse)(struct scheduler *, struct thread **);

	int (*init)(struct scheduler *);
	int (*destroy)(struct scheduler *);

	struct timer timer;
	int processor_id;
	int load;
	struct time slice;

	void *private;
	struct spinlock lock;
};

constexpr int TGID_SYSTEM = 0;

struct tgroup {
	struct bitmap tid_bitmap;
	struct dictionary tid_table;
	int tgid;
};

extern struct scheduler **scheduler_table;

int create_thread(int tgid, struct thread **);
int search_thread(struct thread_capability *, struct thread **);

int delivery_queue_peek(struct delivery_queue *, struct thread **);
int delivery_queue_remove(struct delivery_queue *, struct thread *);
int delivery_queue_push(struct delivery_queue *, struct thread *);
int enqueue_thread(struct thread *);
int dequeue_thread(struct thread *);
int tgroup_search(int, struct tgroup **);
int tgroup_insert(struct tgroup *);
int tgroup_remove(int);

int launch_schedulers(void);

#define yield() __asm__("int $32");

#endif

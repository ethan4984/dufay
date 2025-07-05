#ifndef CORE_TIMER_H_
#define CORE_TIMER_H_
#include <core/wait.h>
#include <core/clock.h>
#include <aria/pairing_heap.h>
#include <stdatomic.h>

enum timer_state {
	TIMER_RUNNING, /* The timer is currently being handled */
	TIMER_PENDING, /* The timer is currently enqueued */
	TIMER_STOPPED, /* The timer was stopped */
};

struct ktimer {
	TAILQ_ENTRY(ktimer) queue_hook;
	struct dispatch_header hdr;
	struct pairing_heap_node heap_node;

	nanoseconds_t deadline;

	_Atomic(enum timer_state) state;

	struct cpu_local *cpu;
};

void timer_init(struct ktimer *timer, const char *name);

void timer_start(struct ktimer *timer, nanoseconds_t nanoseconds);

void timer_stop(struct ktimer *timer);

bool timer_compare(struct pairing_heap_node *a, struct pairing_heap_node *b);

void timer_handle_expiry(void *, void *);

#endif

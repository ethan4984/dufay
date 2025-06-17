#ifndef CORE_TIMER_H_
#define CORE_TIMER_H_
#include <core/wait.h>
#include <fayt/pairing_heap.h>

typedef uint64_t nanoseconds_t;

struct ktimer {
	struct dispatch_header hdr;
	struct pairing_heap_node heap_node;

	nanoseconds_t deadline;
};

void timer_init(struct ktimer *timer, const char *name);

void timer_start(struct ktimer *timer, nanoseconds_t nanoseconds);

void timer_stop(struct ktimer *timer);

bool timer_compare(struct pairing_heap_node *a, struct pairing_heap_node *b);

#endif

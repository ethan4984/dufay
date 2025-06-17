#include <core/timer.h>
#include <core/lock.h>
#include <fayt/container_of.h>

bool timer_compare(struct pairing_heap_node *a, struct pairing_heap_node *b)
{
	struct ktimer *timer_a =
		(struct ktimer *)(CONTAINER_OF(a, struct ktimer, heap_node));

	struct ktimer *timer_b =
		(struct ktimer *)(CONTAINER_OF(b, struct ktimer, heap_node));

	return timer_a->deadline < timer_b->deadline;
}

void timer_init(struct ktimer *timer, const char *name)
{
	dispatch_object_init(&timer->hdr, DISPATCH_NOTIFICATION, name);
	timer->hdr.signaled_count = 0;
	timer->deadline = 0;
}

void timer_start(struct ktimer *timer, uint64_t nanoseconds)
{
	(void)nanoseconds;

	spinlock_irqsave(&timer->hdr.lock);

	spinrelease_irqsave(&timer->hdr.lock);
}

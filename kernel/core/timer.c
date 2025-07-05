#include "arch/amd64/port.h"
#include <aria/pairing_heap.h>
#include <core/timer.h>
#include <core/lock.h>
#include <aria/base.h>
#include <core/cpu.h>
#include <stdatomic.h>
#include <core/debug.h>
#include <aria/debug.h>

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

	timer->deadline = 0;
	timer->cpu = NULL;

	atomic_store(&timer->state, TIMER_STOPPED);
}

#define TICKS_TO_NS(TICKS) ((TICKS) * (NANOSECONDS_PER_SECOND / HZ))

void timer_start(struct ktimer *timer, uint64_t nanoseconds)
{
	enum timer_state expected = TIMER_STOPPED;

	ipl_t ipl = spinlock_acquire_at(&timer->hdr.lock, IPL_HIGH);
	struct cpu_local *cpu = CORE_LOCAL;

	if (!atomic_compare_exchange_weak(&timer->state, &expected,
									  TIMER_PENDING)) {
		spinlock_release(&timer->hdr.lock, ipl);
		return;
	}

	timer->deadline = TICKS_TO_NS(atomic_load(&cpu->ticks)) + nanoseconds;

	timer->cpu = cpu;

	spinlock_irqsave(&cpu->timers_lock);

	pairing_heap_insert(&cpu->timers, &timer->heap_node);

	spinrelease_irqsave(&cpu->timers_lock);

	spinlock_release(&timer->hdr.lock, ipl);
}

static void dequeue_timer(struct ktimer *timer)
{
	struct cpu_local *cpu = timer->cpu;

	spinlock_irqsave(&cpu->timers_lock);

	pairing_heap_remove(&cpu->timers, &timer->heap_node);

	spinrelease_irqsave(&cpu->timers_lock);
}

void timer_stop(struct ktimer *timer)
{
	ipl_t ipl = spinlock_acquire(&timer->hdr.lock);

	/* Timer was already stopped */
	if (atomic_load(&timer->state) == TIMER_STOPPED) {
		spinlock_release(&timer->hdr.lock, ipl);
		return;
	}

	/* Dequeue the timer */
	if (atomic_load(&timer->state) == TIMER_PENDING) {
		dequeue_timer(timer);
	}

	/* Timer is currently running, wait until it's done */
	while (atomic_load(&timer->state) == TIMER_RUNNING) {
		/* Make sure the compiler doesn't remove this loop! */
		__asm__ volatile("");
	}

	atomic_store(&timer->state, TIMER_STOPPED);

	spinlock_release(&timer->hdr.lock, ipl);
}

/* We ignore the arguments, they are irrelevant */
void timer_handle_expiry(void *, void *)
{
	ASSERT(ipl_get() == IPL_DISPATCH);

	while (true) {
		struct pairing_heap_node *timer_node;
		struct ktimer *timer;
		enum timer_state expected = TIMER_PENDING;
		struct cpu_local *cpu = CORE_LOCAL;

		spinlock_irqsave(&cpu->timers_lock);

		/* Get the timer that expires the soonest */
		timer_node = pairing_heap_top(&cpu->timers);

		/* No timers */
		if (!timer_node) {
			spinrelease_irqsave(&cpu->timers_lock);
			break;
		}

		timer = CONTAINER_OF(timer_node, struct ktimer, heap_node);

		/* This timer shouldn't expire yet */
		if (timer->deadline > TICKS_TO_NS(cpu->ticks)) {
			spinrelease_irqsave(&cpu->timers_lock);
			break;
		}

		/* Remove the timer from the heap */
		pairing_heap_pop(&CORE_LOCAL->timers);

		spinrelease_irqsave(&cpu->timers_lock);

		/* Timer was canceled */
		if (!atomic_compare_exchange_weak(&timer->state, &expected,
										  TIMER_RUNNING)) {
			continue;
		}

		/* Wake whoever was waiting on the timer to expire */
		spinlock(&timer->hdr.lock);

		timer->hdr.signaled_count = 1;

		try_satisfy_dispatch_object(&timer->hdr);

		timer->hdr.signaled_count = 0;

		atomic_store(&timer->state, TIMER_STOPPED);

		spinrelease(&timer->hdr.lock);
	}
}

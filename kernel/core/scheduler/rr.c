#include "core/scheduler/processor.h"
#include <core/scheduler/rr.h>
#include <aria/slab.h>
#include <aria/debug.h>
#include <arch/x86/smp.h>
#include <aria/compiler.h>
#include <core/lock.h>

static struct spinlock rr_lock;
int last_used = 0;

int rr_enqueue(struct scheduler *sched, struct thread *t)
{
	if (unlikely(t == NULL))
		RETURN_ERROR;
	if (unlikely(sched == NULL))
		RETURN_ERROR;

	if (!t->scheduler) {
		spinlock_irqsave(&rr_lock);

		struct scheduler *candidate = scheduler_table[last_used];
		sched = candidate;

		last_used++;

		if (last_used >= (logical_processor_cnt))
			last_used = 0;

		spinrelease_irqsave(&rr_lock);
	} else {
		sched = t->scheduler;
	}

	spinlock_irqsave(&sched->lock);

	struct rr *rr = sched->private;
	if (unlikely(rr == NULL)) {
		spinrelease_irqsave(&sched->lock);
		RETURN_ERROR;
	}

	struct unit *unit = alloc(sizeof(struct unit));
	if (unlikely(unit == NULL)) {
		spinrelease_irqsave(&sched->lock);
		RETURN_ERROR;
	}

	unit->t = t;
	t->scheduler = sched;
	TAILQ_INSERT_TAIL(&rr->queue, unit, entry);

	rr->cur = unit;

	spinrelease_irqsave(&sched->lock);

	return 0;
}

int rr_dequeue(struct scheduler *sched, struct thread *t)
{
	if (unlikely(t == NULL))
		RETURN_ERROR;
	if (unlikely(sched == NULL))
		RETURN_ERROR;

	spinlock_irqsave(&sched->lock);

	struct rr *rr = sched->private;
	if (unlikely(rr == NULL)) {
		spinrelease_irqsave(&sched->lock);
		RETURN_ERROR;
	}

	struct unit *unit;
	TAILQ_FOREACH(unit, &rr->queue, entry)
	{
		if (unit->t == t) {
			TAILQ_REMOVE(&rr->queue, unit, entry);
			free(unit);
			spinrelease_irqsave(&sched->lock);
			return 0;
		}
	}

	spinrelease_irqsave(&sched->lock);
	return -1;
}

int rr_traverse(struct scheduler *sched, struct thread **out)
{
	if (unlikely(out == NULL))
		RETURN_ERROR;
	if (unlikely(sched == NULL))
		RETURN_ERROR;

	struct rr *rr = sched->private;
	if (unlikely(rr == NULL))
		RETURN_ERROR;

	spinlock_irqsave(&sched->lock);

	if (rr->cur == NULL) {
		rr->cur = rr->idle;
		*out = rr->idle->t;

		spinrelease_irqsave(&sched->lock);
		return 0;
	}

	struct unit *unit = TAILQ_NEXT(rr->cur, entry);

	if (unit == NULL) {
		unit = TAILQ_FIRST(&rr->queue);
	}

	if (!unit) {
		rr->cur = rr->idle;
		*out = rr->idle->t;

		spinrelease_irqsave(&sched->lock);
		return 0;
	}

	*out = unit->t;
	rr->cur = unit;

	spinrelease_irqsave(&sched->lock);

	return 0;
}

struct thread *new_kernel_thread(uintptr_t entry);

static void idle()
{
	for (;;) {
		__asm__("hlt");
	}
}

int rr_init(struct scheduler *sched)
{
	sched->private = alloc(sizeof(struct rr));
	struct rr *rr = sched->private;
	TAILQ_INIT(&rr->queue);
	rr->cur = NULL;
	rr->idle = alloc(sizeof(struct unit));
	rr->idle->t = new_kernel_thread((uintptr_t)idle);

	return 0;
}

int rr_destroy(struct scheduler *sched)
{
	(void)sched;
	return 0;
}

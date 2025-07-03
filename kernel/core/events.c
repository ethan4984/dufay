#include <arch/amd64/smp.h>

#include <core/events.h>
#include <core/sched.h>
#include <core/notification.h>
#include <core/lock.h>
#include <mm/physical.h>

#include <aria/debug.h>
#include <aria/compiler.h>
#include <core/wait.h>

void event_init(struct event *event, const char *name, bool notification)
{
	dispatch_object_init(
		&event->hdr,
		notification ? DISPATCH_NOTIFICATION : DISPATCH_SYNCHRONIZATION, name);
}

void event_signal(struct event *event)
{
	ipl_t ipl = spinlock_acquire(&event->hdr.lock);

	/* Event was already signaled */
	if (event->hdr.signaled_count > 0) {
		spinlock_release(&event->hdr.lock, ipl);
		return;
	}

	event->hdr.signaled_count = 1;

	/* Try to satisfy waits */
	try_satisfy_dispatch_object(&event->hdr);

	spinlock_release(&event->hdr.lock, ipl);
}

int equeue_wake(struct etrigger *etrigger, struct context *waking_context)
{
	panic("not implemented");

	if (etrigger == NULL || waking_context == NULL)
		RETURN_ERROR;
#if 0
	etrigger->context = waking_context;

	spinlock_irqsave(&etrigger->lock);

	struct scheduler *scheduler = CORE_LOCAL->scheduler;
	if (unlikely(scheduler == NULL || scheduler->enqueue == NULL))
		RETURN_ERROR;

	for (size_t i = 0; i < etrigger->equeue.length; i++) {
		struct equeue *equeue = etrigger->equeue.data[i];

		if (equeue == NULL)
			continue;

		spinlock_irqsave(&equeue->lock);

		for (size_t j = 0; j < equeue->context.length; j++) {
			struct context *context = equeue->context.data[j];

			if (context == NULL)
				continue;

			context->last_etrigger = etrigger;
			context->blocking = false;

			struct thread *thread = context->thread;
			if (thread == NULL)
				RETURN_ERROR;

			int ret = scheduler->enqueue(scheduler, thread);
			if (ret == -1)
				RETURN_ERROR;
		}

		VECTOR_CLEAR(equeue->context);
		spinrelease_irqsave(&equeue->lock);
	}

	spinrelease_irqsave(&etrigger->lock);
#endif
	return 0;
}

int equeue_block(struct equeue *equeue, struct etrigger **waking_object)
{
	panic("not impl");

	(void)equeue;
	(void)waking_object;
#if 0
	if (equeue == NULL)
		RETURN_ERROR;

	struct thread *thread = CORE_LOCAL->current_thread;
	if (thread == NULL)
		RETURN_ERROR;

	struct context *context = thread->context_active;
	if (context == NULL)
		RETURN_ERROR;

	spinlock_irqsave(&equeue->lock);

	VECTOR_PUSH(equeue->context, context);

	spinrelease_irqsave(&equeue->lock);

	struct scheduler *scheduler = CORE_LOCAL->scheduler;
	if (unlikely(scheduler == NULL || scheduler->dequeue == NULL))
		RETURN_ERROR;

	struct spinlock blksync;
	spinlock_irqsave(&blksync);

	int ret = scheduler->dequeue(scheduler, thread);
	if (ret == -1)
		RETURN_ERROR;

	context->blocking = true;
	spinrelease_irqsave(&blksync);

	for (; context->blocking;) {
		yield();
	}

	if (waking_object)
		*waking_object = context->last_etrigger;
#endif
	return 0;
}

int equeue_add(struct equeue *equeue, struct etrigger *trigger)
{
	if (equeue == NULL || trigger == NULL)
		RETURN_ERROR;

	spinlock_irqsave(&trigger->lock);

	VECTOR_PUSH(trigger->equeue, equeue);
	trigger->refcnt++;

	spinrelease_irqsave(&trigger->lock);

	return 0;
}

int equeue_remove(struct equeue *equeue, struct etrigger *trigger)
{
	if (equeue == NULL || trigger == NULL)
		RETURN_ERROR;

	spinlock_irqsave(&trigger->lock);

	VECTOR_REMOVE_BY_VALUE(trigger->equeue, equeue);

	trigger->refcnt--;
	if (!trigger->refcnt)
		free(trigger);

	spinrelease_irqsave(&trigger->lock);

	return 0;
}

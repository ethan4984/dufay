#include <arch/x86/cpu.h>
#include <arch/x86/smp.h>

#include <core/events.h>
#include <core/scheduler.h>
#include <core/notification.h>
#include <core/lock.h>
#include <core/physical.h>

#include <fayt/debug.h>
#include <fayt/sched.h>

int equeue_wake(struct etrigger *etrigger, struct ucontext *waking_ucontext)
{
	if (etrigger == NULL || waking_ucontext == NULL)
		RETURN_ERROR;

	etrigger->ucontext = waking_ucontext;

	spinlock_irqsave(&etrigger->lock);

	VECTOR(struct context *) context_unblocked = { 0 };

	for (int i = 0; i < etrigger->equeue.length; i++) {
		struct equeue *equeue = etrigger->equeue.data[i];
		if (equeue == NULL)
			continue;

		spinlock_irqsave(&equeue->lock);

		for (int j = 0; j < equeue->ucontext.length; j++) {
			struct ucontext *ucontext = equeue->ucontext.data[j];
			if (ucontext == NULL)
				continue;

			ucontext->last_etrigger = etrigger;
			ucontext->blocking = false;

			VECTOR_PUSH(context_unblocked, ucontext->context);
		}

		VECTOR_CLEAR(equeue->ucontext);

		spinrelease_irqsave(&equeue->lock);
	}

	spinrelease_irqsave(&etrigger->lock);

	struct sched_queue_config_set *queue_set =
		alloc(sizeof(struct sched_queue_config_set) +
			  context_unblocked.length * sizeof(struct sched_queue_config));

	queue_set->cnt = 0;
	for (int i = 0; i < context_unblocked.length; i++) {
		struct context *context = context_unblocked.data[i];
		if (context == NULL)
			continue;

		//print("event_wake: requeuing %s\n", context->comms.server);

		queue_set->config[i] =
			(struct sched_queue_config){ .proc_id = context->comms.proc_id };

		queue_set->cnt++;
	}

	VECTOR_CLEAR(context_unblocked);
	if (queue_set->cnt == 0)
		return 0;

	int ret = sched_enqueue_context(CORE_LOCAL->scheduling_context,
									CORE_LOCAL->current_context, queue_set,
									NOTIFY_WEIGHT_INSTANTANEOUS);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int equeue_block(struct equeue *equeue, struct etrigger **waking_object)
{
	if (equeue == NULL)
		RETURN_ERROR;

	struct context *context = CORE_LOCAL->current_context;
	if (context == NULL)
		RETURN_ERROR;

	struct ucontext *ucontext = context->ucontext_active;
	if (ucontext == NULL)
		RETURN_ERROR;

	spinlock_irqsave(&equeue->lock);

	VECTOR_PUSH(equeue->ucontext, ucontext);

	struct sched_queue_config_set *queue_set =
		alloc(sizeof(struct sched_queue_config_set) +
			  sizeof(struct sched_queue_config));

	queue_set->cnt = 1;
	*queue_set->config =
		(struct sched_queue_config){ .proc_id = context->comms.proc_id };

	spinrelease_irqsave(&equeue->lock);

	int ret = sched_dequeue_context(CORE_LOCAL->scheduling_context, context,
									queue_set, NOTIFY_WEIGHT_INSTANTANEOUS);
	if (ret == -1)
		RETURN_ERROR;

	ucontext->blocking = true;
	for (; ucontext->blocking;) {
		yield();
	}

	if (waking_object)
		*waking_object = ucontext->last_etrigger;

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

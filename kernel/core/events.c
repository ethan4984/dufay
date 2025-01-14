#include <arch/x86/cpu.h>
#include <arch/x86/smp.h>

#include <core/events.h>
#include <core/scheduler.h>
#include <core/notification.h>
#include <core/lock.h>
#include <core/physical.h>
#include <core/server.h>

#include <fayt/debug.h>
#include <fayt/sched.h>

int equeue_block(struct equeue *equeue, struct etrigger **waking_object) {
	if(equeue == NULL) RETURN_ERROR;
	
	struct context *context = CORE_LOCAL->current_context;
	if(context == NULL) RETURN_ERROR;

	struct ucontext *ucontext = context->ucontext_active;
	if(ucontext == NULL) RETURN_ERROR;

	spinlock_irqsave(&equeue->lock);

	VECTOR_PUSH(equeue->ucontext, ucontext);

	{
		struct sched_queue_config *config = (void*)(pmm_alloc(1, 1) + HIGH_VMA);

		config->cid = context->comms.cid;
		config->cgroup = 0;
		config->nice = 0;
		config->offload = 0;

		int ret = notification_queue(context, CORE_LOCAL->scheduling_server->context,
			NOT_SCHED_DEQUEUE, NOTIFY_WEIGHT_INSTANTANEOUS, 1, 0, (uint64_t)config - HIGH_VMA, 1);
		if(ret == -1) RETURN_ERROR;

		pmm_free((uintptr_t)config - HIGH_VMA, 1);
	}

	spinrelease_irqsave(&equeue->lock);

	ucontext->blocking = true;
	for(; ucontext->blocking;) yield();

	if(waking_object) *waking_object = ucontext->etrigger;

	return 0;
}

int equeue_arise(struct etrigger *etrigger, struct ucontext *waking_ucontext) {
	if(etrigger == NULL || waking_ucontext == NULL) RETURN_ERROR;

	etrigger->ucontext = waking_ucontext;

	spinlock_irqsave(&etrigger->lock);

	for(int i = 0; i < etrigger->equeue.length; i++) {
		struct equeue *equeue = etrigger->equeue.data[i];
		if(equeue == NULL) continue;

		spinlock_irqsave(&equeue->lock);

		for(int j = 0; j < equeue->ucontext.length; j++) {
			struct ucontext *ucontext = equeue->ucontext.data[j];
			if(ucontext == NULL) continue;

			ucontext->etrigger = etrigger;
			ucontext->blocking = false;
		}

		VECTOR_CLEAR(equeue->ucontext);

		spinrelease_irqsave(&equeue->lock);
	}

	spinrelease_irqsave(&etrigger->lock);

	return 0;
}

int equeue_add(struct equeue *equeue, struct etrigger *trigger) {
	if(equeue == NULL || trigger == NULL) RETURN_ERROR;

	spinlock_irqsave(&trigger->lock);
	VECTOR_PUSH(trigger->equeue, equeue);
	trigger->refcnt++;
	spinrelease_irqsave(&trigger->lock);

	return 0;
}

int equeue_remove(struct equeue *equeue, struct etrigger *trigger) {
	if(equeue == NULL || trigger == NULL) RETURN_ERROR;

	spinlock_irqsave(&trigger->lock);

	VECTOR_REMOVE_BY_VALUE(trigger->equeue, equeue);

	trigger->refcnt--;
	if(!trigger->refcnt) free(trigger);

	spinrelease_irqsave(&trigger->lock);

	return 0;
}

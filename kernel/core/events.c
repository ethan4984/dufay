#include <arch/x86/cpu.h>
#include <arch/x86/smp.h>

#include <core/events.h>
#include <core/scheduler.h>
#include <core/notification.h>

#include <fayt/debug.h>

int equeue_block(struct equeue *equeue, struct etrigger **waking_object) {
	if(equeue == NULL) RETURN_ERROR;
	
	struct context *context = CORE_LOCAL->current_context;
	if(context == NULL) RETURN_ERROR;

	struct ucontext *ucontext = context->ucontext_active;
	if(ucontext == NULL) RETURN_ERROR;

	spinlock(&equeue->lock);
	VECTOR_PUSH(equeue->ucontext, ucontext);
	spinrelease(&equeue->lock);

	ucontext->blocking = true;
	for(; ucontext->blocking;) yield();

	if(waking_object) *waking_object = ucontext->etrigger;

	return 0;
}

int equeue_arise(struct etrigger *etrigger, struct ucontext *waking_ucontext) {
	if(etrigger == NULL || waking_ucontext == NULL) RETURN_ERROR;

	etrigger->ucontext = waking_ucontext;

	spinlock(&etrigger->lock);

	for(int i = 0; i < etrigger->equeue.length; i++) {
		struct equeue *equeue = etrigger->equeue.data[i];
		if(equeue == NULL) continue;

		spinlock(&equeue->lock);

		for(int j = 0; j < equeue->ucontext.length; j++) {
			struct ucontext *ucontext = equeue->ucontext.data[j];
			if(ucontext == NULL) continue;

			ucontext->etrigger = etrigger;
			ucontext->blocking = false;
		}

		VECTOR_CLEAR(equeue->ucontext);

		spinrelease(&equeue->lock);
	}

	spinrelease(&etrigger->lock);

	return 0;
}

int equeue_add(struct equeue *equeue, struct etrigger *trigger) {
	if(equeue == NULL || trigger == NULL) RETURN_ERROR;

	spinlock(&trigger->lock);
	VECTOR_PUSH(trigger->equeue, equeue);
	trigger->refcnt++;
	spinrelease(&trigger->lock);

	return 0;
}

int equeue_remove(struct equeue *equeue, struct etrigger *trigger) {
	if(equeue == NULL || trigger == NULL) RETURN_ERROR;

	spinlock(&trigger->lock);

	VECTOR_REMOVE_BY_VALUE(trigger->equeue, equeue);

	trigger->refcnt--;
	if(!trigger->refcnt) free(trigger);

	spinrelease(&trigger->lock);

	return 0;
}

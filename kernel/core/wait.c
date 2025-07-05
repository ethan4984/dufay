#include <core/sched.h>
#include <core/wait.h>
#include <stdatomic.h>
#include <stddef.h>
#include <sys/queue.h>
#include <arch/amd64/smp.h>
#include <core/lock.h>
#include <aria/debug.h>
#include <core/ipl.h>
#include <core/timer.h>

void dispatch_object_init(struct dispatch_header *hdr,
						  enum dispatch_object_type type, const char *name)
{
	TAILQ_INIT(&hdr->waitblocks);
	hdr->type = type;
	hdr->name = name;
	hdr->signaled_count = 0;
	hdr->lock.lock = 0;
}

static void dispatch_object_consume(struct dispatch_header *hdr)
{
	switch (hdr->type) {
	case DISPATCH_NOTIFICATION:
		/* Object remains signaled */
		break;
	case DISPATCH_SYNCHRONIZATION:
		hdr->signaled_count--;
		break;
	}
}

#define CAS(ptr, old, new)                 \
	atomic_compare_exchange_weak_explicit( \
		(ptr), &(old), (new), memory_order_acquire, memory_order_relaxed)

int wait_any(int count, void *objects[], nanoseconds_t timeout)
{
	ipl_t ipl = ipldispatch();

	struct thread *thread = CORE_LOCAL->current_thread;
	_Atomic enum wait_status *status = &thread->wait_status;
	enum wait_status in_progress = WAIT_IN_PROGRESS;
	int satisfier = -1;
	int timer_i = -1;
	struct ktimer timer;
	struct waitblock timer_wb = { 0 };

	atomic_store(status, WAIT_IN_PROGRESS);

	if (timeout != 0 && timeout != -1UL) {
		timer_init(&timer, "timeout");
		timer_i = (count = count + 1) - 1;
	}

	for (int i = 0; i < count; i++) {
		bool is_timer = i == timer_i;

		struct dispatch_header *obj =
			is_timer ? &timer.hdr : (struct dispatch_header *)objects[i];

		struct waitblock *wb = is_timer ? &timer_wb : &thread->waitblocks[i];

		spinlock(&obj->lock);

		/* Object was already signaled, consume the signal and abort */
		if (obj->signaled_count > 0 &&
			CAS(status, in_progress, WAIT_SATISFIED)) {
			dispatch_object_consume(obj);
			spinrelease(&obj->lock);
			satisfier = i;
			break;
		} else if (obj->signaled_count > 0) {
			/* We have already been satisfied in the meantime (case #1), abort */
			spinrelease(&obj->lock);
			satisfier = i;
			break;
		}

		/* We are not satisfied yet, so add a waitblock to the object's waitblocks */
		TAILQ_INSERT_TAIL(&obj->waitblocks, wb, queue_hook);

		wb->object = obj;
		wb->thread = thread;
		wb->status = WAITBLOCK_ACTIVE;

		spinrelease(&obj->lock);
	}

	/* Wait was already satisfied, back out  */
	if (satisfier != -1 || timeout == 0) {
		if (atomic_load(status) != WAIT_SATISFIED) {
			panic(
				"wait_any: satisfied but status is not WAIT_SATISFIED (status=%d)",
				atomic_load(status));
		}

		/* Remove any waitblock we might've installed */
		for (int i = 0; i < satisfier; i++) {
			struct dispatch_header *obj = (struct dispatch_header *)objects[i];
			struct waitblock *waitblock = &thread->waitblocks[i];

			spinlock(&obj->lock);
			waitblock->status = WAITBLOCK_INACTIVE;
			TAILQ_REMOVE(&obj->waitblocks, waitblock, queue_hook);
			spinrelease(&obj->lock);
		}

		ipl_lower(ipl);

		/* This will return -1 in case of timeout */
		return satisfier;
	}

	if (timeout != -1UL) {
		timer_start(&timer, timeout);
	}

	/*
	 * Now try committing the wait
	 * While we're trying to commit the wait, the object locks have been released, and their state could therefore change
	 * We need to re-check the state of the wait before actually blocking
	*/

	if (CAS(status, in_progress, WAIT_COMMITTED)) {
		/* We're good, now actually block */
		sched_wait();
	}

	/*
	 * We're back (or we never went to sleep)!
	 * Stop the timer if it was set
	 */
	if (timeout != -1UL) {
		timer_stop(&timer);
	}

	/* Find the object that satisfied us */
	for (int i = 0; i < count; i++) {
		bool is_timer = i == timer_i;

		struct dispatch_header *obj =
			is_timer ? &timer.hdr : (struct dispatch_header *)objects[i];

		struct waitblock *wb = is_timer ? &timer_wb : &thread->waitblocks[i];

		spinlock(&obj->lock);

		/* Waitblock is still active, remove it from the list */
		if (wb->status == WAITBLOCK_ACTIVE) {
			TAILQ_REMOVE(&obj->waitblocks, wb, queue_hook);
		}

		/*
		 * This waitblock was signaled, it is the one that satisfied us
		 * Now, this is a simplification over Windows 7's model where Wait blocks have the state WaitBlockBypassStart when they have interrupted a wait while it was being prepared
		 * Instead, we keep the same logic for both code paths (where a wait actually happened, or where it was interrupted before it could be committed) and just check whether or not the waitblock was signaled
		*/
		if (wb->status == WAITBLOCK_SIGNALED) {
			if (satisfier != -1) {
				spinrelease(&obj->lock);
				panic("wait_any: multiple satisfiers found");
			}
			satisfier = i;
		}

		/* Inactive waitblocks are ignored */
		spinrelease(&obj->lock);
	}

	/* Restore interrupt state */
	ipl_lower(ipl);

	return satisfier == timer_i ? -1 : satisfier;
}

int wait_one(struct dispatch_header *hdr, nanoseconds_t timeout)
{
	return wait_any(1, (void *[]){ hdr }, timeout);
}

struct thread *try_satisfy_dispatch_object(struct dispatch_header *hdr)
{
	bool all = hdr->type == DISPATCH_NOTIFICATION;

	/* Go through (potentially) all waitblocks and try to satisfy them */
	while (!TAILQ_EMPTY(&hdr->waitblocks) && hdr->signaled_count > 0) {
		struct waitblock *waitblock = TAILQ_FIRST(&hdr->waitblocks);
		_Atomic enum wait_status *status = &waitblock->thread->wait_status;
		enum wait_status in_progress = WAIT_IN_PROGRESS;
		enum wait_status committed = WAIT_COMMITTED;

		/* Remove the waitblock from the queue */
		TAILQ_REMOVE(&hdr->waitblocks, waitblock, queue_hook);

		/* Three cases may occur:
		 * 1. The wait was still preparing (status == WAIT_IN_PROGRESS) and we interrupted it
		 * 2. The wait was already committed (status == WAIT_COMMITTED) and we satisfied it
		 * 3. The wait was already satisfied by another object (status == WAIT_SATISFIED)
		 */

		/* 1. */
		if (CAS(status, in_progress, WAIT_SATISFIED)) {
			/* We interrupted the wait while it was being prepared */
			waitblock->status = WAITBLOCK_SIGNALED;
			dispatch_object_consume(hdr);
		}
		/* 2. */
		else if (CAS(status, committed, WAIT_SATISFIED)) {
			/* Wait is committed and the thread is blocked. Enqueue the thread */
			dispatch_object_consume(hdr);

			waitblock->status = WAITBLOCK_SIGNALED;

			sched_wake(waitblock->thread);
		}

		/* 3. */
		else if (atomic_load(status) == WAIT_SATISFIED) {
			/* Someone else satisfied the wait, deactivate the waitblock */
			waitblock->status = WAITBLOCK_INACTIVE;
			continue;
		}

		/* We already got one, exit */
		if (!all) {
			return waitblock->thread;
		}
	}

	return NULL;
}

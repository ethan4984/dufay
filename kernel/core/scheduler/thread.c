#include <core/scheduler/thread.h>
#include <core/scheduler/processor.h>
#include <core/notification.h>
#include <core/memory/address.h>
#include <core/scheduler/cfs.h>

#include <fayt/compiler.h>
#include <fayt/capability.h>
#include <fayt/debug.h>

int create_thread(int tgid, struct thread **thread)
{
	if (unlikely(thread == NULL))
		RETURN_ERROR;

	*thread = alloc(sizeof(struct thread));
	if (unlikely(*thread == NULL))
		RETURN_ERROR;

	struct tgroup *tgroup = NULL;
	int ret = tgroup_search(tgid, &tgroup);
	if (ret == -1 || tgroup == NULL)
		RETURN_ERROR;

	int tid;
	ret = bitmap_alloc(&tgroup->tid_bitmap, &tid);
	if (ret == -1)
		RETURN_ERROR;

	(*thread)->thread_capability =
		(struct thread_capability){ .tgid = tgid, .tid = tid };
	ret = dictionary_push(&tgroup->tid_table, &(*thread)->thread_capability.tid,
						  (*thread), sizeof((*thread)->thread_capability.tid));
	if (ret == -1)
		RETURN_ERROR;

	(*thread)->notification.actions =
		alloc(sizeof(struct notification_action) * NOTIFICATION_MAX);
	if (unlikely((*thread)->notification.actions == NULL))
		RETURN_ERROR;

	(*thread)->notification.queue = alloc(sizeof(struct notification_queue));
	if (unlikely((*thread)->notification.queue == NULL))
		RETURN_ERROR;

	(*thread)->capability_table = alloc(sizeof(struct capability_table));
	if ((*thread)->capability_table == NULL)
		RETURN_ERROR;

	capability_table_init((*thread)->capability_table);

	struct thread_capability *thread_capability_self =
		alloc(sizeof(struct thread_capability));
	if (thread_capability_self == NULL)
		RETURN_ERROR;

	capability_t capability_out;
	ret = capability_create((*thread)->capability_table, thread_capability_self,
							CAPABILITY_ACCESS_READ | CAPABILITY_ACCESS_WRITE,
							&capability_out);
	if (ret == -1)
		RETURN_ERROR;
	if (capability_out != CAPABILITY_SELF_THREAD) {
		REPORT_ERROR;
		panic("");
	}

	int asid;
	ret = address_space_construct(&asid);
	if (ret == -1)
		RETURN_ERROR;

	ret = address_find_as(asid, &(*thread)->address_space);
	if (ret == -1)
		RETURN_ERROR;

	struct address_space_capability *as_capability =
		alloc(sizeof(struct address_space_capability));
	if (as_capability == NULL)
		RETURN_ERROR;

	as_capability->asid = asid;

	ret = capability_create((*thread)->capability_table, as_capability,
							CAPABILITY_ACCESS_READ | CAPABILITY_ACCESS_WRITE,
							&capability_out);
	if (ret == -1)
		RETURN_ERROR;
	if (capability_out != CAPABILITY_SELF_AS) {
		REPORT_ERROR;
		panic("");
	}

	return 0;
}

int search_thread(struct thread_capability *thread_capability,
				  struct thread **thread)
{
	if (unlikely(thread == NULL))
		RETURN_ERROR;

	struct tgroup *tgroup;
	int ret = tgroup_search(thread_capability->tgid, &tgroup);
	if (ret == -1)
		RETURN_ERROR;

	ret = dictionary_search(&tgroup->tid_table, &thread_capability->tid,
							sizeof(thread_capability->tid), (void **)thread);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int enqueue_thread(struct thread *thread)
{
	if (unlikely(thread == NULL))
		RETURN_ERROR;

	struct scheduler *scheduler = CORE_LOCAL->scheduler;
	if (unlikely(scheduler == NULL))
		RETURN_ERROR;
	if (unlikely(scheduler->enqueue == NULL))
		RETURN_ERROR;

	int ret = scheduler->enqueue(scheduler, thread);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int dequeue_thread(struct thread *thread)
{
	if (unlikely(thread == NULL))
		RETURN_ERROR;

	struct scheduler *scheduler = CORE_LOCAL->scheduler;
	if (unlikely(scheduler == NULL))
		RETURN_ERROR;
	if (unlikely(scheduler->enqueue == NULL))
		RETURN_ERROR;

	int ret = scheduler->dequeue(scheduler, thread);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int delivery_queue_peek(struct delivery_queue *queue, struct thread **thread)
{
	if (unlikely(queue == NULL || thread == NULL))
		RETURN_ERROR;

	(*thread) = queue->list;
	if (queue->list == NULL || queue->list == queue->top) {
		return 0;
	}

	if (queue->list->next)
		queue->list->next->last = NULL;
	queue->list = queue->list->next;
	delivery_queue_push(queue, (*thread));

	return 0;
}

int delivery_queue_remove(struct delivery_queue *queue, struct thread *thread)
{
	if (unlikely(queue == NULL || thread == NULL))
		RETURN_ERROR;
	if (queue->list == NULL)
		return 0;

	if (queue->list == thread) {
		queue->list = thread->next;
		if (queue->list)
			queue->list->last = NULL;
		if (queue->top)
			queue->top = NULL;
	}

	if (queue->top == thread) {
		queue->top = thread->last;
		if (queue->top)
			queue->top->next = NULL;
	}

	if (thread->last)
		thread->last->next = thread->next;
	if (thread->next)
		thread->next->last = thread->last;

	return 0;
}

int delivery_queue_push(struct delivery_queue *queue, struct thread *thread)
{
	if (unlikely(queue == NULL || thread == NULL))
		RETURN_ERROR;

	if (queue->list)
		thread->next = queue->list->next;
	thread->last = NULL;

	if (queue->top)
		queue->top->next = queue->list;

	queue->top = queue->list;
	queue->list = thread;

	return 0;
}

static struct bitmap tgroup_bitmap;
static struct dictionary tgroup_table;

int tgroup_search(int tgid, struct tgroup **tgroup)
{
	if (unlikely(tgroup == NULL))
		RETURN_ERROR;

	int ret =
		dictionary_search(&tgroup_table, &tgid, sizeof(tgid), (void **)tgroup);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int tgroup_insert(struct tgroup *tgroup)
{
	if (unlikely(tgroup == NULL))
		RETURN_ERROR;

	int ret = bitmap_alloc(&tgroup_bitmap, &tgroup->tgid);
	if (ret == -1)
		RETURN_ERROR;

	tgroup->tid_bitmap =
		(struct bitmap){ .data = NULL, .size = 1, .resizable = true };

	ret = dictionary_push(&tgroup_table, &tgroup->tgid, tgroup,
						  sizeof(tgroup->tgid));
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int tgroup_remove(int tgid)
{
	int ret = dictionary_delete(&tgroup_table, &tgid, sizeof(tgid));
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

#include <core/scheduler/rr.h>
struct scheduler **scheduler_table;

int launch_schedulers(void)
{
	scheduler_table = alloc(sizeof(struct scheduler *) * logical_processor_cnt);
	if (scheduler_table == NULL)
		RETURN_ERROR;

	for (size_t i = 0; i < logical_processor_cnt; i++) {
		struct scheduler *scheduler = alloc(sizeof(struct scheduler));
		if (scheduler == NULL)
			RETURN_ERROR;

		logical_processor_locales[i].scheduler = scheduler;

		scheduler->timer = invariant_tsc;
		scheduler->timer.read = NULL;
		scheduler->processor_id = i;
		scheduler->load = 0;
		scheduler->slice =
			(struct time){ .sec = 0, .nsec = MS_TO_NS(SCHED_TICK_RATE_MS) };

#if defined(CONFIG_SCHED_RR)
		scheduler->enqueue = rr_enqueue;
		scheduler->dequeue = rr_dequeue;
		scheduler->traverse = rr_traverse;
		scheduler->init = rr_init;
		scheduler->destroy = rr_destroy;
#elif defined(CONFIG_SCHED_CFS)
		scheduler->enqueue = cfs_enqueue;
		scheduler->dequeue = cfs_dequeue;
		scheduler->traverse = cfs_traverse;
		scheduler->init = cfs_init;
		scheduler->destroy = cfs_destroy;
#else
#error \
	"No scheduler configured. Please define CONFIG_SCHED_RR or CONFIG_SCHED_CFS."
#endif

		int ret = scheduler->init(scheduler);
		if (ret == -1)
			RETURN_ERROR;

		scheduler_table[i] = scheduler;
	}

	return 0;
}

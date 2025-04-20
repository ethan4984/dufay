#include <core/scheduler/cfs.h>
#include <core/scheduler/thread.h>
#include <core/scheduler/processor.h>
#include <core/lock.h>

#include <fayt/compiler.h>
#include <fayt/debug.h>

static int cfs_internal_enqueue(struct scheduler *, struct thread *, int);

int cfs_enqueue(struct scheduler *scheduler, struct thread *thread)
{
	return cfs_internal_enqueue(scheduler, thread, true);
}

static int cfs_internal_enqueue(struct scheduler *scheduler,
								struct thread *thread, int offload)
{
	if (unlikely(thread == NULL))
		RETURN_ERROR;
	if (unlikely(scheduler == NULL))
		RETURN_ERROR;

	struct cfs *cfs = scheduler->private;
	if (unlikely(cfs == NULL))
		RETURN_ERROR;

	spinlock_irqsave(&scheduler->lock);

	if (offload) {
		struct scheduler *optimal = scheduler;
		for (size_t i = 0; i < logical_processor_cnt; i++) {
			struct scheduler *candidate = scheduler_table[i];
			if (optimal->load > candidate->load)
				optimal = candidate;
		}

		if (unlikely(optimal == NULL)) {
			RETURN_ERROR;
		}

		if (optimal == scheduler)
			goto exit;

		thread->scheduler = optimal;

		int ret = cfs_internal_enqueue(optimal, thread, false);
		if (ret == -1)
			RETURN_ERROR;

		return ret;
	}
exit:
	scheduler->load++;

	struct cfs_unit *unit = thread->private;
	if (unit == NULL) {
		unit = alloc(sizeof(struct cfs_unit));
		if (unlikely(unit == NULL))
			RETURN_ERROR;

		unit->thread = thread;
	}

	int ret = hash_table_push(cfs->unit_table, &unit->thread->thread_capability,
							  unit, sizeof(struct thread_capability));
	if (ret == -1) {
		spinrelease_irqsave(&scheduler->lock);
		RETURN_ERROR;
	}

	ret = RB_GENERIC_INSERT(cfs->unit_tree, vruntime, unit);
	if (ret == -1) {
		spinrelease_irqsave(&scheduler->lock);
		RETURN_ERROR;
	}

	spinrelease_irqsave(&scheduler->lock);

	return 0;
}

int cfs_dequeue(struct scheduler *scheduler, struct thread *thread)
{
	if (unlikely(thread == NULL))
		RETURN_ERROR;
	if (unlikely(scheduler == NULL))
		RETURN_ERROR;

	struct cfs *cfs = scheduler->private;
	if (unlikely(cfs == NULL))
		RETURN_ERROR;

	struct cfs_unit *unit = thread->private;
	if (unit == NULL)
		return 0;

	spinlock_irqsave(&scheduler->lock);

	int ret = hash_table_delete(cfs->unit_table,
								&unit->thread->thread_capability,
								sizeof(struct thread_capability));
	if (ret == -1) {
		spinrelease_irqsave(&scheduler->lock);
		RETURN_ERROR;
	}

	//print("dequeueing thread cid=%x\n", thread->proc_id.cid);

	ret = RB_GENERIC_DELETE(cfs->unit_tree, vruntime, unit);
	if (ret == -1) {
		spinrelease_irqsave(&scheduler->lock);
		RETURN_ERROR;
	}

	struct time epoch = scheduler->timer.read(&scheduler->timer);
	struct time delta = time_sub(epoch, unit->epoch);
	unit->vruntime -= VRUNTIME(unit->weight, time_to_ns(delta));

	spinrelease_irqsave(&scheduler->lock);

	return 0;
}

int cfs_traverse(struct scheduler *scheduler, struct thread **thread)
{
	if (thread == NULL || scheduler == NULL)
		RETURN_ERROR;

	struct cfs *cfs = scheduler->private;
	if (unlikely(cfs == NULL))
		RETURN_ERROR;

	spinlock_irqsave(&scheduler->lock);

	struct cfs_unit *enqueue = cfs->unit_tree;
	while (enqueue && enqueue->left)
		enqueue = enqueue->left;
	*thread = enqueue->thread;

	spinrelease_irqsave(&scheduler->lock);

	return 0;
}

int cfs_init(struct scheduler *scheduler)
{
	if (unlikely(scheduler == NULL))
		RETURN_ERROR;

	scheduler->private = alloc(sizeof(struct cfs));
	if (unlikely(scheduler->private == NULL))
		RETURN_ERROR;

	struct cfs *cfs = scheduler->private;

	cfs->unit_table = alloc(sizeof(struct hash_table));
	if (unlikely(cfs->unit_table == NULL))
		RETURN_ERROR;

	return 0;
}

static int cfs_destroy_tree(struct cfs_unit *root)
{
	if (unlikely(root == NULL))
		RETURN_ERROR;

	cfs_destroy_tree(root->left);
	cfs_destroy_tree(root->right);

	free(root);

	return 0;
}

int cfs_destroy(struct scheduler *scheduler)
{
	if (unlikely(scheduler == NULL))
		RETURN_ERROR;

	struct cfs *cfs = scheduler->private;
	if (unlikely(cfs == NULL))
		RETURN_ERROR;

	int ret = cfs_destroy_tree(cfs->unit_tree);
	if (unlikely(ret == -1))
		RETURN_ERROR;

	free(cfs->unit_table);
	free(cfs);

	return 0;
}

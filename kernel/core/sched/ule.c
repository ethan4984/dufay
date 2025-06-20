#include <core/sched/sched.h>
#include <core/cpu.h>

static inline void calendar_queue_increment(struct cpu_local *cpu)
{
	/* Increment the calendar queue runq index */
	cpu->calendar_queue_runidx = (cpu->calendar_queue_runidx + 1) % RUNQUEUES_N;

	if (cpu->calendar_queue_runidx == cpu->calendar_queue_insidx) {
		/* Make sure insq is always ahead of runq */
		cpu->calendar_queue_insidx =
			(cpu->calendar_queue_insidx + 1) % RUNQUEUES_N;
	}
}

static struct thread *pick_realtime_thread(struct cpu_local *cpu)
{
	struct thread *td;
	struct runqueue *runq = &cpu->realtime_runq;

	if (runq->status == 0) {
		/* runq is empty */
		return NULL;
	}

	/* Search higher priorities queues first */
	for (int i = RUNQUEUES_N; i >= 0; i--) {
		/* Queue is not empty */
		if (runq->status & (1 << i)) {
			/* Pop the first thread off the queue */
			td = TAILQ_FIRST(&runq->queues[i]);

			TAILQ_REMOVE(&runq->queues[i], td, runqueue_hook);

			if (TAILQ_EMPTY(&runq->queues[i])) {
				runq->status &= ~(1 << i); /* Clear bit */
			}

			return td;
		}
	}

	return NULL;
}

static struct thread *pick_batch_thread(struct cpu_local *cpu)
{
	struct thread *td = NULL;
	struct runqueue *runq = &cpu->calendar_queue;
	size_t cnt = 0;

	if (runq->status == 0) {
		/* runq is empty */
		return NULL;
	}

	/* Search all queues in priority order, starting from `calendar_queue_runq` */
	for (size_t i = cpu->calendar_queue_runidx; i; i++) {
		/* We've gone through all queues, give up. */
		if (cnt >= RUNQUEUES_N) {
			break;
		}

		/* Non-empty queue */
		if (runq->status & (1 << i)) {
			/* Pick the first thread from the queue */
			td = TAILQ_FIRST(&runq->queues[i]);

			TAILQ_REMOVE(&runq->queues[i], td, runqueue_hook);

			/* If the queue is now empty, clear the status bit and increment runidx */
			if (TAILQ_EMPTY(&runq->queues[i])) {
				runq->status &= ~(1 << i);

				calendar_queue_increment(cpu);
			}

			return td;
		}

		cnt++;
	}

	return NULL;
}

static struct thread *pick_idle_thread(struct cpu_local *cpu)
{
	struct thread *td;
	td = TAILQ_FIRST(&cpu->idle_queue);

	if (td) {
		TAILQ_REMOVE(&cpu->idle_queue, td, runqueue_hook);
		return td;
	}

	return NULL;
}

static struct thread *pick_next_thread(struct cpu_local *cpu)
{
	struct thread *td;

	/* First try to get from realtime queues */
	td = pick_realtime_thread(cpu);

	if (td) {
		return td;
	}

	/* If no realtime threads, try the calendar queues */
	td = pick_batch_thread(cpu);

	if (td) {
		return td;
	}

	/* If no threads in the calendar queues, try the idle queue */
	td = pick_idle_thread(cpu);

	if (td) {
		return td;
	}

	/* Didn't find anything */
	return NULL;
}

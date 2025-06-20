#include "cpu.h"
#include "fayt/lock.h"
#include "ule.h"
#include <pthread.h>
#include <stdio.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>

#define HZ 100

#define SCALING_FACTOR 50
#define INTERACTIVITY_THRESHOLD 30

#define SECONDS_TO_TICKS(seconds) ((seconds) * HZ)
#define TICKS_TO_SECONDS(ticks) ((ticks) / HZ)

#define LOAD_BALANCE_LIMIT 16

/*
 * Timeshared threads are not preempted by other timeshared threads by default.
 * This is meant to reproduce the behavior of the default value of FreeBSD's preemption threshold, which essentially preempts only realtime threads.
 */
#define PREEMPT_THRESHOLD PRIO_REALTIME

extern thread_local struct cpu_local *CORE_LOCAL;

static struct cpu_local *cpus[8];
static _Atomic int ncpus = 0;

static inline void calendar_queue_increment(struct cpu_local *cpu)
{
	cpu->calendar_queue_runidx = (cpu->calendar_queue_runidx + 1) % RUNQUEUES_N;

	/* Ensure insidx is always one ahead of runidx */
	if (CORE_LOCAL->calendar_queue_runidx ==
		CORE_LOCAL->calendar_queue_insidx) {
		CORE_LOCAL->calendar_queue_insidx =
			(CORE_LOCAL->calendar_queue_insidx + 1) % RUNQUEUES_N;
	}
}

static struct thread *pick_realtime_thread(struct cpu_local *cpu, int minprio)
{
	struct thread *td;
	struct runqueue *runq = &cpu->realtime_runq;

	if (runq->status == 0) {
		/* runq is empty */
		return NULL;
	}

	/* Search higher priorities queues first */
	for (int i = minprio; i <= PRIO_MAX; i++) {
		/* Queue is not empty */
		if (runq->status & (1UL << i)) {
			/* Pop the first thread off the queue */
			td = TAILQ_FIRST(&runq->queues[i]);

			TAILQ_REMOVE(&runq->queues[i], td, runqueue_hook);

			if (TAILQ_EMPTY(&runq->queues[i])) {
				runq->status &= ~(1UL << i); /* Clear bit */
			}

			return td;
		}
	}

	return NULL;
}

static struct thread *pick_batch_thread(struct cpu_local *cpu, bool migrate)
{
	struct thread *td = NULL;
	struct runqueue *runq = &cpu->calendar_queue;
	size_t cnt = 0;

	if (runq->status == 0) {
		/* runq is empty */
		return NULL;
	}

	/* Search all queues in priority order, starting from `calendar_queue_runidx` */
	for (size_t i = cpu->calendar_queue_runidx;; i++) {
		if (i >= RUNQUEUES_N) {
			i = 0;
		}

		/* We've gone through all queues, give up. */
		if (cnt >= RUNQUEUES_N) {
			break;
		}

		/* Non-empty queue */
		if (runq->status & (1UL << i)) {
			/* Pick the first thread from the queue */
			td = TAILQ_FIRST(&runq->queues[i]);

			TAILQ_REMOVE(&runq->queues[i], td, runqueue_hook);

			/* If the queue is now empty, clear the status bit and increment runidx */
			if (TAILQ_EMPTY(&runq->queues[i])) {
				runq->status &= ~(1UL << i);

				if (!migrate)
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

static int interactive_score(struct thread *td)
{
	int penalty;

	if (!td->sleeptime || !td->runtime) {
		return 100;
	}

	/* Calculate the interactivity penalty */
	if (td->sleeptime > td->runtime) {
		penalty = SCALING_FACTOR / (td->sleeptime / td->runtime);
	} else {
		penalty =
			(SCALING_FACTOR / (td->runtime / td->sleeptime)) + SCALING_FACTOR;
	}

	/*
	 * Add niceness values to the penalty, this makes it easier for threads with
	 * lower nice values (higher priority) to be considered interactive
	 */
	penalty += td->nice;

	return penalty;
}

static inline void insert_calendar_queue(struct cpu_local *cpu,
										 struct thread *td)
{
	/*
	 * The insertion index is determined by insidx and the the priority of the thread,
	 * higher priority threads will be put closer to insidx, which ensures that they are ran more frequently.
	 */
	size_t idx =
		(cpu->calendar_queue_insidx + (PRIO_HIGH_BATCH - td->priority)) %
		RUNQUEUES_N;

	cpu->calendar_queue.status |= (1UL << idx);

	TAILQ_INSERT_TAIL(&cpu->calendar_queue.queues[idx], td, runqueue_hook);
}

static void find_most_and_least_loaded_cpu(struct cpu_local **most,
										   struct cpu_local **least)
{
	struct cpu_local *cpu;
	*least = *most = cpus[0];

	/**
	 * Look at all CPUs and find the most and the least loaded one.
	 * This is racey on purpose, as taking all the locks would lead to very high contention
	 * And a slight imbalance is fine.
	 */
	for (int i = 0; i < ncpus; i++) {
		cpu = cpus[i];

		if (cpu->load < (*least)->load) {
			*least = cpu;
		}

		if (cpu->load > (*most)->load) {
			*most = cpu;
		}
	}
}

static void sched_insert_in_queue(struct cpu_local *cpu, struct thread *thread)
{
	if (thread->priority_class == PRIO_REALTIME || thread->interactive) {
		/* Add to realtime runqueue */
		cpu->realtime_runq.status |= (1UL << thread->priority);
		TAILQ_INSERT_TAIL(&cpu->realtime_runq.queues[thread->priority], thread,
						  runqueue_hook);
	} else if (thread->priority_class == PRIO_LOW_BATCH) {
		/* Add to calendar queue */
		insert_calendar_queue(cpu, thread);
	} else if (thread->priority_class == PRIO_IDLE) {
		/* Add to idle queue */
		TAILQ_INSERT_TAIL(&cpu->idle_queue, thread, runqueue_hook);
	}

	thread->state = READY;
	cpu->load++;
}

static void sched_update_interactivity(struct thread *td)
{
	uint64_t max = SECONDS_TO_TICKS(5);
	size_t sum = td->runtime + td->sleeptime;

	if (sum < max) {
		/* If the sum of runtime and sleeptime is less than 5 seconds, do nothing */
		return;
	}

	if (sum > max * 2) {
		if (td->runtime > td->sleeptime) {
			td->runtime = max;
			td->sleeptime = 1;
		} else {
			td->sleeptime = max;
			td->runtime = 1;
		}
		return;
	}

	if (sum > ((max / 5) * 6)) {
		td->runtime /= 2;
		td->sleeptime /= 2;
		return;
	}

	/* Decay values by 20% */
	td->runtime = (td->runtime / 5) * 4;
	td->sleeptime = (td->sleeptime / 5) * 4;

	td->interactive = interactive_score(td) < INTERACTIVITY_THRESHOLD;
}

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

static void sched_recompute_priority(struct cpu_local *cpu, struct thread *td)
{
	/*
   	* Priority is computed only for timeshared (batch) threads based on
   	* interactivity and nice.
   	* If the thread is determined interactive, it is effectively promoted to realtime with a
   	* lower priority than actual realtime threads.
   	* If the thread is determined non-interactive, priority is calculated based
   	* on recent CPU usage and nice.
   	*/

	int score = interactive_score(td);

	if (score < INTERACTIVITY_THRESHOLD) {
		/*
		 * Choose a priority based on score, the lower the score, the higher
		 * priority it will be.
		 * This is a simple formula I came up with that is probably good enough.
		 */
		td->priority =
			PRIO_INTERACTIVE + ((INTERACTIVITY_THRESHOLD - score) / 4);
		td->interactive = true;
	} else {
		/*
		 * Calculate the priority based on CPU usage and nice.
		 * This is another simple formula that takes into account the number of CPU
		 * ticks spent running the thread overall (using cpu->ticks and
		 * thread->ticks), the more a thread runs, the lower its priority.
		 * nice is then simply added to the priority (inverted
		 * because lower nice means higher priority!).
		 * This *should* be good enough for now.
		 */
		td->priority =
			DIV_ROUND_UP(cpu->ticks, (td->ticks * (PRIO_BATCH_RANGE / 2))) -
			td->nice;

		/* Clamp it */
		if (td->priority < PRIO_LOW_BATCH) {
			td->priority = PRIO_LOW_BATCH;
		} else if (td->priority > PRIO_HIGH_BATCH) {
			td->priority = PRIO_HIGH_BATCH;
		}

		td->interactive = false;
	}
}

/* Returns whether or not td should preempt curtd */
static inline bool should_preempt(struct thread *td, struct thread *curtd)
{
	/* Always preempt idle threads */
	if (curtd->priority == PRIO_IDLE)
		return true;

	if (curtd->priority_class == PRIO_LOW_BATCH && !curtd->interactive &&
		td->interactive) {
		/* Interactive threads always preempt timeshared ones */
		return true;
	}

	/*
	 * Preempt if the priority exceeds the preemption threshold.
	 * The default value forbids timeshared (batch) threads to preempt each other.
	 */
	if (td->priority >= PREEMPT_THRESHOLD && td->priority > curtd->priority)
		return true;

	return false;
}

static void sched_switch(struct thread *cur, struct thread *next,
						 struct cpu_local *cpu);

static void sched_try_preempt(struct cpu_local *cpu, struct thread *td)
{
	spinlock(&cpu->thread_queues_lock);
	struct thread *curthread = cpu->current_thread;

	if (should_preempt(td, curthread)) {
		spinlock(&curthread->lock);
		sched_switch(curthread, td, cpu);

		spinrelease(&cpu->thread_queues_lock);

		return;
	}

	else {
		/* Add the thread to its appropriate queue */
		sched_insert_in_queue(cpu, td);
		spinrelease(&cpu->thread_queues_lock);
	}
}

/*
 * Find a suitable CPU to run this thread.
 */
static struct cpu_local *pick_cpu(struct thread *td)
{
	/*
	 * CPU selection policy in order:
	 * 1. Pick the last CPU the thread ran on (CPU affinity) if it would run immediately.
	 * 2. Pick the least loaded CPU on which the thread can run immediately.
	 * 3. Pick the least loaded CPU overall.
	 */

	/* Check the last CPU the thread ran on */
	if (td->last_cpu) {
		/* We can preempt it */
		if (should_preempt(td, td->last_cpu->current_thread)) {
			return td->last_cpu;
		}
	}

	/* Check all CPUs, keeping track of the least loaded one overall and the least loaded preemptible one */
	struct cpu_local *least = NULL, *least_preempt = NULL;

	for (int i = 0; i < ncpus; i++) {
		struct cpu_local *cpu = cpus[i];

		if (!least || (least && cpu->load < least->load)) {
			least = cpu;
		}

		if (should_preempt(td, cpu->current_thread)) {
			if (!least_preempt ||
				(least_preempt && cpu->load < least_preempt->load)) {
				least_preempt = cpu;
			}
		}
	}

	/* If we found a preemptible CPU, pick that one, else pick the least loaded one */
	return least_preempt ? least_preempt : least;
}

static void sched_switch(struct thread *cur, struct thread *next,
						 struct cpu_local *cpu)
{
	if (cur != &cpu->idle_thread) {
		sched_insert_in_queue(cpu, cur);
	}

	printf("cpu%d: %s\n", CORE_LOCAL->core_id, next->name);

	next->state = RUNNING;
	cpu->current_thread = next;
	cpu->next_thread = NULL;
	next->last_cpu = CORE_LOCAL;

	if (CORE_LOCAL == cpu && next->entry) {
		next->entry();
	}
}

static struct thread *sched_select_thread(struct thread *cur,
										  struct cpu_local *cpu, bool migrate)
{
	struct thread *td;

	/* First try to get from realtime queues */
	td = pick_realtime_thread(cpu, cur ? cur->priority : 0);

	if (td) {
		cpu->load--;
		return td;
	}

	/* Nothing lower can preempt this thread */
	if (cur && (cur->priority_class == PRIO_REALTIME || cur->interactive)) {
		return NULL;
	}

	/* If no realtime thread was found, try getting a timeshared thread */
	td = pick_batch_thread(cpu, migrate);

	if (td) {
		cpu->load--;
		return td;
	}

	/* Idle threads don't preempt each other */
	if (cur && cur->priority == PRIO_IDLE) {
		return NULL;
	}

	/* If no threads in the calendar queues, try the idle queue */
	td = pick_idle_thread(cpu);

	if (td) {
		cpu->load--;
		return td;
	}

	/* Nothing to run */
	return NULL;
}

/*
 * It is useless to steal from a cpu if that CPU is only running one thread (load=0)
 * A load of 1 means there's 1 ready thread waiting in the queues.
*/
#define STEAL_THRESHOLD 1

static void idle_thread()
{
	/* Find the most loaded CPU and steal a thread from it */
	struct cpu_local *most, *least;

	find_most_and_least_loaded_cpu(&most, &least);

	spinlock(&most->thread_queues_lock);

	/* Don't bother. */
	if (most->load < STEAL_THRESHOLD) {
		spinrelease(&most->thread_queues_lock);
		return;
	}

	/* Find a thread to steal. Anything is fine */
	struct thread *td = sched_select_thread(NULL, most, true);

	if (!td) {
		spinrelease(&most->thread_queues_lock);
		return;
	}

	spinrelease(&most->thread_queues_lock);

	spinlock(&td->lock);

	printf("idle: stole %s from cpu%d\n", td->name, most->core_id);

	sched_try_preempt(CORE_LOCAL, td);

	spinrelease(&td->lock);
}

/*
 * Try to balance load between the CPUs.
 * This is called once per second by cpu0
 */
static void balance_load()
{
	struct cpu_local *most, *least;
	ssize_t load_diff = 0;
	size_t cnt = LOAD_BALANCE_LIMIT;

	while (cnt--) {
		find_most_and_least_loaded_cpu(&most, &least);

		if (!most || !least)
			return;

		load_diff = most->load - least->load;

		/* We're done here */
		if (load_diff < 1) {
			break;
		}

		spinlock(&most->thread_queues_lock);

		/* Pick a thread on the most loaded CPU */
		struct thread *td = sched_select_thread(NULL, most, true);

		/* !! This should not happen, but still check for it */
		if (!td) {
			spinrelease(&most->thread_queues_lock);
			return;
		}

		spinrelease(&most->thread_queues_lock);

		spinlock(&td->lock);

		/* Move the thread into the least loaded CPU */
		sched_try_preempt(least, td);

		spinrelease(&td->lock);

		/* We just shuffled the threads, no point in doing that again */
		if (load_diff == 1) {
			break;
		}

		/* Now do it again! */
	}
}

void sched_clock()
{
	struct thread *td = CORE_LOCAL->current_thread;

	CORE_LOCAL->ticks++;

	spinlock(&td->lock);
	spinlock(&CORE_LOCAL->thread_queues_lock);

	/*
	 * Advance the insert index every tick, whilst keeping a separation of 1 with the runidx
	 * This ensures fairness
	 */
	if (CORE_LOCAL->calendar_queue_runidx ==
		CORE_LOCAL->calendar_queue_insidx) {
		CORE_LOCAL->calendar_queue_insidx =
			(CORE_LOCAL->calendar_queue_insidx + 1) % RUNQUEUES_N;
	}

	/* Every second, do load balancing on core 0 */
	if (CORE_LOCAL->core_id == 0 && !(CORE_LOCAL->ticks % 10000)) {
		spinrelease(&CORE_LOCAL->thread_queues_lock);
		spinrelease(&td->lock);
		/* This should be a software interrupt */
		balance_load();
		spinlock(&td->lock);
		spinlock(&CORE_LOCAL->thread_queues_lock);
	}

	if (td->priority_class == PRIO_LOW_BATCH) {
		/* Charge one tick of runtime to this thread */
		td->runtime += 1;
		td->ticks += 1;

		sched_update_interactivity(td);
		sched_recompute_priority(CORE_LOCAL, td);
	}

	/* Pick a new thread to run */
	struct thread *newtd = CORE_LOCAL->next_thread =
		sched_select_thread(td, CORE_LOCAL, false);

	if (newtd) {
		sched_switch(td, newtd, CORE_LOCAL);
		spinrelease(&CORE_LOCAL->thread_queues_lock);
		spinrelease(&td->lock);
	} else {
		spinrelease(&CORE_LOCAL->thread_queues_lock);
		spinrelease(&td->lock);

		printf("cpu%d: %s\n", CORE_LOCAL->core_id, td->name);

		if (td->entry) {
			td->entry();
		}
	}
}

void sched_setup()
{
	CORE_LOCAL = calloc(1, sizeof(struct cpu_local));

	CORE_LOCAL->calendar_queue_insidx = 0;
	CORE_LOCAL->calendar_queue_runidx = 0;

	CORE_LOCAL->calendar_queue.status = 0;
	CORE_LOCAL->realtime_runq.status = 0;

	TAILQ_INIT(&CORE_LOCAL->idle_queue);

	for (int i = 0; i < RUNQUEUES_N; i++) {
		TAILQ_INIT(&CORE_LOCAL->realtime_runq.queues[i]);
	}

	for (int i = 0; i < RUNQUEUES_N; i++) {
		TAILQ_INIT(&CORE_LOCAL->calendar_queue.queues[i]);
	}

	strcpy(CORE_LOCAL->idle_thread.name, "idle");

	CORE_LOCAL->idle_thread.priority = PRIO_IDLE;
	CORE_LOCAL->idle_thread.entry = idle_thread;
	CORE_LOCAL->current_thread = &CORE_LOCAL->idle_thread;
}

void sched_add_cpu(struct cpu_local *cpu)
{
	cpus[ncpus] = cpu;
	atomic_fetch_add(&ncpus, 1);
}

void sched_enqueue(struct thread *td)
{
	spinlock(&td->lock);
	struct cpu_local *cpu = pick_cpu(td);
	sched_try_preempt(cpu, td);
	spinrelease(&td->lock);
}

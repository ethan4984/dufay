#include <core/ipl.h>
#include <core/lock.h>
#include <arch/port.h>
#include <core/cpu.h>
#include <core/sched.h>
#include <aria/base.h>
#include <mm/address.h>
#include <mm/slab.h>
#include <core/timer.h>
#include <mm/physical.h>
#include <core/debug.h>
#include <core/thread.h>

#define SCALING_FACTOR 50
#define INTERACTIVITY_THRESHOLD 30

#define SECONDS_TO_MS(S) (S * 1000)
#define TICKS_TO_MS(ticks) ((ticks * 1000) / HZ)

#define LOAD_BALANCE_LIMIT 16

/*
 * Timeshared threads are not preempted by other timeshared threads by default.
 * This is meant to reproduce the behavior of the default value of FreeBSD's preemption threshold, which essentially preempts only realtime threads.
 */
#define PREEMPT_THRESHOLD PRIO_REALTIME

static inline void calendar_queue_increment(struct cpu_local *cpu)
{
	cpu->sched_data.calendar_queue_runidx =
		(cpu->sched_data.calendar_queue_runidx + 1) % RUNQUEUES_N;

	/* Ensure insidx is always one ahead of runidx */
	if (cpu->sched_data.calendar_queue_runidx ==
		cpu->sched_data.calendar_queue_insidx) {
		cpu->sched_data.calendar_queue_insidx =
			(cpu->sched_data.calendar_queue_insidx + 1) % RUNQUEUES_N;
	}
}

static struct thread *pick_realtime_thread(struct cpu_local *cpu, int minprio,
										   bool migrate)
{
	struct thread *td;
	struct runqueue *runq = &cpu->sched_data.realtime_runq;

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

			if (migrate) {
				/* Find the first thread that's not pinned */
				while (td->pinned) {
					td = TAILQ_NEXT(td, runqueue_hook);

					/* Every thread is pinned, go through another queue */
					if (!td) {
						goto retry;
						break;
					}
				}
			}

			TAILQ_REMOVE(&runq->queues[i], td, runqueue_hook);

			if (TAILQ_EMPTY(&runq->queues[i])) {
				runq->status &= ~(1UL << i); /* Clear bit */
			}

			return td;
		}

retry:
	}

	return NULL;
}

static struct thread *pick_batch_thread(struct cpu_local *cpu, bool migrate)
{
	struct thread *td = NULL;
	struct runqueue *runq = &cpu->sched_data.calendar_queue;
	size_t cnt = 0;

	if (runq->status == 0) {
		/* runq is empty */
		return NULL;
	}

	/* Search all queues in priority order, starting from `calendar_queue_runidx` */
	for (size_t i = cpu->sched_data.calendar_queue_runidx;; i++) {
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

			if (migrate) {
				/* Find the first thread that's not pinned */
				while (td->pinned) {
					td = TAILQ_NEXT(td, runqueue_hook);

					/* Every thread is pinned, go through another queue */
					if (!td) {
						goto retry;
						break;
					}
				}
			}

			TAILQ_REMOVE(&runq->queues[i], td, runqueue_hook);

			/* If the queue is now empty, clear the status bit and increment runidx */
			if (TAILQ_EMPTY(&runq->queues[i])) {
				runq->status &= ~(1UL << i);

				if (!migrate)
					calendar_queue_increment(cpu);
			}

			return td;
		}
retry:
		cnt++;
	}

	return NULL;
}

static struct thread *pick_idle_thread(struct cpu_local *cpu, bool migrate)
{
	struct thread *td;
	td = TAILQ_FIRST(&cpu->sched_data.idle_queue);

	if (td && !(migrate && td->pinned)) {
		TAILQ_REMOVE(&cpu->sched_data.idle_queue, td, runqueue_hook);
		return td;
	}

	return NULL;
}

static int interactive_score(struct thread *td)
{
	int penalty;

	if (!td->sleeptime) {
		return 100;
	} else if (!td->runtime) {
		return 0;
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
	size_t idx = (cpu->sched_data.calendar_queue_insidx +
				  (PRIO_HIGH_BATCH - td->priority)) %
				 RUNQUEUES_N;

	cpu->sched_data.calendar_queue.status |= (1UL << idx);

	TAILQ_INSERT_TAIL(&cpu->sched_data.calendar_queue.queues[idx], td,
					  runqueue_hook);
}

static void find_most_and_least_loaded_cpu(struct cpu_local **most,
										   struct cpu_local **least)
{
	struct cpu_local *cpu;
	*least = *most = &logical_processor_locales[0];

	/**
	 * Look at all CPUs and find the most and the least loaded one.
	 * This is racey on purpose, as taking all the locks would lead to very high contention
	 * And a slight imbalance is fine.
	 */
	for (size_t i = 0; i < logical_processor_cnt; i++) {
		cpu = &logical_processor_locales[i];

		if (cpu->sched_data.load < (*least)->sched_data.load) {
			*least = cpu;
		}

		if (cpu->sched_data.load > (*most)->sched_data.load) {
			*most = cpu;
		}
	}
}

static void sched_insert_in_queue(struct cpu_local *cpu, struct thread *thread)
{
	if (thread->priority_class == PRIO_REALTIME || thread->interactive) {
		/* Add to realtime runqueue */
		cpu->sched_data.realtime_runq.status |= (1UL << thread->priority);
		TAILQ_INSERT_TAIL(
			&cpu->sched_data.realtime_runq.queues[thread->priority], thread,
			runqueue_hook);
		thread->runq = &cpu->sched_data.realtime_runq;
	} else if (thread->priority_class == PRIO_LOW_BATCH) {
		/* Add to calendar queue */
		insert_calendar_queue(cpu, thread);
		thread->runq = &cpu->sched_data.calendar_queue;
	} else if (thread->priority_class == PRIO_IDLE) {
		/* Add to idle queue */
		TAILQ_INSERT_TAIL(&cpu->sched_data.idle_queue, thread, runqueue_hook);
	}

	thread->state = READY;
	cpu->sched_data.load++;
}

static void sched_remove_from_queue(struct cpu_local *cpu, struct thread *td)
{
	struct runqueue *runq = td->runq;

	cpu->sched_data.load--;

	if (!runq) {
		/* Must be an idle thread */
		TAILQ_REMOVE(&cpu->sched_data.idle_queue, td, runqueue_hook);
		return;
	}

	/* Remove the thread from its associated queue */
	TAILQ_REMOVE(&runq->queues[td->priority], td, runqueue_hook);

	/* Clear status bit if empty */
	if (TAILQ_EMPTY(&runq->queues[td->priority])) {
		runq->status &= ~(1 << td->priority);
	}
}

static void sched_clamp_time(struct thread *td)
{
	uint64_t max = SECONDS_TO_MS(5);
	size_t sum = td->runtime + td->sleeptime;

	if (sum < max) {
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
}

static void sched_update_interactivity(struct thread *td)
{
	sched_clamp_time(td);

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

		/* td->ticks being 0 can occur when a thread immediately went to sleep and just woke up */
		uint64_t ticks = td->ticks ? td->ticks : 1;
		td->priority =
			DIV_ROUND_UP(cpu->ticks, (ticks * (PRIO_BATCH_RANGE / 2))) -
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

/* Returns whether or not `td` should preempt the thread running on `cpu` */
static inline bool should_preempt(struct thread *td, struct cpu_local *cpu)
{
	uint8_t status = cpu->current_thread_status;
	uint8_t prio = (status >> 1) & 0x7F;
	bool interact = status & 1;

	/* Always preempt idle threads */
	if (prio == PRIO_IDLE)
		return true;

	if (PRIO_IS_BATCH(prio) && !interact && td->interactive) {
		/* Interactive threads always preempt timeshared ones */
		return true;
	}

	/*
	 * Preempt if the priority exceeds the preemption threshold.
	 * The default value forbids timeshared (batch) threads to preempt each other.
	 */
	if (td->priority >= PREEMPT_THRESHOLD && td->priority > prio)
		return true;

	return false;
}

static void sched_try_preempt(struct cpu_local *cpu, struct thread *td)
{
	spinlock(&cpu->sched_data.thread_queues_lock);

	if (should_preempt(td, cpu)) {
		struct thread *next = cpu->next_thread;

		cpu->next_thread = td;
		cpu->preemption_reason = PREEMPT_HIGHER_PRIORITY;

		if (next) {
			sched_insert_in_queue(cpu, next);
		}

		set_softint_pending(cpu, IPL_DISPATCH);

		if (cpu != CORE_LOCAL) {
			arch_send_ipi(cpu, IPI_DPC);
		}

	} else {
		/* Add the thread to its appropriate queue */
		sched_insert_in_queue(cpu, td);
	}

	spinrelease(&cpu->sched_data.thread_queues_lock);
}

/*
 * Find a suitable CPU to run this thread.
 */
static struct cpu_local *pick_cpu(struct thread *td)
{
	/*
	 * CPU selection policy in order:
	 * 1. Pick the last CPU the thread ran on (CPU affinity) if it would run immediately or if it's pinned.
	 * 2. Pick the least loaded CPU on which the thread can run immediately.
	 * 3. Pick the least loaded CPU overall.
	 */

	/* Check the last CPU the thread ran on (or the one it's pinned to) */
	if (td->last_cpu) {
		/* We can preempt it or the thread is pinned */
		if (should_preempt(td, td->last_cpu) || td->pinned) {
			return td->last_cpu;
		}
	}

	/* Check all CPUs, keeping track of the least loaded one overall and the least loaded preemptible one */
	struct cpu_local *least = NULL, *least_preempt = NULL;

	for (size_t i = 0; i < logical_processor_cnt; i++) {
		struct cpu_local *cpu = &logical_processor_locales[i];

		if (!least ||
			(least && cpu->sched_data.load < least->sched_data.load)) {
			least = cpu;
		}

		if (should_preempt(td, cpu)) {
			if (!least_preempt ||
				(least_preempt &&
				 cpu->sched_data.load < least_preempt->sched_data.load)) {
				least_preempt = cpu;
			}
		}
	}

	/* If we found a preemptible CPU, pick that one, else pick the least loaded one */
	return least_preempt ? least_preempt : least;
}

void sched_switch(struct thread *cur, struct thread *next)
{
	struct cpu_local *cpu = CORE_LOCAL;

	if (cur != &cpu->idle_thread && cur->state == RUNNING) {
		sched_insert_in_queue(cpu, cur);
	}

	next->state = RUNNING;
	cpu->current_thread = next;
	cpu->next_thread = NULL;
	next->last_cpu = cpu;

	/* We stash this to ensure that remote preemption checks can be done locklessly (without loading from cpu->current_thread) */
	cpu->current_thread_status = ((next->priority & 0x7F) << 1) |
								 (next->interactive);

	thread_switch(cur, next);
}

static struct thread *sched_select_thread(struct thread *cur,
										  struct cpu_local *cpu, bool migrate)
{
	struct thread *td;

	/* First try to get from realtime queues */
	td = pick_realtime_thread(cpu, cur ? cur->priority : 0, migrate);

	if (td) {
		cpu->sched_data.load--;
		return td;
	}

	/* Nothing lower can preempt this thread */
	if (cur && (cur->priority_class == PRIO_REALTIME || cur->interactive)) {
		return NULL;
	}

	/* If no realtime thread was found, try getting a timeshared thread */
	td = pick_batch_thread(cpu, migrate);

	if (td) {
		cpu->sched_data.load--;
		return td;
	}

	/* Idle threads don't preempt each other */
	if (cur && cur->priority == PRIO_IDLE) {
		return NULL;
	}

	/* If no threads in the calendar queues, try the idle queue */
	td = pick_idle_thread(cpu, migrate);

	if (td) {
		cpu->sched_data.load--;
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
	for (;;) {
		ipl_t ipl = ipldispatch();

		/* Find the most loaded CPU and steal a thread from it */
		struct cpu_local *most, *least;

		find_most_and_least_loaded_cpu(&most, &least);

		/* Something changed, try again */
		if (most == CORE_LOCAL) {
			ipl_lower(ipl);
			continue;
		}

		spinlock(&most->sched_data.thread_queues_lock);

		/* Don't bother. */
		if (most->sched_data.load < STEAL_THRESHOLD) {
			spinrelease(&most->sched_data.thread_queues_lock);
			ipl_lower(ipl);
			continue;
		}

		/* Find a thread to steal. Anything is fine */
		struct thread *td = sched_select_thread(NULL, most, true);

		if (!td) {
			spinrelease(&most->sched_data.thread_queues_lock);
			ipl_lower(ipl);
			continue;
		}

		spinrelease(&most->sched_data.thread_queues_lock);

		spinlock(&td->lock);

		sched_try_preempt(CORE_LOCAL, td);

		spinrelease(&td->lock);

		ipl_lower(ipl);
	}
}

/*
 * Try to balance load between the CPUs.
 * This is called once per second by cpu0
 */
static void balance_load(void *, void *)
{
	struct cpu_local *most, *least;
	int64_t load_diff = 0;
	size_t cnt = LOAD_BALANCE_LIMIT;

	while (cnt--) {
		find_most_and_least_loaded_cpu(&most, &least);

		if (!most || !least)
			return;

		load_diff = most->sched_data.load - least->sched_data.load;

		/* We're done here */
		if (load_diff < 1) {
			break;
		}

		spinlock(&most->sched_data.thread_queues_lock);

		/* Pick a thread on the most loaded CPU */
		struct thread *td = sched_select_thread(NULL, most, true);

		/* !! This should not happen, but still check for it */
		if (!td) {
			spinrelease(&most->sched_data.thread_queues_lock);
			return;
		}

		spinrelease(&most->sched_data.thread_queues_lock);

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

void sched_reschedule()
{
	struct thread *td = CORE_LOCAL->current_thread;

	spinlock(&td->lock);
	spinlock(&CORE_LOCAL->sched_data.thread_queues_lock);

	/*
	 * Advance the insert index every tick, while keeping a separation of 1 with the runidx.
	 * This ensures fairness.
	 */
	if (CORE_LOCAL->sched_data.calendar_queue_runidx ==
		CORE_LOCAL->sched_data.calendar_queue_insidx) {
		CORE_LOCAL->sched_data.calendar_queue_insidx =
			(CORE_LOCAL->sched_data.calendar_queue_insidx + 1) % RUNQUEUES_N;
	}

	if (td->priority_class == PRIO_LOW_BATCH) {
		/* Charge one tick of runtime to this thread */
		td->runtime += TICKS_TO_MS(1);
		td->ticks += 1;

		sched_update_interactivity(td);

		sched_recompute_priority(CORE_LOCAL, td);
	}

	/* Pick a new thread to run */
	struct thread *newtd = sched_select_thread(td, CORE_LOCAL, false);

	if (newtd) {
		CORE_LOCAL->next_thread = newtd;
	}

	spinrelease(&CORE_LOCAL->sched_data.thread_queues_lock);
	spinrelease(&td->lock);
}

struct process kprocess = { 0 };

struct thread *make_kernel_thread(void (*fn)())
{
	struct thread *t = kmem_zalloc(sizeof(struct thread));

	memset(t, 0, sizeof(struct thread));

	t->kernel_stack_base = (uintptr_t)pmm_alloc(2, 1) + HIGH_VMA;

	arch_context_init(&t->ctx, t->kernel_stack_base + 8192, (uintptr_t)fn);
	t->process = &kprocess;
	t->priority_class = PRIO_LOW_BATCH;
	t->priority = PRIO_DEFAULT;

	return t;
}

void sched_init()
{
	memcpy(kprocess.name, "kernel", sizeof("kernel"));
	kprocess.as = &kernel_mappings;
	TAILQ_INIT(&kprocess.threads);
}

void sched_cpu_init()
{
	TAILQ_INIT(&CORE_LOCAL->dpc_queue);
	pairing_heap_init(&CORE_LOCAL->timers, timer_compare);

	dpc_init(&CORE_LOCAL->timer_dpc, timer_handle_expiry);

	CORE_LOCAL->sched_data.calendar_queue_insidx = 0;
	CORE_LOCAL->sched_data.calendar_queue_runidx = 0;

	CORE_LOCAL->sched_data.calendar_queue.status = 0;
	CORE_LOCAL->sched_data.realtime_runq.status = 0;
	CORE_LOCAL->sched_data.thread_queues_lock.lock = 0;

	TAILQ_INIT(&CORE_LOCAL->sched_data.idle_queue);

	for (int i = 0; i < RUNQUEUES_N; i++) {
		TAILQ_INIT(&CORE_LOCAL->sched_data.realtime_runq.queues[i]);
	}

	for (int i = 0; i < RUNQUEUES_N; i++) {
		TAILQ_INIT(&CORE_LOCAL->sched_data.calendar_queue.queues[i]);
	}

	CORE_LOCAL->idle_thread = *make_kernel_thread(idle_thread);

	memcpy(CORE_LOCAL->idle_thread.name, "idle", 6);

	CORE_LOCAL->idle_thread.priority = PRIO_IDLE;
	CORE_LOCAL->idle_thread.priority_class = PRIO_IDLE;
	CORE_LOCAL->current_thread = &CORE_LOCAL->idle_thread;
	CORE_LOCAL->next_thread = NULL;

	if (CORE_LOCAL->core_id == 0) {
		dpc_init(&CORE_LOCAL->balance_dpc, balance_load);
	}
}

void sched_ready(struct thread *td)
{
	ipl_t ipl = spinlock_acquire(&td->lock);
	struct cpu_local *cpu = pick_cpu(td);
	sched_try_preempt(cpu, td);
	spinlock_release(&td->lock, ipl);
}

void sched_wait()
{
	struct thread *td = CORE_LOCAL->current_thread;
	ipl_t ipl = spinlock_acquire(&td->lock);

	td->state = WAITING;

	td->sleep_start = TICKS_TO_MS(atomic_load(&CORE_LOCAL->ticks));

	spinlock_release(&td->lock, ipl);
	sched_yield();
}

void sched_wake(struct thread *td)
{
	ipl_t ipl = ipldispatch();
	spinlock(&td->lock);

	uint64_t ticks_end = atomic_load(&CORE_LOCAL->ticks);

	td->sleeptime = TICKS_TO_MS(ticks_end) - td->sleep_start;

	/* The thread is done sleeping, update the scheduler on whether it's interactive */
	sched_update_interactivity(td);
	sched_recompute_priority(CORE_LOCAL, td);

	spinrelease(&td->lock);

	sched_ready(td);

	ipl_lower(ipl);
}

void sched_yield_locked(struct thread *td)
{
	struct thread *next;

	spinlock(&CORE_LOCAL->sched_data.thread_queues_lock);

	/* Select a new thread to run */
	next = sched_select_thread(td, CORE_LOCAL, false);

	spinrelease(&CORE_LOCAL->sched_data.thread_queues_lock);

	/* Nothing to run, go idle */
	if (!next) {
		next = &CORE_LOCAL->idle_thread;
	}

	/* Switch into the thread */
	sched_switch(td, next);

	/* Thread lock released by sched_switch */
}

void sched_yield()
{
	struct thread *td = CORE_LOCAL->current_thread;
	ipl_t ipl = spinlock_acquire(&td->lock);

	sched_yield_locked(td);

	spinlock_release(&td->lock, ipl);
}

static void yield_on_cpu(struct cpu_local *cpu)
{
	if (cpu == CORE_LOCAL) {
		sched_yield_locked(cpu->current_thread);
		return;
	}

	spinlock(&cpu->sched_data.thread_queues_lock);

	cpu->preemption_reason = PREEMPT_MIGRATION;

	spinrelease(&cpu->sched_data.thread_queues_lock);

	set_softint_pending(cpu, IPL_DISPATCH);
	arch_send_ipi(cpu, IPI_DPC);
}

void sched_pin(struct thread *td, struct cpu_local *cpu)
{
	ipl_t ipl = spinlock_acquire(&td->lock);
	struct cpu_local *prev_cpu = td->last_cpu;
	enum thread_state tdstate = td->state;

	td->pinned = true;
	td->last_cpu = cpu;

	/* Was called before the thread even got the chance to run */
	if (prev_cpu == NULL) {
		spinlock_release(&td->lock, ipl);
		return;
	}

	/*
	 * We changed CPUs, insert the thread into the new CPU's lists
	 * and yield if currently running.
	 */
	if (prev_cpu != cpu) {
		sched_try_preempt(cpu, td);

		/* Thread is currently running, trigger a reschedule. */
		if (tdstate == RUNNING) {
			yield_on_cpu(prev_cpu);
		} else {
			/* Dequeue the thread from its previous CPU's queues */
			sched_remove_from_queue(prev_cpu, td);
		}
	}

	spinlock_release(&td->lock, ipl);
}

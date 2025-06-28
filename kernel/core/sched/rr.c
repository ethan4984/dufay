#include "arch/amd64/port.h"
#include "core/ipl.h"
#include "core/sched.h"
#include "fayt/lock.h"
#include <mm/physical.h>
#include <arch/port.h>
#include <mm/virtual.h>
#include <mm/address.h>
#include <core/debug.h>
#include <core/sched/rr.h>
#include <core/cpu.h>
#include <sys/queue.h>
#include <core/timer.h>
#include <core/lock.h>
#include <core/thread.h>
#include <mm/slab.h>
#include <fayt/string.h>

static size_t last_cpu = 0;

static struct cpu_local *pick_cpu()
{
	size_t cpu_id = last_cpu++;

	if (last_cpu >= logical_processor_cnt) {
		last_cpu = 0;
	}

	return &logical_processor_locales[cpu_id];
}

void sched_switch(struct thread *cur, struct thread *new)
{
	if (cur != &CORE_LOCAL->idle_thread && cur->state == RUNNING) {
		ipl_t ipl = spinlock_acquire(&CORE_LOCAL->sched_data.lock);
		cur->state = READY;
		TAILQ_INSERT_TAIL(&CORE_LOCAL->sched_data.runq, cur, runqueue_hook);
		spinlock_release(&CORE_LOCAL->sched_data.lock, ipl);
	}

	CORE_LOCAL->next_thread = NULL;
	CORE_LOCAL->current_thread = new;
	new->state = RUNNING;

	thread_switch(cur, new);
}

void sched_reschedule()
{
	struct thread *curthread = CORE_LOCAL->current_thread;

	spinlock(&curthread->lock);

	spinlock(&CORE_LOCAL->sched_data.lock);

	struct thread *newtd = TAILQ_FIRST(&CORE_LOCAL->sched_data.runq);

	if (newtd) {
		CORE_LOCAL->next_thread = newtd;
		TAILQ_REMOVE(&CORE_LOCAL->sched_data.runq, newtd, runqueue_hook);
	}

	spinrelease(&CORE_LOCAL->sched_data.lock);
	spinrelease(&curthread->lock);
}

void sched_yield()
{
	struct thread *next;
	struct thread *td = CORE_LOCAL->current_thread;

	spinlock(&td->lock);
	spinlock(&CORE_LOCAL->sched_data.lock);

	/* Select a new thread to run */
	next = TAILQ_FIRST(&CORE_LOCAL->sched_data.runq);

	/* Nothing to run, go idle */
	if (!next) {
		next = &CORE_LOCAL->idle_thread;
	} else {
		TAILQ_REMOVE(&CORE_LOCAL->sched_data.runq, next, runqueue_hook);
	}

	spinrelease(&CORE_LOCAL->sched_data.lock);

	/* Switch into the thread */
	sched_switch(td, next);

	/* Thread lock released by sched_switch */
}

void sched_wait()
{
	struct thread *td = CORE_LOCAL->current_thread;
	ipl_t ipl = spinlock_acquire(&td->lock);

	td->state = WAITING;

	spinlock_release(&td->lock, ipl);
	sched_yield();
}

void sched_wake(struct thread *td)
{
	sched_ready(td);
}

static void sched_enqueue_locked(struct thread *thread)
{
	struct cpu_local *cpu = pick_cpu();

	ipl_t ipl = spinlock_acquire(&cpu->sched_data.lock);

	thread->last_cpu = cpu;
	thread->state = READY;

	if (cpu->current_thread == &cpu->idle_thread && !cpu->next_thread) {
		/* Cause a preemption */
		cpu->next_thread = thread;
		cpu->preemption_reason = PREEMPT_HIGHER_PRIORITY;

		set_softint_pending(cpu, IPL_DISPATCH);

		if (cpu != CORE_LOCAL) {
			arch_send_ipi(cpu, IPI_DPC);
		}

		spinlock_release(&cpu->sched_data.lock, ipl);
		return;
	}

	TAILQ_INSERT_TAIL(&cpu->sched_data.runq, thread, runqueue_hook);

	spinlock_release(&cpu->sched_data.lock, ipl);
}

void sched_ready(struct thread *thread)
{
	ipl_t ipl = spinlock_acquire(&thread->lock);
	sched_enqueue_locked(thread);
	spinlock_release(&thread->lock, ipl);
}

struct process kprocess;

struct thread *make_kernel_thread(void (*fn)())
{
	struct thread *t = alloc(sizeof(struct thread));

	memset(t, 0, sizeof(struct thread));

	t->kernel_stack_base = (uintptr_t)pmm_alloc(2, 1) + HIGH_VMA;

	arch_context_init(&t->ctx, t->kernel_stack_base + 8192, (uintptr_t)fn);
	t->process = &kprocess;

	return t;
}

static void idle()
{
	for (;;) {
		asm("hlt");
	}
}

void sched_init()
{
	memcpy(kprocess.name, "kernel", sizeof("kernel"));
	kprocess.as = &kernel_mappings;
	TAILQ_INIT(&kprocess.threads);
}

void sched_cpu_init()
{
	TAILQ_INIT(&CORE_LOCAL->sched_data.runq);
	TAILQ_INIT(&CORE_LOCAL->dpc_queue);

	dpc_init(&CORE_LOCAL->timer_dpc, timer_handle_expiry);
	pairing_heap_init(&CORE_LOCAL->timers, timer_compare);

	CORE_LOCAL->idle_thread = *make_kernel_thread(idle);
	CORE_LOCAL->idle_thread.state = RUNNING;
	memcpy(CORE_LOCAL->idle_thread.name, "idle", sizeof("idle"));
	CORE_LOCAL->current_thread = &CORE_LOCAL->idle_thread;
}

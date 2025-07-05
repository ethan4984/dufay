#include <core/rcu.h>
#include <core/dpc.h>
#include <core/clock.h>
#include <core/cpu.h>
#include <arch/port.h>
#include <aria/base.h>
#include <core/timer.h>
#include <core/debug.h>
#include <core/lock.h>

/* Called at a frequency of HZ */
void hardclock()
{
	struct cpu_local *cpu = CORE_LOCAL;

	print("hardclock() on cpu %d\n", cpu->core_id);

	bool timer_expired = false;
	nanoseconds_t nanos;
	struct pairing_heap_node *top_timer_node = NULL;

	/* Increase the ticks on this CPU */
	atomic_fetch_add(&cpu->ticks, 1);

	/* Calculate that amount in nanoseconds */
	nanos = cpu->ticks * (NANOSECONDS_PER_SECOND / HZ);

	spinlock_irqsave(&cpu->timers_lock);

	top_timer_node = pairing_heap_top(&cpu->timers);

	if (top_timer_node &&
		CONTAINER_OF(top_timer_node, struct ktimer, heap_node)->deadline <=
			nanos) {
		timer_expired = true;
	}

	spinrelease_irqsave(&cpu->timers_lock);

	/* A timer has expired, enqueue the timer DPC */
	if (timer_expired) {
		dpc_enqueue(&CORE_LOCAL->timer_dpc, NULL, NULL);
	}

	rcu_check();

#if defined(CONFIG_SCHED_ULE)
	/* Balance work on cpu0 */
	if (CORE_LOCAL->core_id == 0 && (cpu->ticks % HZ) == 0) {
		dpc_enqueue(&CORE_LOCAL->balance_dpc, NULL, NULL);
	}
#endif

	/*
	 * Ensure the idle thread doesn't take a quantum end.
	 * This would mess up scenarios where quantum_end is set before any threads are enqueued (at boot),
	 * which would result in one of those threads never being scheduled.
	 */
	if (CORE_LOCAL->current_thread != &CORE_LOCAL->idle_thread) {
		/* Preempt the current thread */
		CORE_LOCAL->preemption_reason = PREEMPT_QUANTUM_END;
	}

	set_softint_pending(CORE_LOCAL, IPL_DISPATCH);
}

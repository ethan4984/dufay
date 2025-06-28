#include "arch/amd64/port.h"
#include "core/sched.h"
#include <core/debug.h>
#include <core/ipl.h>
#include <arch/port.h>
#include <core/cpu.h>
#include <stdatomic.h>

ipl_t ipl_raise(ipl_t ipl)
{
	ipl_t old_ipl = CORE_LOCAL->ipl;

	ASSERT(ipl >= old_ipl);

	CORE_LOCAL->ipl = ipl;

	/* Hardware interrupts disabled */
	if (ipl > IPL_DISPATCH) {
		arch_disable_interrupts();
	}

	return old_ipl;
}

void ipl_lower(ipl_t ipl)
{
	ipl_t old_ipl = CORE_LOCAL->ipl;

	ASSERT(ipl <= old_ipl);

	CORE_LOCAL->ipl = ipl;

	/* Re-enable hardware interrupts */
	if (ipl <= IPL_DISPATCH) {
		arch_enable_interrupts();
	}

	/* Dispatch software interrupts */
	if (ipl < IPL_DISPATCH && is_softint_pending(CORE_LOCAL, ipl)) {
		dispatch_software_interrupts(ipl);
	}
}

ipl_t ipl_get()
{
	return CORE_LOCAL->ipl;
}

bool is_softint_pending(struct cpu_local *cpu, ipl_t ipl)
{
	return (atomic_load(&cpu->pending_softints) >> ipl) != 0;
}

void clear_softint_pending(struct cpu_local *cpu, ipl_t ipl)
{
	atomic_fetch_and(&cpu->pending_softints, ~(1 << ipl));
}

void set_softint_pending(struct cpu_local *cpu, ipl_t ipl)
{
	atomic_fetch_or(&cpu->pending_softints, (1 << ipl));
}

static void dispatch_dpc()
{
	bool int_state = arch_interrupt_state();

	ipl_t oldipl = CORE_LOCAL->ipl;

	enum preemption_reason preempt_reason = CORE_LOCAL->preemption_reason;

	CORE_LOCAL->ipl = IPL_DISPATCH;

	arch_enable_interrupts();

	dispatch_dpc_queue(CORE_LOCAL);

	if (preempt_reason == PREEMPT_QUANTUM_END ||
		preempt_reason == PREEMPT_MIGRATION) {
		sched_reschedule();
	}

	CORE_LOCAL->preemption_reason = PREEMPT_NONE;

	if (CORE_LOCAL->next_thread) {
		sched_switch(CORE_LOCAL->current_thread, CORE_LOCAL->next_thread);
	}

	if (!int_state) {
		arch_disable_interrupts();
	}

	CORE_LOCAL->ipl = oldipl;
}

void dispatch_software_interrupts(ipl_t ipl)
{
	if (is_softint_pending(CORE_LOCAL, ipl)) {
		clear_softint_pending(CORE_LOCAL, ipl);
		dispatch_dpc();
	}
}

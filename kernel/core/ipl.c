#include <core/debug.h>
#include <core/ipl.h>
#include <arch/port.h>
#include <core/scheduler/processor.h>
#include <stdatomic.h>

ipl_t ipl_raise(ipl_t ipl)
{
	ipl_t old_ipl = CORE_LOCAL->ipl;

	ASSERT(ipl > old_ipl);

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
	if (is_softint_pending(ipl)) {
		dispatch_software_interrupts(ipl);
	}
}

ipl_t ipl_get()
{
	return CORE_LOCAL->ipl;
}

bool is_softint_pending(ipl_t ipl)
{
	return (atomic_load(&CORE_LOCAL->pending_softints) >> ipl) != 0;
}

void clear_softint_pending(ipl_t ipl)
{
	atomic_fetch_and(&CORE_LOCAL->pending_softints, ~(1 << ipl));
}

void set_softint_pending(ipl_t ipl)
{
	atomic_fetch_or(&CORE_LOCAL->pending_softints, (1 << ipl));
}

static void dispatch_dpc()
{
	print("DPC CALLED!\n");
}

void dispatch_software_interrupts(ipl_t ipl)
{
	if (is_softint_pending(ipl) && ipl < IPL_DISPATCH) {
		clear_softint_pending(ipl);
		dispatch_dpc();
	}
}

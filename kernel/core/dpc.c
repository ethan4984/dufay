#include <core/ipl.h>
#include <core/dpc.h>
#include <core/cpu.h>
#include <core/lock.h>
#include <aria/debug.h>
#include <sys/queue.h>

void dpc_init(struct dpc *dpc, dpc_routine_t routine)
{
	dpc->routine = routine;
}

void dpc_enqueue(struct dpc *dpc, void *arg1, void *arg2)
{
	spinlock_irqsave(&CORE_LOCAL->dpc_queue_lock);

	dpc->arg1 = arg1;
	dpc->arg2 = arg2;

	if (!dpc->cpu) {
		dpc->cpu = CORE_LOCAL;

		/* Insert the DPC on this CPU's dpc queue */
		TAILQ_INSERT_TAIL(&CORE_LOCAL->dpc_queue, dpc, queue_hook);

		/* Set a DPC pending on this CPU */
		set_softint_pending(CORE_LOCAL, IPL_DISPATCH);
	}

	spinrelease_irqsave(&CORE_LOCAL->dpc_queue_lock);
}

void dispatch_dpc_queue(struct cpu_local *cpu)
{
	/* Go through all DPCs in the queue and call them */
	while (true) {
		spinlock_irqsave(&cpu->dpc_queue_lock);

		struct dpc *dpc = TAILQ_FIRST(&cpu->dpc_queue);

		if (dpc) {
			TAILQ_REMOVE(&cpu->dpc_queue, dpc, queue_hook);
			dpc->cpu = NULL;
		} else {
			spinrelease_irqsave(&cpu->dpc_queue_lock);
			break;
		}

		/*
		 * An interrupt which would enqueue this DPC could occur between loading the arguments and calling the routine,
		 * which is why we save them here to ensure that we get the real ones.
		 */
		void *arg1 = dpc->arg1;
		void *arg2 = dpc->arg2;

		spinrelease_irqsave(&cpu->dpc_queue_lock);

		/* DPCs are ran at IPL_DISPATCH */
		ASSERT(CORE_LOCAL->ipl == IPL_DISPATCH);
		ASSERT(dpc->routine != NULL);

		dpc->routine(arg1, arg2);
	}
}

#ifndef CORE_DPC_H_
#define CORE_DPC_H_
#include <sys/queue.h>

typedef void (*dpc_routine_t)(void *arg1, void *arg2);

struct dpc {
	TAILQ_ENTRY(dpc) queue_hook; /* Entry into per-cpu DPC queue */
	dpc_routine_t routine; /* Routine to be called */
	void *arg1; /* First argument passed to the routine */
	void *arg2; /* Second argument passed to the routine */
	struct cpu_local *cpu; /* CPU this DPC is enqueued on  */
};

void dpc_init(struct dpc *dpc, dpc_routine_t routine);

/*
 * Enqueues a deferred procedure call (DPC).
 * This is a function that will be called when IPL is lower than IPL_DISPATCH.
 */
void dpc_enqueue(struct dpc *dpc, void *arg1, void *arg2);

/*
 * Dispatch the DPC queue on `cpu`
 */
struct cpu_local;
void dispatch_dpc_queue(struct cpu_local *cpu);

#endif

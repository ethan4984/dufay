#ifndef CORE_CPU_H_
#define CORE_CPU_H_

#include <core/sched.h>
#include <mm/portal.h>

#include <aria/circular_queue.h>

#include <stddef.h>
#include <stdint.h>

#include <aria/pairing_heap.h>
#include <arch/port.h>
#include <core/ipl.h>
#include <core/dpc.h>

enum preemption_reason : uint8_t {
	PREEMPT_NONE, /* Dummy value to avoid quantum end to be set when = 0 */
	PREEMPT_QUANTUM_END, /* A thread's quantum has expired */
	PREEMPT_HIGHER_PRIORITY, /* An higher priority thread has been readied */
	PREEMPT_MIGRATION, /* The thread is being migrated */
};

struct cpu_local {
	struct arch_cpu_cb
		arch_cb; /* Architecture-dependent CPU control-block. This must ALWAYS be the first member of the struct */

	int core_id; /* ID of this CPU */

	_Atomic(uint8_t)
		pending_softints; /* Bitmask of pending software interrupts on this CPU */

	_Atomic(uint64_t) ticks; /* Ticks elapsed on this CPU */

	ipl_t ipl; /* Current interrupt priority level of this CPU */

	struct thread
		*current_thread; /* Thread that's currently running on this CPU */

	uint8_t
		current_thread_status; /* 1 bit for thread interactivity and 7 for priority */

	struct thread *next_thread; /* Thread that will run next on this CPU */

	struct spinlock timers_lock;
	struct pairing_heap timers; /* Timers enqueued on this CPU */

	struct dpc timer_dpc; /* Timer expiry DPC */
	struct dpc balance_dpc; /* Load balancing DPC */

	struct sched_percpu sched_data; /* Per-CPU scheduler data */

	struct thread idle_thread; /* Per-CPU idle thread, executed when there's
                                nothing else to do */

	enum preemption_reason preemption_reason; /* Preemption reason */

	TAILQ_HEAD(, dpc) dpc_queue; /* Queue of DPCs on this CPU */
	struct spinlock dpc_queue_lock;
};

extern size_t logical_processor_cnt;
extern size_t bootable_processor_cnt;

extern struct cpu_local *logical_processor_locales;

#endif

#ifndef CORE_RCU_H_
#define CORE_RCU_H_
#include <stddef.h>
#include <sys/queue.h>
#include <core/lock.h>
#include <stdatomic.h>
#include <stdint.h>

typedef void (*rcu_callback_t)(void *);

/* Global RCU state */
struct rcu_state {
	struct spinlock lock;
	size_t cur_generation; /* Current generation number */
	size_t max_generation; /* Highest requested generation */

	_Atomic uint64_t bitmask; /* FIXME: Find a way to handle more than 64 CPUs? */
};

struct rcu_head {
	TAILQ_ENTRY(rcu_head) queue_hook;

	rcu_callback_t callback;
	void *arg;
};

struct rcu_cpu {
	size_t qs_counter; /* Quiescent states counter */
	size_t last_qs_counter; /* Value of qs_counter at beginning of grace period */
	size_t generation; /* Generation the CPU is on */
	int num;

	TAILQ_HEAD(, rcu_head) next, current, intr;
};

extern struct rcu_state rcu_global_state;

#define rcu_read_lock() (ipl_raise(IPL_DISPATCH))
#define rcu_read_unlock(ipl) (ipl_lower(ipl))
#define rcu_assign_pointer(p, v) __atomic_store(&(p), &(v), __ATOMIC_RELEASE)

/* Exposed API */
void call_rcu(struct rcu_head *head, rcu_callback_t func, void *arg);
void synchronize_rcu();

/* Private functions */
void rcu_enter_quiescent();
void rcu_init();
void rcu_check();

#endif

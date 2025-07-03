#include <core/rcu.h>
#include <core/dpc.h>
#include <core/cpu.h>
#include <core/events.h>

#include <core/debug.h>

#define RCU_IS_CPU_QUIESCED(CPUNUM) \
	((atomic_load(&rcu_global_state.bitmask) & (1 << CPUNUM)) == 0)

#define RCU_SET_CPU_QUIESCED(CPUNUM) \
	(atomic_fetch_and(&rcu_global_state.bitmask, ~(1 << CPUNUM)))

TAILQ_HEAD(rcu_head_list, rcu_head);

struct rcu_state rcu_global_state = {
	.lock.lock = 0,
	.cur_generation = 0,
	.max_generation = 0,
	.bitmask = 0,
};

static void new_generation()
{
	rcu_global_state.max_generation = ++rcu_global_state.cur_generation;
	rcu_global_state.bitmask = (1 << logical_processor_cnt) - 1;
}

static void update_quiescent_state(struct rcu_cpu *cpu)
{
	if (RCU_IS_CPU_QUIESCED(cpu->num)) {
		return;
	}

	if (cpu->last_qs_counter == 0) {
		cpu->last_qs_counter = cpu->qs_counter;
		return;
	}

	/* No quiescent states */
	if (cpu->last_qs_counter == cpu->qs_counter) {
		return;
	}

	spinlock(&rcu_global_state.lock);

	if (RCU_IS_CPU_QUIESCED(cpu->num)) {
		spinrelease(&rcu_global_state.lock);
		return;
	}

	RCU_SET_CPU_QUIESCED(cpu->num);

	cpu->last_qs_counter = 0;

	/*
	 * All CPUs now went through a quiescent state, increase current generation
	 */
	if (rcu_global_state.bitmask == 0) {
		rcu_global_state.cur_generation++;

		/* We must go through another generation */
		if (rcu_global_state.cur_generation <=
			rcu_global_state.max_generation) {
			new_generation();
		}
	}

	spinrelease(&rcu_global_state.lock);
}

static void do_callbacks(struct rcu_head_list *list)
{
	struct rcu_head *elem;

	while (!TAILQ_EMPTY(list)) {
		elem = TAILQ_FIRST(list);

		TAILQ_REMOVE(list, elem, queue_hook);

		elem->callback(elem->arg);
	}
}

static void rcu_dpc(void *, void *)
{
	struct rcu_cpu *cpu = &CORE_LOCAL->rcu;

	struct rcu_head_list list;

	TAILQ_INIT(&list);

	/*
	 * The generation has completed, we can call whatever callbacks that were
	 * enqueued
	 */
	if (!TAILQ_EMPTY(&cpu->current) &&
		cpu->generation < rcu_global_state.cur_generation) {
		TAILQ_CONCAT(&list, &cpu->current, queue_hook);
		TAILQ_INIT(&cpu->current);
	}

	if (!TAILQ_EMPTY(&cpu->next) && TAILQ_EMPTY(&cpu->current)) {
		/* Move next to current */
		TAILQ_CONCAT(&cpu->current, &cpu->next, queue_hook);

		TAILQ_INIT(&cpu->next);

		spinlock(&rcu_global_state.lock);

		cpu->generation = rcu_global_state.cur_generation + 1;

		/*
		 * If other CPUs still need to go through quiescent state (generation is in progress), schedule the next generation.
		 */
		if (rcu_global_state.bitmask) {
			rcu_global_state.max_generation = cpu->generation + 1;
		} else {
			/* No generation is in progress, start a new one */
			new_generation();
		}

		spinrelease(&rcu_global_state.lock);
	}

	update_quiescent_state(cpu);

	do_callbacks(&list);
}

static inline bool rcu_pending(struct rcu_cpu *cpu)
{
	if (!TAILQ_EMPTY(&cpu->current) &&
		cpu->generation < rcu_global_state.cur_generation) {
		return true;
	}

	if (TAILQ_EMPTY(&cpu->current) && !TAILQ_EMPTY(&cpu->next)) {
		return true;
	}

	if (!RCU_IS_CPU_QUIESCED(cpu->num)) {
		return true;
	}

	return false;
}

void rcu_check()
{
	if (rcu_pending(&CORE_LOCAL->rcu)) {
		if (CORE_LOCAL->current_thread == &CORE_LOCAL->idle_thread) {
			rcu_enter_quiescent();
		}

		dpc_enqueue(&CORE_LOCAL->rcu_dpc, NULL, NULL);
	}
}

void rcu_enter_quiescent()
{
	CORE_LOCAL->rcu.qs_counter++;
}

void rcu_init()
{
	TAILQ_INIT(&CORE_LOCAL->rcu.next);
	TAILQ_INIT(&CORE_LOCAL->rcu.current);

	CORE_LOCAL->rcu.num = CORE_LOCAL->core_id;

	dpc_init(&CORE_LOCAL->rcu_dpc, rcu_dpc);
}

void call_rcu(struct rcu_head *head, rcu_callback_t callback, void *arg)
{
	ipl_t ipl = ipldispatch();

	head->callback = callback;
	head->arg = arg;

	TAILQ_INSERT_TAIL(&CORE_LOCAL->rcu.next, head, queue_hook);

	ipl_lower(ipl);
}

static void rcu_done(void *event)
{
	event_signal(event);
}

void synchronize_rcu()
{
	struct rcu_head rcu;
	struct event rcu_done_event;

	event_init(&rcu_done_event, "synchronize_rcu", false);

	call_rcu(&rcu, rcu_done, &rcu_done_event);

	wait_one(&rcu_done_event.hdr, -1);
}

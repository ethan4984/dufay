#include <mm/virtual.h>
#include <arch/amd64/smp.h>

#include <core/notification.h>
#include <core/syscall.h>
#include <mm/physical.h>
#include <core/lock.h>
#include <core/debug.h>
#include <core/capability.h>

#include <aria/lock.h>
#include <aria/base.h>
#include <aria/compiler.h>
#include <aria/debug.h>

static inline int notification_is_valid(int not)
{
	if (not < 0 || not > NOTIFICATION_MAX)
		RETURN_ERROR;
	else
		return 0;
}

static int notification_push(struct notification_queue *queue,
							 struct notification *notification)
{
	if (queue == NULL || notification == NULL)
		RETURN_ERROR;

	int i = 0;
	for (; i < NOTIFICATION_PENDING_CAPACITY; i++) {
		if (queue->queue[NOTIFICATION_INDEX(notification->notnum)][i] == NULL) {
			goto finish;
		}
	}
	return -1;
finish:
	queue->queue[NOTIFICATION_INDEX(notification->notnum)][i] = notification;
	queue->pending |= NOTIFICATION_MASK(notification->notnum);

	return 0;
}

static int notification_pop(struct notification_queue *queue,
							struct notification **notification, int not)
{
	if (queue == NULL || notification == NULL)
		RETURN_ERROR;

	int i = 0;
	for (; i < NOTIFICATION_PENDING_CAPACITY; i++) {
		if (queue->queue[NOTIFICATION_INDEX(not)][i] &&
			queue->queue[NOTIFICATION_INDEX(not)][i]->serviceable)
			goto finish;
	}
	*notification = NULL;
	return 0;
finish:
	*notification = queue->queue[NOTIFICATION_INDEX(not)][i];
	queue->queue[NOTIFICATION_INDEX(not)][i] = NULL;

	return 0;
}

static inline int notification_check_perms(struct thread *, struct thread *,
										   int)
{
	return 0;
}

static int bridge_to_destination(struct comm_bridge *bridge,
								 struct thread **dest)
{
	if (bridge == NULL || dest == NULL)
		RETURN_ERROR;
	struct thread *thread = CORE_LOCAL->current_thread;

	struct capability_binding *binding =
		capability_lookup(thread->capability_table, bridge->destination);
	if (binding == NULL)
		RETURN_ERROR;

	struct thread_capability *thread_capability = binding->obj;
	if (thread_capability == NULL)
		RETURN_ERROR;
	if ((binding->access & CAPABILITY_ACCESS_WRITE) == 0)
		RETURN_ERROR;

	*dest = NULL;
	int ret = search_thread(thread_capability, dest);
	if (ret == -1 || *dest == NULL)
		RETURN_ERROR;

	return 0;
}

static int notification_destroy(struct notification *notification)
{
	if (notification == NULL)
		RETURN_ERROR;
	struct notification_queue *queue = notification->queue;
	if (queue == NULL)
		RETURN_ERROR;

	for (int i = 0; i < NOTIFICATION_PENDING_CAPACITY; i++) {
		if (queue->queue[NOTIFICATION_INDEX(notification->notnum)][i] ==
			notification) {
			queue->queue[NOTIFICATION_INDEX(notification->notnum)][i] = NULL;
		}
	}

	notification->refcnt--;
	if (notification->refcnt <= 0)
		free(notification);

	return 0;
}

static int notification_context_instantiate(struct thread *thread,
											struct context *context,
											struct notification *notification,
											struct notification_action *action)
{
	if (thread == NULL || context == NULL || notification == NULL ||
		action == NULL)
		RETURN_ERROR;

	struct ustack *ustack;
	int ret = USTACK_CLAIM(thread, ustack);
	if (ret == -1)
		RETURN_ERROR;
	if (ustack == NULL)
		RETURN_ERROR;

	context->stack = ustack;
	context->arch_context.fpu_thread =
		alloc(CORE_LOCAL->arch_cb.fpu_thread_size);
	if (context->arch_context.fpu_thread == NULL)
		RETURN_ERROR;

	context->arch_context.regs.ss = 0x3b;
	context->arch_context.regs.rsp = context->stack->user_stack.sp;
	context->arch_context.regs.rflags = 0x202 & ~(1 << 9);
	context->arch_context.regs.cs = 0x43;
	context->arch_context.regs.rip = (uintptr_t)action->handler;

	context->arch_context.regs.rdi = (uint64_t)notification->info;
	context->arch_context.regs.rsi = 0;
	context->arch_context.regs.rdx = notification->notnum;

	int parameter_length = notification->parameter.page_cnt * PAGE_SIZE;
	if (parameter_length) {
		uintptr_t vaddr = context->arch_context.regs.rsp -= parameter_length;
		pmap_map(thread->address_space->page_table->pmap, vaddr,
				 notification->parameter.paddr,
				 VM_PROT_PRESENT | VM_PROT_WRITE | VM_PROT_USER, 0);
		context->arch_context.regs.rsi = context->arch_context.regs.rsp;
	}

	struct portal_resp resp;
	struct portal_req req = {
		.type = PORTAL_REQ_ANON,
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.length = sizeof(struct portal_req),
		.share = { .identifier = NULL, .type = 0, .create = 0 },
		.morphology = { .addr = context->arch_context.regs.rsp,
						.length =
							context->stack->user_stack.size - parameter_length }
	};

	ret = portal(&req, &resp);
	if (ret == -1 || resp.base != context->arch_context.regs.rsp) {
		print("ERROR: unable to anonymously map notification stack\n");
		RETURN_ERROR;
	}

	return 0;
}

int notification_queue(struct thread *sender, struct thread *target, int not,
					   int weight, int serviceable, uintptr_t vaddr,
					   uint64_t paddr, int page_cnt)
{
	if (target == NULL || notification_is_valid(not) == -1)
		RETURN_ERROR;
	if (sender && notification_check_perms(sender, target, not) == -1)
		RETURN_ERROR;

	struct notification_queue *queue = target->notification.queue;
	struct notification *notification = alloc(sizeof(struct notification));
	if (notification == NULL)
		RETURN_ERROR;

	notification->refcnt = 1;
	notification->notnum = not;
	notification->weight = weight;
	notification->info = alloc(sizeof(struct notification_info));
	if (notification->info == NULL)
		RETURN_ERROR;
	notification->queue = queue;
	notification->source = sender;

	if (page_cnt && paddr) {
		if (vaddr)
			notification->parameter.vaddr = vaddr;
		notification->parameter.paddr = paddr;
		notification->parameter.page_cnt = page_cnt;

		if (vaddr)
			pmap_map(sender->address_space->page_table->pmap, vaddr, paddr,
					 VM_PROT_PRESENT | VM_PROT_WRITE | VM_PROT_USER, 0);
	}

	int ret = notification_push(queue, notification);
	if (ret == -1)
		RETURN_ERROR;

	if (weight & NOTIFY_WEIGHT_INSTANTANEOUS || weight & NOTIFY_WEIGHT_TICK) {
		struct context *context = alloc(sizeof(struct context));
		if (context == NULL)
			RETURN_ERROR;

		context->etrigger = alloc(sizeof(struct etrigger));
		if (context->etrigger == NULL)
			RETURN_ERROR;
		context->etrigger->context = context;
		context->thread = target;
		context->notification = notification;
		context->notification->serviceable = serviceable;

		int ret = CONTEXT_PUSH(target, context);
		if (ret == -1) {
			print("ERROR: failed to push context on stack\n");
			RETURN_ERROR;
		}

		target->context_top = context;

		ret = delivery_queue_push(&CORE_LOCAL->delivery_queue, target);
		if (ret == -1)
			RETURN_ERROR;
	}
	if (weight & NOTIFY_WEIGHT_INSTANTANEOUS)
		yield();

	return 0;
}

SYSCALL_DEFINE1(notification_build, struct comm_bridge *, bridge, {
	struct thread *thread = CORE_LOCAL->current_thread;
	struct thread *destination;

	int ret = bridge_to_destination(bridge, &destination);
	if (ret == -1)
		RETURN_ERROR;

	uintptr_t vaddr = (uintptr_t)bridge->data.base;
	int page_cnt = DIV_ROUNDUP(bridge->data.limit, PAGE_SIZE);

	struct notification_queue *queue = destination->notification.queue;
	if (queue == NULL)
		RETURN_ERROR;

	struct notification *notification = alloc(sizeof(struct notification));
	if (notification == NULL)
		RETURN_ERROR;

	uint64_t paddr = 0;
	if (vaddr && page_cnt) {
		paddr = pmm_alloc(page_cnt, 1);
		if (!paddr)
			RETURN_ERROR;

		pmap_map(thread->address_space->page_table->pmap, vaddr, paddr,
				 VM_PROT_PRESENT | VM_PROT_WRITE | VM_PROT_USER, 0);

		notification->parameter.paddr = paddr;
		notification->parameter.page_cnt = page_cnt;
	}

	notification->refcnt = 1;
	notification->notnum = bridge->not;
	notification->weight = bridge->weight;
	notification->info = alloc(sizeof(struct notification_info));
	if (notification->info == NULL)
		RETURN_ERROR;
	notification->queue = queue;
	notification->serviceable = false;
	notification->source = thread;

	ret = notification_push(queue, notification);
	if (ret == -1)
		return -1;

	bridge->lnkidx = ret;
})

SYSCALL_DEFINE1(notification_broadcast, struct comm_bridge *, bridge, {
	struct thread *thread = CORE_LOCAL->current_thread;
	struct thread *destination;

	int ret = bridge_to_destination(bridge, &destination);
	if (ret == -1)
		RETURN_ERROR;

	if (bridge->lnkidx < 0 || bridge->lnkidx > NOTIFICATION_PENDING_CAPACITY)
		return -1;
	struct notification_queue *queue = destination->notification.queue;
	if (queue == NULL)
		RETURN_ERROR;

	struct notification *notification =
		queue->queue[NOTIFICATION_INDEX(bridge->not)][bridge->lnkidx];
	if (notification == NULL)
		return -1;

	notification->serviceable = true;

	if (bridge->weight & NOTIFY_WEIGHT_INSTANTANEOUS ||
		bridge->weight & NOTIFY_WEIGHT_TICK) {
		int ret = delivery_queue_push(&CORE_LOCAL->delivery_queue, destination);
		if (ret == -1)
			RETURN_ERROR;
	}
	if (bridge->weight & NOTIFY_WEIGHT_INSTANTANEOUS) {
		struct equeue equeue = { 0 };

		struct context *context = thread->context_active;
		if (context == NULL)
			RETURN_ERROR;

		VECTOR_PUSH(notification->etrigger, context->etrigger);

		ret = equeue_add(&equeue, context->etrigger);
		if (ret == -1)
			RETURN_ERROR;

		for (;;) {
			//print("broadcast: blocking on cid=%x\n", thread->comms.proc_id.cid);
			int ret = equeue_block(&equeue, NULL);
			if (ret == -1)
				RETURN_ERROR;
			//print("broadcast: unblocking on cid=%x\n", thread->comms.proc_id.cid);
			if (!context->blocking && notification->done)
				break; // think of a better way to solve this
		}
	}
})

int notification_dispatch(struct thread *thread)
{
	if (thread == NULL)
		RETURN_ERROR;

	struct notification_queue *queue = thread->notification.queue;
	if (unlikely(queue == NULL))
		RETURN_ERROR;
	if (unlikely(queue->active == 0)) {
		return 0;
	}

	spinlock(&queue->lock);

	if (!queue->active || !queue->pending) {
		spinrelease(&queue->lock);
		return 0;
	}

	struct context *top = thread->context_top;

	if (top->notification && top->notification->serviceable &&
		!top->notification->serviced) {
		struct notification_action *action =
			&thread->notification
				 .actions[thread->context_top->notification->notnum - 1];
		int ret = notification_context_instantiate(
			thread, thread->context_top, thread->context_top->notification,
			action);
		if (ret == -1) {
			print("ERROR: failed to initialise context\n");
			spinrelease(&queue->lock);
			RETURN_ERROR;
		}

		top->notification->serviceable = false;
		top->notification->serviced = true;

		for (int i = 0; i < NOTIFICATION_PENDING_CAPACITY; i++) {
			if (queue->queue[NOTIFICATION_INDEX(top->notification->notnum)][i] ==
				top->notification) {
				queue->queue[NOTIFICATION_INDEX(top->notification->notnum)][i] =
					NULL;
			}
		}

		spinrelease(&queue->lock);

		return 0;
	}

	for (int i = 1; i <= NOTIFICATION_MAX; i++) {
		if ((queue->pending & NOTIFICATION_MASK(i)) == 0 ||
			queue->mask & NOTIFICATION_MASK(i))
			continue;

		struct notification_action *action =
			&thread->notification.actions[i - 1];
		struct notification *notification;

		int ret = notification_pop(queue, &notification, i);
		if (ret == -1) {
			print("ERROR: failed to pop notification from stack\n");
			spinrelease(&queue->lock);
			RETURN_ERROR;
		}
		if (notification == NULL || action == NULL) {
			continue;
		}

		struct context *context = alloc(sizeof(struct context));
		if (context == NULL)
			RETURN_ERROR;

		context->etrigger = alloc(sizeof(struct etrigger));
		if (context->etrigger == NULL)
			RETURN_ERROR;
		context->etrigger->context = context;
		context->notification = notification;
		context->thread = thread;

		context->notification->serviceable = false;
		context->notification->serviced = true;

		ret = notification_context_instantiate(thread, context, notification,
											   action);
		if (ret == -1) {
			spinrelease(&queue->lock);
			RETURN_ERROR;
		}

		ret = CONTEXT_PUSH(thread, context);
		if (ret == -1) {
			print("ERROR: failed to push context on stack\n");
			RETURN_ERROR;
		}

		thread->context_top = context;

		spinrelease(&queue->lock);

		return 0;
	}

	spinrelease(&queue->lock);

	return 0;
}

SYSCALL_DEFINE1(notify, struct comm_bridge *, bridge, {
	if (bridge == NULL)
		RETURN_ERROR;

	struct thread *destination;

	int ret = bridge_to_destination(bridge, &destination);
	if (ret == -1)
		RETURN_ERROR;
})

SYSCALL_DEFINE3(notification_action, int, not, struct notification_action *,
				action, struct notification_action *, old, {
					struct thread *thread = CORE_LOCAL->current_thread;

					if (unlikely(thread == NULL))
						RETURN_ERROR;
					if (unlikely(notification_is_valid(not) == -1))
						RETURN_ERROR;

					spinlock(&thread->notification.lock);

					struct notification_action *current_action =
						&thread->notification.actions[not - 1];

					if (old)
						*old = *current_action;
					if (action)
						*current_action = *action;

					spinrelease(&thread->notification.lock);
				})

SYSCALL_DEFINE2(notification_define_stack, void *, sp, size_t, sp_size, {
	struct thread *thread = CORE_LOCAL->current_thread;
	if (unlikely(thread == NULL))
		RETURN_ERROR;

	struct ustack *new_stack = alloc(sizeof(struct ustack));
	if (new_stack == NULL)
		RETURN_ERROR;

	new_stack->user_stack.sp = (uintptr_t)sp;
	new_stack->user_stack.size = sp_size;
	new_stack->kernel_stack.sp =
		(uintptr_t)pmm_alloc(DIV_ROUNDUP(CONTEXT_DEFAULT_STACK_SIZE, PAGE_SIZE),
							 1) +
		CONTEXT_DEFAULT_STACK_SIZE + HIGH_VMA;
	if (!new_stack->kernel_stack.sp)
		RETURN_ERROR;
	new_stack->kernel_stack.size = CONTEXT_DEFAULT_STACK_SIZE;
	new_stack->active = 0;

	int ret = USTACK_PUSH(thread, new_stack);
	if (ret == -1)
		RETURN_ERROR;
})

SYSCALL_DEFINE1(notification_destroy, struct comm_bridge *, bridge, {
	struct thread *destination;

	int ret = bridge_to_destination(bridge, &destination);
	if (ret == -1)
		RETURN_ERROR;

	if (bridge->lnkidx < 0 || bridge->lnkidx > NOTIFICATION_PENDING_CAPACITY)
		return -1;
	struct notification_queue *queue = destination->notification.queue;
	if (queue == NULL)
		RETURN_ERROR;

	struct notification *notification =
		queue->queue[NOTIFICATION_INDEX(bridge->not)][bridge->lnkidx];
	if (notification == NULL)
		return -1;

	ret = notification_destroy(notification);
	if (ret == -1)
		return -1;
})

//	IF THE NOTIFICATION WAS WEIGHTED INSTANTANEOUS OR TICK, IT WILL RETURN DIRECTLY TO THE CALLER
//	THIS BEHAVIOUR IS LOGICAL BECAUSE FOR AN INSTANTEANOUS NOTIFICATION WE WISH FOR CONTINUITY, AND
//	FOR A TICKED NOTIFICATION WE DO NOT WANT TO DISTURB THE SCHEDULING QUEUE. FOR A SCHEDULED
//	NOTIFICATION, IT WILL FIND THE NEXT SCHEDULABLE UCONTEXT.

SYSCALL_DEFINE0(notification_return, {
	struct thread *current_thread = CORE_LOCAL->current_thread;
	if (unlikely(current_thread == NULL))
		RETURN_ERROR;

	struct context *current_context = current_thread->context_active;
	if (unlikely(current_context == NULL))
		RETURN_ERROR;

	spinlock_irqsave(&CORE_LOCAL->sched_lock);

	// REFINE AND STREAMLINE HOW WE INDICIATE WHETHER OR NOT A NOTIFICATION HAS BEEN SERVICED/FINISHED

	struct thread *rthread = current_context->notification->source ?
								 current_context->notification->source :
								 NULL;
	struct context *rcontext = ({
		__label__ finish;
		struct context *rcontext = rthread->context_top;

		for (; rcontext;) {
			if (rcontext->notification && rcontext->notification->serviceable &&
				!rcontext->notification->serviced) {
				if (rcontext->notification->weight ==
					NOTIFY_WEIGHT_INSTANTANEOUS)
					goto finish;
				rcontext = rcontext->last;
				continue;
			}
			if (rcontext->last)
				rcontext = rcontext->last;
			else
				goto finish;
		}
		rcontext = NULL;
finish:
		rcontext;
	});
	if (rthread == NULL || rcontext == NULL)
		RETURN_ERROR;

	if (rcontext->notification)
		rcontext->notification->serviced = true;

	spinrelease_irqsave(&CORE_LOCAL->sched_lock);

	//	print(
	//		"notification_return: current ctx [cid=%x] going to [cid=%x] [rip=%x] [NOT=%c] [INSTANT=%c]\n",
	//		current_thread->comms.proc_id.cid, rthread->comms.proc_id.cid,
	//		rcontext->regs.rip, rcontext->notification ? 'T' : 'F',
	//		current_context->notification ? 'T' : 'F');

	current_context->stack->active = false;
	current_context->notification->done = true;
	int ret = destroy_context(current_thread, current_context);
	if (ret == -1)
		RETURN_ERROR;

	//print(
	//	"notification_return: ESCAPED DESTROY: current ctx [%x] going to [%x] [rip=%x] [NOT=%c] [INSTANT=%c]\n",
	//	current_thread->comms.proc_id.cid, rthread->comms.proc_id.cid,
	//	rcontext->regs.rip, rcontext->notification ? 'T' : 'F',
	//	current_context->notification ? 'T' : 'F');

	if (rcontext->notification) {
		struct notification_action *action =
			&rthread->notification
				 .actions[NOTIFICATION_INDEX(rcontext->notification->notnum)];
		int ret = notification_context_instantiate(
			rthread, rcontext, rcontext->notification, action);
		if (ret == -1)
			RETURN_ERROR;
	}

	ret = delivery_queue_remove(&CORE_LOCAL->delivery_queue, current_thread);
	if (ret == -1)
		RETURN_ERROR;

	rthread->context_active = rcontext;

	if (rthread != current_thread) {
		pmap_activate(rthread->address_space->page_table->pmap);

		set_user_fs(rthread->user_fs_base);
		set_user_gs(rthread->user_gs_base);

		CORE_LOCAL->current_thread = rthread;
	}

	CORE_LOCAL->arch_cb.kernel_stack = rcontext->stack->kernel_stack.sp;
	CORE_LOCAL->arch_cb.fpu_rstor(rcontext->arch_context.fpu_thread);

	CORE_LOCAL->arch_cb.user_stack = rcontext->sysctx.user_stack;
	CORE_LOCAL->arch_cb.error = rcontext->sysctx.user_stack;

	rcontext->blocking = false;

	SWAP_TLS(&rcontext->arch_context.regs);

	__asm__ volatile("mov %0, %%rsp\n\t"
					 "pop %%r15\n\t"
					 "pop %%r14\n\t"
					 "pop %%r13\n\t"
					 "pop %%r12\n\t"
					 "pop %%r11\n\t"
					 "pop %%r10\n\t"
					 "pop %%r9\n\t"
					 "pop %%r8\n\t"
					 "pop %%rsi\n\t"
					 "pop %%rdi\n\t"
					 "pop %%rbp\n\t"
					 "pop %%rdx\n\t"
					 "pop %%rcx\n\t"
					 "pop %%rbx\n\t"
					 "pop %%rax\n\t"
					 "addq $16, %%rsp\n\t"
					 "iretq\n\t" ::"r"(&rcontext->arch_context.regs));
})

SYSCALL_DEFINE1(notification_wait, struct comm_bridge *, bridge, {
	struct thread *thread = CORE_LOCAL->current_thread;
	struct thread *destination;

	int ret = bridge_to_destination(bridge, &destination);
	if (ret == -1)
		RETURN_ERROR;

	if (bridge->lnkidx < 0 || bridge->lnkidx > NOTIFICATION_PENDING_CAPACITY)
		return -1;
	struct notification_queue *queue = destination->notification.queue;
	if (queue == NULL)
		RETURN_ERROR;

	struct notification *notification =
		queue->queue[NOTIFICATION_INDEX(bridge->not)][bridge->lnkidx];
	if (notification == NULL)
		return -1;

	struct context *context = thread->context_active;
	if (context == NULL) {
		print("ERROR: context is null (should not be)");
		return -1;
	}

	context->blocking = true;
	for (; context->blocking;)
		yield();
})

SYSCALL_DEFINE0(notification_unmute, {
	struct thread *current_thread = CORE_LOCAL->current_thread;
	if (current_thread == NULL)
		RETURN_ERROR;

	struct notification_queue *queue = current_thread->notification.queue;
	if (queue == NULL)
		RETURN_ERROR;

	queue->active = 1;
})

SYSCALL_DEFINE0(notification_mute, {
	struct thread *current_thread = CORE_LOCAL->current_thread;
	if (current_thread == NULL)
		RETURN_ERROR;

	struct notification_queue *queue = current_thread->notification.queue;
	if (queue == NULL)
		RETURN_ERROR;

	queue->active = 0;
})

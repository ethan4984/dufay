#include <arch/x86/smp.h>
#include <arch/x86/paging.h>

#include <core/notification.h>
#include <core/syscall.h>
#include <core/scheduler.h>
#include <core/physical.h>
#include <core/debug.h>
#include <core/server.h>

#include <fayt/lock.h>
#include <fayt/string.h>
#include <fayt/compiler.h>
#include <fayt/debug.h>

static inline int notification_is_valid(int not) {
	if(not < 0 || not > NOTIFICATION_MAX) RETURN_ERROR;
	else return 0;
}

static inline int notification_check_perms(struct context*, struct context*, int) {
	return 0;
}

static int bridge_to_destination(struct comm_bridge *bridge, struct context **dest) {
	if(bridge == NULL || dest == NULL) RETURN_ERROR;
	struct context *context = CORE_LOCAL->current_context;

	if(bridge->destination) {
		const char *namespace = context->comms.namespace;
		if(bridge->namespace) namespace = bridge->namespace;

		struct server *server = find_server(namespace, bridge->destination);
		if(server == NULL || server->context == NULL) RETURN_ERROR;
	
		*dest = server->context;
	} else {
		int ret = SEARCH_CONTEXT(bridge->cid, dest);
		if(ret == -1 || *dest == NULL) RETURN_ERROR;
	}

	return 0;
}

static int notification_ucontext_instantiate(struct context *context, struct ucontext *ucontext,
	struct notification *notification, struct notification_action *action) {
	if(context == NULL || ucontext == NULL || notification == NULL || action == NULL) RETURN_ERROR; 

	struct ustack *ustack;
	int ret = USTACK_CLAIM(context, ustack);
	if(ret == -1) RETURN_ERROR;
	if(ustack == NULL) RETURN_ERROR;

	ucontext->stack = ustack;
	ucontext->fpu_context = alloc(CORE_LOCAL->fpu_context_size);

	ucontext->regs.ss = 0x3b;
	ucontext->regs.rsp = ucontext->stack->user_stack.sp;
	ucontext->regs.rflags = 0x202;
	ucontext->regs.cs = 0x43;
	ucontext->regs.rip = (uintptr_t)action->handler;

	ucontext->regs.rdi = (uint64_t)notification->info;
	ucontext->regs.rsi = 0;
	ucontext->regs.rdx = notification->notnum;

	int parameter_length = notification->parameter.page_cnt * PAGE_SIZE;
	if(parameter_length) {
		uintptr_t vaddr = ucontext->regs.rsp -= parameter_length; 
		x86_map_page(context->page_table, vaddr, notification->parameter.paddr, 
			X86_FLAGS_P | X86_FLAGS_US | X86_FLAGS_NX);
		ucontext->regs.rsi = ucontext->regs.rsp;
	}

	struct portal_resp resp;
	struct portal_req req = {
		.type = PORTAL_REQ_ANON,
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.length = sizeof(struct portal_req),
		.share = {
			.identifier = NULL,
			.type = 0,
			.create = 0
		},
		.morphology = {
			.addr = ucontext->regs.rsp,
			.length = ucontext->stack->user_stack.size - parameter_length
		}
	};

	ret = portal(&req, &resp);
	if(ret == -1 || resp.base != ucontext->regs.rsp) {
		print("dufay: unable to anonymously map notification stack\n");
		RETURN_ERROR;
	}

	return 0;
}

int notification_queue(struct context *sender, struct context *target, int not,
	int weight, int ready, uintptr_t vaddr, uint64_t paddr, int page_cnt) {
	if(target == NULL || notification_is_valid(not) == -1) RETURN_ERROR;
	if(sender && notification_check_perms(sender, target, not) == -1) RETURN_ERROR;

	struct notification_queue *queue = target->notification.queue;
	struct notification *notification = alloc(sizeof(struct notification));

	notification->refcnt = 1;
	notification->notnum = not;
	notification->weight = weight;
	notification->info = alloc(sizeof(struct notification_info));
	notification->queue = queue;

	if(page_cnt && paddr) {
		if(vaddr) notification->parameter.vaddr = vaddr;
		notification->parameter.paddr = paddr;
		notification->parameter.page_cnt = page_cnt;

		if(vaddr) x86_map_page(sender->page_table, vaddr, paddr,
			X86_FLAGS_PS | X86_FLAGS_US | X86_FLAGS_NX);
	}

	int ret = NOTIFICATION_PUSH(queue, notification);
	if(ret == -1) RETURN_ERROR;

	if(weight & NOTIFY_WEIGHT_INSTANTANEOUS || weight & NOTIFY_WEIGHT_TICK) {
		struct ucontext *ucontext = alloc(sizeof(struct ucontext));

		ucontext->notification = notification;
		ucontext->ready = ready;

		int ret = UCONTEXT_PUSH(target, ucontext);
		if(ret == -1) { print("dufay: failed to push ucontext on stack\n"); RETURN_ERROR; }

		target->ucontext_top = ucontext;
		VECTOR_PUSH(CORE_LOCAL->delivery_stack, target);
	}
	if(weight & NOTIFY_WEIGHT_INSTANTANEOUS) yield();

	return 0;

}

SYSCALL_DEFINE1(notification_build, struct comm_bridge*, bridge, {
	struct context *context = CORE_LOCAL->current_context;
	struct context *destination;

	int ret = bridge_to_destination(bridge, &destination);
	if(ret == -1) RETURN_ERROR;

	return notification_queue(context, destination, bridge->not, bridge->weight, 0,
		(uintptr_t)bridge->data.ptr, 0, DIV_ROUNDUP(bridge->data.length, PAGE_SIZE));
})

SYSCALL_DEFINE1(notification_broadcast, struct comm_bridge*, bridge, {

})

int notification_dispatch(struct context *context) {
	if(context == NULL) RETURN_ERROR;

	struct notification_queue *queue = context->notification.queue;
	if(unlikely(queue == NULL)) RETURN_ERROR;
	if(unlikely(queue->active == 0)) return 0; 

	spinlock(&queue->lock);

	if(!queue->active || !queue->pending) {
		spinrelease(&queue->lock);
		RETURN_ERROR;
	}

	struct ucontext *top = context->ucontext_top;

	if(top->notification && top->ready && !top->delivered) {
		struct notification_action *action =
			&context->notification.actions[context->ucontext_top->notification->notnum];
		int ret = notification_ucontext_instantiate(context, context->ucontext_top,
			context->ucontext_top->notification, action);	
		if(ret == -1) {
			print("dufay: failed to initialise ucontext\n");
			spinrelease(&queue->lock); RETURN_ERROR;
		}

		spinrelease(&queue->lock); return 0;
	}

	for(int i = 1; i <= NOTIFICATION_MAX; i++) {
		if((queue->pending & NOTIFICATION_MASK(i)) == 0 || 
			queue->mask & NOTIFICATION_MASK(i)) continue;

		struct notification_action *action = &context->notification.actions[i - 1];
		struct notification *notification;

		int ret = NOTIFICATION_POP(queue, notification, i);
		if(ret == -1) {
			print("dufay: failed to pop notification from stack\n");
			spinrelease(&queue->lock); RETURN_ERROR;
		}
		if(notification == NULL || action == NULL) continue;
	
		struct ucontext *ucontext = alloc(sizeof(struct ucontext));

		ret = notification_ucontext_instantiate(context, ucontext, notification, action);
		if(ret == -1) { spinrelease(&queue->lock); RETURN_ERROR; }

		ret = UCONTEXT_PUSH(context, ucontext);
		if(ret == -1) { print("dufay: failed to push ucontext on stack\n"); RETURN_ERROR; }

		context->ucontext_top = ucontext;

		spinrelease(&queue->lock); return 0;
	}

	spinrelease(&queue->lock); return 0;
}

SYSCALL_DEFINE1(notify, struct comm_bridge*, bridge, {
	if(bridge == NULL) RETURN_ERROR;

	struct context *context = CORE_LOCAL->current_context;
	struct context *destination;

	int ret = bridge_to_destination(bridge, &destination);
	if(ret == -1) RETURN_ERROR;
})

SYSCALL_DEFINE3(notification_action, int, not, struct notification_action *, action,
	struct notification_action *, old, {
	struct context *context = CORE_LOCAL->current_context; 

	if(unlikely(context == NULL)) RETURN_ERROR;
	if(unlikely(notification_is_valid(not) == -1)) RETURN_ERROR;

	spinlock(&context->notification.lock);

	struct notification_action *current_action = &context->notification.actions[not];
	
	if(old) {
		*old = *current_action;	
	}

	if(action) {
		*current_action = *action;
	}

	spinrelease(&context->notification.lock);
})

SYSCALL_DEFINE2(notification_define_stack, void *, sp, size_t, size, {
	struct context *context = CORE_LOCAL->current_context;
	if(unlikely(context == NULL)) RETURN_ERROR;

	struct ustack *new_stack = alloc(sizeof(struct ustack));

	new_stack->user_stack.sp = (uintptr_t)sp;
	new_stack->user_stack.size = size;
	new_stack->kernel_stack.sp = (uintptr_t)pmm_alloc(DIV_ROUNDUP(CONTEXT_DEFAULT_STACK_SIZE,
		PAGE_SIZE), 1) + CONTEXT_DEFAULT_STACK_SIZE + HIGH_VMA;
	new_stack->kernel_stack.size = CONTEXT_DEFAULT_STACK_SIZE;
	new_stack->active = 0;

	int ret = USTACK_PUSH(context, new_stack);
	if(ret == -1) RETURN_ERROR;
})

SYSCALL_DEFINE0(notification_return, {
	struct context *context = CORE_LOCAL->current_context; 
	if(context == NULL) RETURN_ERROR;

	struct notification_queue *queue = context->notification.queue;
	if(queue == NULL) panic("dufay: nqueue is null\n");

	int ret = destroy_ucontext(context, context->ucontext_active);
	if(ret == -1) panic("dufay: failed to kill active ucontext\n");

	struct ucontext *rcontext = ({
		__label__ finish;
		struct ucontext *rcontext = context->ucontext_active->last;
	
		for(; rcontext;) {
			if(rcontext->notification && !rcontext->delivered) {
				if(rcontext->notification->weight == NOTIFY_WEIGHT_INSTANTANEOUS) goto finish;
				rcontext = rcontext->last;
				continue;
			}
			goto finish;
		}
		rcontext = NULL;	
finish:
		rcontext;
	});
	if(rcontext == NULL) panic("dufay: rcontext is null\n");

	if(rcontext->notification) {
		struct notification_action *action = &context->notification.actions[rcontext->notification->notnum];
		int ret = notification_ucontext_instantiate(context, rcontext,
			rcontext->notification, action);	
		if(ret == -1) panic("dufay: cant instantiate ucontetx");

		rcontext->delivered = 1;
	}

	context->ucontext_active = rcontext;

	CORE_LOCAL->kernel_stack = rcontext->stack->kernel_stack.sp;
	CORE_LOCAL->fpu_rstor(rcontext->fpu_context);

	SWAP_TLS(&rcontext->regs);

	__asm__ volatile (
		"mov %0, %%rsp\n\t"
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
		"iretq\n\t"
		:: "r" (&rcontext->regs)
	);
})

SYSCALL_DEFINE0(notification_unmute, {
	struct context *current_context = CORE_LOCAL->current_context; 
	if(current_context == NULL) RETURN_ERROR;

	struct notification_queue *queue = current_context->notification.queue;
	if(queue == NULL) RETURN_ERROR;

	queue->active = 1;
})

SYSCALL_DEFINE0(notification_mute, {
	struct context *current_context = CORE_LOCAL->current_context; 
	if(current_context == NULL) RETURN_ERROR;

	struct notification_queue *queue = current_context->notification.queue;
	if(queue == NULL) RETURN_ERROR;

	queue->active = 0;
})

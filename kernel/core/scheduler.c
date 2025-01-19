#include <arch/x86/paging.h>
#include <arch/x86/cpu.h>
#include <arch/x86/apic.h> 
#include <arch/x86/smp.h>

#include <core/scheduler.h>
#include <core/virtual.h>
#include <core/physical.h>
#include <core/server.h>
#include <core/syscall.h>
#include <core/notification.h>
#include <core/debug.h>

#include <fayt/lock.h>
#include <fayt/string.h>
#include <fayt/compiler.h>
#include <fayt/portal.h>
#include <fayt/debug.h>

int create_blank_context(struct context *context) { 
	if(unlikely(context == NULL)) RETURN_ERROR;

	context->page_table = alloc(sizeof(struct page_table));
	vmm_default_table(context->page_table);

	context->comms.sysperm = 0;
	
	context->notification.actions = alloc(sizeof(struct notification_action) * NOTIFICATION_MAX);
	context->notification.queue = alloc(sizeof(struct notification_queue));

	int ret = NEW_CONTEXT(context);
	if(ret == -1) RETURN_ERROR;

	return 0;
}

int destroy_ucontext(struct context *context, struct ucontext *ucontext) {
	if(context == NULL || ucontext == NULL) RETURN_ERROR;

	struct notification *notification = ucontext->notification;
	if(notification) {
		struct context *current_context = CORE_LOCAL->current_context; 
		if(current_context == NULL) RETURN_ERROR;

		struct ucontext *current_ucontext = current_context->ucontext_active;
		if(current_ucontext == NULL) RETURN_ERROR;

		for(int i = 0; i < notification->etrigger.length; i++) {
			struct etrigger *etrigger = notification->etrigger.data[i];
			if(etrigger == NULL) continue;

			int ret = equeue_wake(etrigger, current_ucontext);
			if(ret == -1) RETURN_ERROR;
		}
	}

	if(context->ucontext_top == ucontext) context->ucontext_top = ucontext->last;
	if(context->ucontext_queue == ucontext) context->ucontext_queue = ucontext->next;

	if(ucontext->last) ucontext->last->next = ucontext->next;
	if(ucontext->next) ucontext->next->last = ucontext->last;

	pmm_free(ucontext->stack->kernel_stack.sp - HIGH_VMA - ucontext->stack->kernel_stack.size,
		DIV_ROUNDUP(ucontext->stack->kernel_stack.size, PAGE_SIZE));

	if(ucontext->stack->last) {
		ucontext->stack->last->next = ucontext->stack->next;
		if(ucontext->stack->next) ucontext->stack->next->last = ucontext->stack->last;
	}

	free(ucontext->stack);
	free(ucontext);

	return 0;
}

int sched_establish_shared_link(struct context *scheduler_context,
	struct cpu_local *cpu_local, const char *identifier) {
	size_t page_cnt = DIV_ROUNDUP(SCHEDULER_DEFAULT_QUEUE_SIZE, PAGE_SIZE);

	uint64_t physical_base = pmm_alloc(page_cnt, 1);
	uint64_t virtual_base = physical_base + HIGH_VMA;

	struct portal_resp resp;
	struct portal_req *req = alloc(sizeof(struct portal_req) + sizeof(uint64_t) * page_cnt);

	*req = (struct portal_req) {
		.type = PORTAL_REQ_SHARE | PORTAL_REQ_DIRECT, 
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.length = sizeof(struct portal_req) + sizeof(uint64_t) * page_cnt,
		.share = {
			.identifier = identifier,
			.length = sizeof(void*),
			.create = 1,
			.type = LINK_CIRCULAR, 
		}
	};

	req->morphology.addr = virtual_base;
	req->morphology.length = page_cnt * PAGE_SIZE;
	req->morphology.pcnt = page_cnt;
	req->morphology.paddr = physical_base;

	int ret = portal(req, &resp);
	if(ret == -1) RETURN_ERROR;

	cpu_local->thread_queue_link = (void*)(physical_base + HIGH_VMA);

	free(req);

	return 0;
}

// FIND CONTEXT BY EITHER POPPING OFF THE DELIVERY STACK, OR BY POPPING OFF THE QUEUE, IF NONE EXISTS
// RESCHEDULE TO THE SCHEDULING SERVER TO REFILL THE QUEUE.
//
// FOR A GIVEN CONTEXT, THE ACTIVE UCONTEXT IS ASSUMED UCONTEXT_TOP, BUT IF UCONTEXT_TOP IS BLKOCKED
// ITERATE DONW THE LIST UNTIL YOU FIND ONE THAT IS NOT, IF YOU CANT (ALL UCONTEXTS ARE CURRENTLY BOCKED)
// FIND ANOTHER CONTEXT AND REPEAT

static int fetch_context(struct context **context, struct ucontext **ucontext) {
	struct context *next_context = NULL;

	int ret = VECTOR_POP(CORE_LOCAL->delivery_stack, next_context);
	if(ret == 0) { goto find_ucontext; }
find_context:
	bool found = false;
	found = OPERATE_LINK(CORE_LOCAL->thread_queue_link, LINK_CIRCULAR,
		({
			circular_queue_pop((void*)CORE_LOCAL->thread_queue_link +
				CORE_LOCAL->thread_queue_link->data_offset, &next_context);
		})
	);
	
	if(found) goto find_ucontext;

	struct server *scheduling_server = CORE_LOCAL->scheduling_server;
	if(scheduling_server == NULL) panic("DUFAY: SCHEDULING SERVER DOWN");

	next_context = scheduling_server->context;
	if(next_context == NULL) panic("DUFAY: SCHEDULING SERVER DOWN");
find_ucontext:
	notification_dispatch(next_context);

	struct ucontext *next_ucontext = ({
		struct ucontext *ucontext = next_context->ucontext_top;

		struct notification_queue *nqueue = next_context->notification.queue;
		if(nqueue && nqueue->active == 0) {
			for(; ucontext;) {
				if(ucontext->notification == NULL) break;
				ucontext = ucontext->last;
			}
		}

		for(; ucontext;) {
			if(!ucontext->blocking) break;
			print("we are blocking on %x\n", ucontext); 
			ucontext = ucontext->last;
		}

		if(ucontext == NULL) {
			if(found) goto find_context;
			panic("DUFAY: SCHEDULER SERVER DOWN");
		}

		ucontext;
	});

	*context = next_context;
	*ucontext = next_ucontext;

	return 0;
}

static int cnt = 0;

void reschedule(struct registers *regs, void*) {
	if(__atomic_test_and_set(&CORE_LOCAL->sched_lock.lock, __ATOMIC_ACQUIRE)) return; 

	struct context *current_context = CORE_LOCAL->current_context;

	struct context *next_context = NULL;
	struct ucontext *next_ucontext = NULL;

	fetch_context(&next_context, &next_ucontext);

	void **fpu_context;
	struct registers *r;

	if(likely(current_context && current_context->ucontext_active)) {
		struct ucontext *ucontext = current_context->ucontext_active;

		fpu_context = &ucontext->fpu_context;
		r = &ucontext->regs;

		ucontext->sysctx.user_stack = CORE_LOCAL->user_stack;
		ucontext->sysctx.error = CORE_LOCAL->error;

		CORE_LOCAL->fpu_save(*fpu_context);

		*r = *regs;
		current_context->user_fs_base = get_user_fs();
		current_context->user_gs_base = get_user_gs();
	}

	next_context->ucontext_active = next_ucontext;
	CORE_LOCAL->kernel_stack = next_ucontext->stack->kernel_stack.sp;

	fpu_context = &next_ucontext->fpu_context;
	r = &next_ucontext->regs;

	x86_swap_tables(next_context->page_table);

	CORE_LOCAL->fpu_rstor(*fpu_context);

	CORE_LOCAL->user_stack = next_ucontext->sysctx.user_stack;
	CORE_LOCAL->error = next_ucontext->sysctx.user_stack;

	set_user_fs(next_context->user_fs_base);
	set_user_gs(next_context->user_gs_base);

	CORE_LOCAL->current_context = next_context;

	//print("rescheduling to: rip=%x on cid=%x [%s]\n", r->rip, next_context->comms.cid, next_context->comms.server ? next_context->comms.server : "NULL");

	if(next_ucontext->notification) next_ucontext->delivered = 1;

	xapic_write(XAPIC_EOI_OFF, 0);

	spinrelease(&CORE_LOCAL->sched_lock);

	SWAP_TLS(r); 

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
		:: "r" (r)
	);
}

int sched_dequeue_context(struct server *scheduling_server, struct context *context, struct sched_queue_config_set *config_set,int weight) {
	if(scheduling_server == NULL || context == NULL || config_set == NULL) RETURN_ERROR; 

	struct sched_queue_config_set *config = (void*)(pmm_alloc(1, 1) + HIGH_VMA);
	memcpy(config, config_set, sizeof(struct sched_queue_config_set) +
		config_set->cnt * sizeof(struct sched_queue_config));

	int ret = notification_queue(context, scheduling_server->context,
		NOT_SCHED_DEQUEUE, weight, 1, 0, (uint64_t)config - HIGH_VMA, 1);
	if(ret == -1) RETURN_ERROR;

	pmm_free((uintptr_t)config - HIGH_VMA, 1);

	return 0;
}

int sched_enqueue_context(struct server *scheduling_server, struct context *context, struct sched_queue_config_set *config_set, int weight) {
	if(scheduling_server == NULL || context == NULL || config_set == NULL) RETURN_ERROR;

	struct sched_queue_config_set *config = (void*)(pmm_alloc(1, 1) + HIGH_VMA);
	memcpy(config, config_set, sizeof(struct sched_queue_config_set) +
		config_set->cnt * sizeof(struct sched_queue_config));

	int ret = notification_queue(context, scheduling_server->context,
		NOT_SCHED_ENQUEUE, weight, 1, 0, (uint64_t)config - HIGH_VMA, 1);
	if(ret == -1) RETURN_ERROR;

	pmm_free((uintptr_t)config - HIGH_VMA, 1);

	return 0;
}

SYSCALL_DEFINE0(yield, {
	yield();
})

SYSCALL_DEFINE0(sched_acquire, {
	spinlock(&CORE_LOCAL->sched_lock);
})

SYSCALL_DEFINE0(sched_release, {
	spinrelease(&CORE_LOCAL->sched_lock);
})

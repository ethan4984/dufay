#include <arch/amd64/paging.h>
#include <arch/amd64/cpu.h>
#include <arch/amd64/apic.h>
#include <arch/amd64/smp.h>
#include <arch/amd64/idt.h>

#include <core/scheduler/thread.h>
#include <core/memory/virtual.h>
#include <core/memory/physical.h>
#include <core/syscall.h>
#include <core/notification.h>
#include <core/debug.h>

#include <fayt/lock.h>
#include <fayt/string.h>
#include <fayt/compiler.h>
#include <fayt/portal.h>
#include <fayt/debug.h>
#include <fayt/dictionary.h>

int destroy_context(struct thread *thread, struct context *context)
{
	if (thread == NULL || context == NULL)
		RETURN_ERROR;

	if (thread->context_top == context)
		thread->context_top = context->last;
	if (thread->context_queue == context)
		thread->context_queue = context->next;

	if (context->last)
		context->last->next = context->next;
	if (context->next)
		context->next->last = context->last;

	pmm_free(context->stack->kernel_stack.sp - HIGH_VMA -
				 context->stack->kernel_stack.size,
			 DIV_ROUNDUP(context->stack->kernel_stack.size, PAGE_SIZE));

	if (context->stack->last) {
		context->stack->last->next = context->stack->next;
		if (context->stack->next)
			context->stack->next->last = context->stack->last;
	}

	context->stack->active = false;

	struct notification *notification = context->notification;
	if (notification) {
		struct thread *current_thread = CORE_LOCAL->current_thread;
		if (current_thread == NULL)
			RETURN_ERROR;

		struct context *current_context = current_thread->context_active;
		if (current_context == NULL)
			RETURN_ERROR;

		for (size_t i = 0; i < notification->etrigger.length; i++) {
			struct etrigger *etrigger = notification->etrigger.data[i];
			if (etrigger == NULL)
				continue;

			int ret = equeue_wake(etrigger, current_context);
			if (ret == -1)
				RETURN_ERROR;
		}
	}

	return 0;
}

static int fetch_thread(struct thread **next_thread,
						struct context **next_context)
{
	if (unlikely(next_thread == NULL || next_context == NULL))
		RETURN_ERROR;

	int ret = delivery_queue_peek(&CORE_LOCAL->delivery_queue, next_thread);
	if (*next_thread) {
		goto find_context;
	}

find_thread:
	struct scheduler *scheduler = CORE_LOCAL->scheduler;

	if (unlikely(scheduler == NULL)) {
		return 1;
	}

	ret = scheduler->traverse(scheduler, next_thread);

	if (*next_thread == NULL) {
		RETURN_ERROR;
	}
	if (ret == -1)
		RETURN_ERROR;
find_context:
	notification_dispatch(*next_thread);

	*next_context = ({
		struct context *context = (*next_thread)->context_top;

		struct notification_queue *nqueue = (*next_thread)->notification.queue;
		if (nqueue && nqueue->active == 0) {
			for (; context;) {
				if (context->notification == NULL)
					break;
				context = context->last;
			}
		}

		for (; context;) {
			if (!context->blocking)
				break;
			context = context->last;
		}

		if (context == NULL) {
			goto find_thread;
		}

		context;
	});

	return 0;
}

void reschedule(struct registers *regs, void *)
{
	if (__atomic_test_and_set(&CORE_LOCAL->sched_lock.lock, __ATOMIC_ACQUIRE))
		return;

	struct thread *current_thread = CORE_LOCAL->current_thread;

	struct thread *next_thread = NULL;
	struct context *next_context = NULL;

	int ret = fetch_thread(&next_thread, &next_context);
	if (unlikely(ret == -1)) {
		REPORT_ERROR;
		panic("");
	}

	if (ret == 1) {
		spinrelease(&CORE_LOCAL->sched_lock);
		return;
	}

	void **fpu_thread;
	struct registers *r;

	if (likely(current_thread && current_thread->context_active)) {
		struct context *context = current_thread->context_active;

		fpu_thread = &context->arch_context.fpu_thread;
		r = &context->arch_context.regs;

		context->sysctx.user_stack = CORE_LOCAL->arch_cb.user_stack;
		context->sysctx.error = CORE_LOCAL->arch_cb.error;

		CORE_LOCAL->arch_cb.fpu_save(*fpu_thread);

		*r = *regs;
		current_thread->user_fs_base = get_user_fs();
		current_thread->user_gs_base = get_user_gs();
	}

	next_thread->context_active = next_context;
	CORE_LOCAL->arch_cb.kernel_stack = next_context->stack->kernel_stack.sp;

	fpu_thread = &next_context->arch_context.fpu_thread;
	r = &next_context->arch_context.regs;

	pmap_activate(next_thread->address_space->page_table->pmap);

	CORE_LOCAL->arch_cb.fpu_rstor(*fpu_thread);

	CORE_LOCAL->arch_cb.user_stack = next_context->sysctx.user_stack;
	CORE_LOCAL->arch_cb.error = next_context->sysctx.user_stack;

	set_user_fs(next_thread->user_fs_base);
	set_user_gs(next_thread->user_gs_base);

	CORE_LOCAL->current_thread = next_thread;

	/* print( */
	/* 	"Rescheduling to thread %x, on scheduler %x (CORE_LOCAL->scheduler is %x) rip is %x\n", */
	/* 	next_thread, next_thread->scheduler, CORE_LOCAL->scheduler, next_context->regs.rip); */

	/* print("rescheduling to rip=%x, rsp=%x, rflags=%x, CORE_LOCAL is %x\n", */
	/* 	  (void *)next_context->regs.rip, (void *)next_context->regs.rsp, */
	/* 	  next_context->regs.rflags, CORE_LOCAL); */

	//if (next_context->notification)
	//	next_context->delivered = 1;

	xapic_write(XAPIC_EOI_OFF, 0);

	spinrelease(&CORE_LOCAL->sched_lock);

	SWAP_TLS(r);

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
					 "iretq\n\t" ::"r"(r));
}

int archctl(int request, int *data)
{
	switch (request) {
	case ARCHCTL_RESERVE_IRQ:
		if (data == NULL)
			RETURN_ERROR;
		*data = idt_reserve_vector();
		break;
	case ARCHCTL_RELEASE_IRQ:
		if (data == NULL)
			RETURN_ERROR;
		uint8_t vector = *data & 0xff;
		idt_release_vector(vector);
		break;
	case ARCHCTL_YIELD:
		yield();
		break;
	default:
		print("archctl: unknown request [%x]\n", request);
		RETURN_ERROR;
	}

	return 0;
}

SYSCALL_DEFINE2(archctl, int, request, int *, data,
				{ return archctl(request, data); });

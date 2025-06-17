#include <arch/port.h>
#include <arch/amd64/port.h>
#include <fayt/slab.h>
#include <arch/amd64/smp.h>

void arch_context_init(struct arch_thread_context *context,
					   uintptr_t entry_point, uintptr_t stack, bool user)
{
	context->fpu_thread = alloc(CORE_LOCAL->arch_cb.fpu_thread_size);
	context->regs.rip = entry_point;
	context->regs.cs = user ? 0x43 : 0x28;
	context->regs.rflags = 0x202;
	context->regs.ss = user ? 0x3b : 0x30;
	context->regs.rsp = stack;
}

void arch_context_set_arg(struct arch_thread_context *context, uint64_t arg)
{
	context->regs.rdi = arg;
}

void arch_context_save(struct arch_thread_context *in,
					   struct arch_thread_context *out)
{
	(void)in;
	(void)out;
}

void arch_context_restore(struct arch_thread_context *context)
{
	(void)context;
}

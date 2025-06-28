#include "arch/x86/cpu.h"
#include <arch/x86/smp.h>
#include <arch/x86/paging.h>

#include <core/init.h>
#include <core/debug.h>
#include <core/elf.h>
#include <core/memory/physical.h>

#include <aria/debug.h>
#include <aria/notification.h>
#include <aria/string.h>
#include <aria/compiler.h>

static volatile struct limine_module_request limine_module_request = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0
};

struct tgroup tgroup_system;

static int launch_server(const char *, struct thread *, void *, int);

struct thread *new_kernel_thread(uintptr_t entry);

int init_system_tgroup()
{
	int ret = tgroup_insert(&tgroup_system);
	if (ret == -1)
		RETURN_ERROR;
	if (tgroup_system.tgid != TGID_SYSTEM)
		RETURN_ERROR;

	return 0;
}

int launch_init(void)
{
	if (limine_module_request.response == NULL)
		RETURN_ERROR;

	struct limine_file **modules = limine_module_request.response->modules;
	int module_count = limine_module_request.response->module_count;

	struct thread *thread_init;
	int ret = create_thread(TGID_SYSTEM, &thread_init);
	if (ret == -1)
		RETURN_ERROR;

	ret = launch_server("init", thread_init, NULL, 0);
	if (ret == -1)
		RETURN_ERROR;

	ret = enqueue_thread(thread_init);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

struct thread *new_kernel_thread(uintptr_t entry)
{
	struct thread *thread;
	int ret = create_thread(TGID_SYSTEM, &thread);
	if (ret == -1)
		return NULL;

	struct ustack *ustack = alloc(sizeof(struct ustack));

	ustack->kernel_stack.sp =
		pmm_alloc(DIV_ROUNDUP(CONTEXT_DEFAULT_STACK_SIZE, PAGE_SIZE), 1) +
		CONTEXT_DEFAULT_STACK_SIZE + HIGH_VMA;
	ustack->kernel_stack.size = CONTEXT_DEFAULT_STACK_SIZE;
	ustack->active = 1;

	ret = USTACK_PUSH(thread, ustack);
	if (ret == -1) {
		print("ERROR: unable to push ustack\n");
	}

	struct context *context = alloc(sizeof(struct context));

	context->fpu_thread = alloc(CORE_LOCAL->fpu_thread_size);
	context->stack = ustack;
	context->thread = thread;
	context->etrigger = alloc(sizeof(struct etrigger));
	context->etrigger->context = context;

	ret = CONTEXT_PUSH(thread, context);

	thread->context_top = context;

	context->regs.rip = entry;
	context->regs.cs = 0x28; // kernel code segment
	context->regs.rflags = 0x202;
	context->regs.ss = 0x30; // kernel data segment
	context->regs.rsp = ustack->kernel_stack.sp;

	thread->user_fs_base = rdmsr(MSR_FS_BASE);
	thread->user_gs_base = rdmsr(MSR_GS_BASE);

	thread->address_space = &kernel_mappings;

	return thread;
}

static int launch_server(const char *identifier, struct thread *thread,
						 void *arg, int arg_length)
{
	if (unlikely(identifier == NULL || thread == NULL))
		RETURN_ERROR;

	struct limine_file **modules = limine_module_request.response->modules;
	int module_count = limine_module_request.response->module_count;

	struct limine_file *file = ({
		__label__ finish;
		struct limine_file *ret = NULL;
		for (uint64_t i = 0; i < module_count; i++) {
			if (strcmp(modules[i]->cmdline, identifier) == 0) {
				ret = modules[i];
				goto finish;
			}
		}
finish:
		ret;
	});
	if (file == NULL)
		RETURN_ERROR;

	struct elf64_file *elf = alloc(sizeof(struct elf64_file));
	if (unlikely(elf == NULL))
		RETURN_ERROR;

	struct elf64_file_buffer *file_buffer =
		alloc(sizeof(struct elf64_file_buffer));
	if (unlikely(file_buffer == NULL))
		RETURN_ERROR;

	file_buffer->data = file->address;
	file_buffer->length = file->size;
	file_buffer->address_space = thread->address_space;

	elf->elf64_read = elf64_read;
	elf->elf64_write = elf64_write;
	elf->elf64_map = elf64_map;
	elf->private = file_buffer;

	elf->aslr = &thread->address_space->aslr;

	int ret = elf64_file_init(elf);
	if (ret == -1)
		RETURN_ERROR;

	print("ASLR: applied to [%s]: %x -> %x\n", identifier,
		  elf->aslr_layout->lower_bound, elf->aslr_layout->upper_bound);

	ret = elf64_file_aux(elf, &elf->aux);
	if (ret == -1)
		RETURN_ERROR;

	struct ustack *ustack = alloc(sizeof(struct ustack));
	if (ustack == NULL)
		RETURN_ERROR;

	ustack->kernel_stack.sp =
		pmm_alloc(DIV_ROUNDUP(CONTEXT_DEFAULT_STACK_SIZE, PAGE_SIZE), 1) +
		CONTEXT_DEFAULT_STACK_SIZE + HIGH_VMA;
	if (!ustack->kernel_stack.sp)
		RETURN_ERROR;
	ustack->kernel_stack.size = CONTEXT_DEFAULT_STACK_SIZE;
	ustack->user_stack.sp =
		SERVER_DEFAULT_STACK_LOCATION + SERVER_DEFAULT_STACK_SIZE;
	ustack->user_stack.size = SERVER_DEFAULT_STACK_SIZE;
	ustack->active = 1;

	ret = USTACK_PUSH(thread, ustack);
	if (ret == -1) {
		print("ERROR: unable to push ustack\n");
		RETURN_ERROR;
	}

	struct context *context = alloc(sizeof(struct context));
	if (context == NULL)
		RETURN_ERROR;

	context->fpu_thread = alloc(CORE_LOCAL->fpu_thread_size);
	if (context->fpu_thread == NULL)
		RETURN_ERROR;
	context->stack = ustack;
	context->thread = thread;
	context->etrigger = alloc(sizeof(struct etrigger));
	if (context->etrigger == NULL)
		RETURN_ERROR;
	context->etrigger->context = context;

	ret = CONTEXT_PUSH(thread, context);
	if (ret == -1) {
		print("ERROR: failed to push context on stack\n");
		RETURN_ERROR;
	}

	thread->context_top = context;

	ret = elf64_file_load(elf);
	if (ret == -1)
		RETURN_ERROR;

	context->regs.rip = elf->aux.at_entry;
	context->regs.cs = 0x43;
	context->regs.rflags = 0x202;
	context->regs.ss = 0x3b;

	uintptr_t stack_physical =
		pmm_alloc(context->stack->user_stack.sp / PAGE_SIZE, 1) +
		SERVER_DEFAULT_STACK_SIZE;
	if (!stack_physical)
		RETURN_ERROR;
	uintptr_t stack_virtual = context->stack->user_stack.sp;

	for (size_t i = 0; i < SERVER_DEFAULT_STACK_SIZE / PAGE_SIZE; i++) {
		thread->address_space->page_table->map_page(
			thread->address_space->page_table, stack_virtual - PAGE_SIZE * i,
			stack_physical - PAGE_SIZE * i,
			X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_US);
	}

	char *location = (void *)(stack_physical + HIGH_VMA);

	if (arg) {
		location = (void *)(((uintptr_t)location - arg_length) & ~15);
		memcpy(location, arg, arg_length);
		context->regs.rdi =
			stack_virtual - (stack_physical - ((uint64_t)location - HIGH_VMA));
	}

	location = (void *)((uint64_t)location & -16ll);
	context->regs.rsp =
		stack_virtual - (stack_physical - ((uint64_t)location - HIGH_VMA));

	return 0;
}

struct limine_file *limine_search_module(const char *identifier)
{
	if (limine_module_request.response == NULL)
		return NULL;

	struct limine_file **modules = limine_module_request.response->modules;
	uint64_t module_count = limine_module_request.response->module_count;

	for (uint64_t i = 0; i < module_count; i++) {
		if (strcmp(modules[i]->cmdline, identifier) == 0) {
			return modules[i];
		}
	}

	return NULL;
}

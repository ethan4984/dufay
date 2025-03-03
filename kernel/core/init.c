#include <arch/x86/smp.h>
#include <arch/x86/paging.h>

#include <core/init.h>
#include <core/debug.h>
#include <core/elf.h>
#include <core/physical.h>

#include <fayt/debug.h>
#include <fayt/notification.h>
#include <fayt/string.h>
#include <fayt/compiler.h>

static volatile struct limine_module_request limine_module_request = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0
};

struct sched_cgroup cgroup_system;

static int launch_schedulers(void);
static int launch_server(const char *, struct context *, void *, int);

int launch_init(void)
{
	if (limine_module_request.response == NULL)
		RETURN_ERROR;

	int ret = cgroup_insert(&cgroup_system);
	if (ret == -1)
		RETURN_ERROR;

	struct limine_file **modules = limine_module_request.response->modules;
	int module_count = limine_module_request.response->module_count;

	ret = launch_schedulers();
	if (ret == -1)
		RETURN_ERROR;

	struct context *context_init;
	ret = create_context(CGID_SYSTEM, &context_init);
	if (ret == -1)
		RETURN_ERROR;

	ret = launch_server("init", context_init, NULL, 0);
	if (ret == -1)
		RETURN_ERROR;

	struct sched_queue_config_set *queue_set =
		alloc(sizeof(struct sched_queue_config_set) +
			  sizeof(struct sched_queue_config));

	queue_set->cnt = 1;
	queue_set->config->proc_id = context_init->comms.proc_id;

	ret = sched_enqueue_context(CORE_LOCAL->scheduling_context, context_init,
								queue_set, NOTIFY_WEIGHT_TICK);
	if (ret == -1)
		RETURN_ERROR;

	free(queue_set);

	return 0;
}

static int launch_server(const char *identifier, struct context *context,
						 void *arg, int arg_length)
{
	if (unlikely(identifier == NULL || context == NULL))
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
	file_buffer->page_table = context->page_table;

	elf->elf64_read = elf64_read;
	elf->elf64_write = elf64_write;
	elf->elf64_map = elf64_map;
	elf->private = file_buffer;

	struct aslr *aslr = alloc(sizeof(struct aslr));
	if (unlikely(aslr == NULL))
		RETURN_ERROR;

	*aslr = (struct aslr){ .layout = NULL,
						   .minimum_vaddr = 0x100000000000,
						   .maximum_vaddr = 0x7fffffffffff };

	elf->aslr = aslr;

	int ret = elf64_file_init(elf);
	if (ret == -1)
		RETURN_ERROR;

	print("ASLR: applied to [%s]: %x -> %x\n", identifier,
		  elf->aslr_layout->lower_bound, elf->aslr_layout->upper_bound);

	ret = elf64_file_aux(elf, &elf->aux);
	if (ret == -1)
		RETURN_ERROR;

	struct ustack *ustack = alloc(sizeof(struct ustack));

	ustack->kernel_stack.sp =
		pmm_alloc(DIV_ROUNDUP(CONTEXT_DEFAULT_STACK_SIZE, PAGE_SIZE), 1) +
		CONTEXT_DEFAULT_STACK_SIZE + HIGH_VMA;
	ustack->kernel_stack.size = CONTEXT_DEFAULT_STACK_SIZE;
	ustack->user_stack.sp =
		SERVER_DEFAULT_STACK_LOCATION + SERVER_DEFAULT_STACK_SIZE;
	ustack->user_stack.size = SERVER_DEFAULT_STACK_SIZE;
	ustack->active = 1;

	ret = USTACK_PUSH(context, ustack);
	if (ret == -1) {
		print("ERROR: unable to push ustack\n");
		RETURN_ERROR;
	}

	struct ucontext *ucontext = alloc(sizeof(struct ucontext));

	ucontext->fpu_context = alloc(CORE_LOCAL->fpu_context_size);
	ucontext->stack = ustack;
	ucontext->context = context;
	ucontext->etrigger = alloc(sizeof(struct etrigger));
	ucontext->etrigger->ucontext = ucontext;

	ret = UCONTEXT_PUSH(context, ucontext);
	if (ret == -1) {
		print("ERROR: failed to push ucontext on stack\n");
		RETURN_ERROR;
	}

	context->ucontext_top = ucontext;

	ret = elf64_file_load(elf);
	if (ret == -1)
		RETURN_ERROR;

	ucontext->regs.rip = elf->aux.at_entry;
	ucontext->regs.cs = 0x43;
	ucontext->regs.rflags = 0x202;
	ucontext->regs.ss = 0x3b;

	uintptr_t stack_physical =
		pmm_alloc(ucontext->stack->user_stack.sp / PAGE_SIZE, 1) +
		SERVER_DEFAULT_STACK_SIZE;
	uintptr_t stack_virtual = ucontext->stack->user_stack.sp;

	for (size_t i = 0; i < SERVER_DEFAULT_STACK_SIZE / PAGE_SIZE; i++) {
		context->page_table->map_page(
			context->page_table, stack_virtual - PAGE_SIZE * i,
			stack_physical - PAGE_SIZE * i,
			X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_US);
	}

	char *location = (void *)(stack_physical + HIGH_VMA);

	if (arg) {
		location = (void *)(((uintptr_t)location - arg_length) & ~15);
		memcpy(location, arg, arg_length);
		ucontext->regs.rdi =
			stack_virtual - (stack_physical - ((uint64_t)location - HIGH_VMA));
	}

	location = (void *)((uint64_t)location & -16ll);
	ucontext->regs.rsp =
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

static int launch_schedulers(void)
{
	struct sched_descriptor *descriptors = ({
		size_t page_cnt = DIV_ROUNDUP(0x10000, PAGE_SIZE);
		uint64_t physical_base = pmm_alloc(page_cnt, 1);
		uint64_t virtual_base = physical_base + HIGH_VMA;

		struct portal_resp resp;
		struct portal_req req = { .type = PORTAL_REQ_SHARE | PORTAL_REQ_DIRECT,
								  .prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
								  .length = sizeof(struct portal_req),
								  .share = { .identifier = "SCHEDULER META",
											 .type = LINK_RAW,
											 .create = 1 },
								  .morphology = { .addr = virtual_base,
												  .length =
													  page_cnt * PAGE_SIZE,
												  .pcnt = page_cnt,
												  .paddr = physical_base } };

		int ret = portal(&req, &resp);
		if (ret == -1)
			RETURN_ERROR;

		(struct sched_descriptor *)(virtual_base + sizeof(struct portal_link));
	});

	struct context *scheduler_context[bootable_processor_cnt];

	for (int i = 0; i < bootable_processor_cnt; i++) {
		struct sched_descriptor *descriptor = descriptors + i;

		descriptor->timer = invariant_tsc;
		descriptor->timer.read = NULL;
		descriptor->processor_id = i;
		descriptor->queue_default_refill = 0xa;
		descriptor->load = 0;
		descriptor->slice =
			(struct time){ .sec = 0, .nsec = MS_TO_NS(SCHED_TICK_RATE_MS) };

		scheduler_context[i] = alloc(sizeof(struct context));

		int ret = create_context(CGID_SYSTEM, &scheduler_context[i]);
		if (unlikely(ret == -1 || scheduler_context[i] == NULL))
			RETURN_ERROR;

		ret = launch_server("scheduler", scheduler_context[i], descriptor,
							sizeof(struct sched_descriptor));
		if (ret == -1)
			RETURN_ERROR;

		char name[SERVER_MAX_NAME_LENGTH];
		ret = sched_establish_shared_link(
			scheduler_context[i], logical_processor_locales + i, ({
				sprint(name, "SCHEDULER CORE%d", i);
				name;
			}));
		if (ret == -1)
			RETURN_ERROR;

		(logical_processor_locales + i)->scheduling_context =
			scheduler_context[i];
	}

	return 0;
}

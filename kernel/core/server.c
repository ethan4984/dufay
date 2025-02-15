#include <core/handle.h>
#include <arch/x86/paging.h>
#include <core/message.h>
#include <arch/x86/smp.h>
#include <arch/x86/idt.h>

#include <core/server.h>
#include <core/virtual.h>
#include <core/physical.h>
#include <core/scheduler.h>
#include <core/elf.h>
#include <core/debug.h>
#include <core/notification.h>
#include <core/syscall.h>

#include <acpi/rsdp.h>

#include <fayt/slab.h>
#include <fayt/hash.h>
#include <fayt/bitmap.h>
#include <fayt/string.h>
#include <fayt/notification.h>
#include <fayt/pci.h>
#include <fayt/debug.h>
#include <fayt/sched.h>

static struct hash_table namespace_table;
static struct bitmap nid_bitmap;

static volatile struct limine_module_request limine_module_request = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0
};

struct sched_cgroup cgroup_system;

static int launch_schedulers(struct limine_file *);
static int launch_server(struct server *, void *, int);

struct server *master_scheduler;

int create_server(const char *namespace_name, const char *name,
				  struct server *server)
{
	struct namespace *namespace = NULL;
	int ret = hash_table_search(&namespace_table, (void *)namespace_name,
								strlen(namespace_name), (void **)&namespace);
	if (ret == -1 || namespace == NULL) {
		RETURN_ERROR;
	}

	struct server_id id =
		(struct server_id){ .nid = namespace->nid,
							.sid = ({
								int sid;
								int ret =
									bitmap_alloc(&namespace->sid_bitmap, &sid);
								ret == -1 ? ret : sid;
							}) };

	*server = (struct server){ .name = ({
								   void *copy = alloc(strlen(name) + 1);
								   strcpy(copy, name);
								   copy;
							   }),
							   .id = id };

	ret = hash_table_push(&namespace->server_table, (void *)server->name,
						  server, strlen(server->name));
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int create_namespace(const char *name)
{
	struct namespace *namespace = alloc(sizeof(struct namespace));

	*namespace = (struct namespace){ .name = ({
										 void *copy = alloc(strlen(name) + 1);
										 strcpy(copy, name);
										 copy;
									 }),
									 .nid = ({
										 int nid;
										 int ret =
											 bitmap_alloc(&nid_bitmap, &nid);
										 ret == -1 ? ret : nid;
									 }) };

	int ret = hash_table_push(&namespace_table, (void *)namespace->name,
							  namespace, strlen(namespace->name));
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

struct server *find_server(const char *namespace_name, const char *server_name)
{
	if (namespace_name == NULL || server_name == NULL)
		return NULL;

	struct namespace *namespace = NULL;
	int ret = hash_table_search(&namespace_table, (void *)namespace_name,
								strlen(namespace_name), (void **)&namespace);
	if (ret == -1 || namespace == NULL) {
		return NULL;
	}

	struct server *server = NULL;
	ret = hash_table_search(&namespace->server_table, (void *)server_name,
							strlen(server_name), (void **)&server);
	if (ret == -1 || server == NULL)
		return NULL;

	return server;
}

int spawn_server(const char *namespace, const char *identifier)
{
	struct server *server = find_server(namespace, identifier);
	if (server == NULL)
		RETURN_ERROR;

	struct sched_queue_config_set *queue_set =
		alloc(sizeof(struct sched_queue_config_set) +
			  sizeof(struct sched_queue_config));

	queue_set->cnt = 1;
	*queue_set->config =
		(struct sched_queue_config){ .proc_id =
										 server->context->comms.proc_id };

	int ret = sched_enqueue_context(master_scheduler, server->context,
									queue_set, NOTIFY_WEIGHT_TICK);
	if (ret == -1)
		RETURN_ERROR;

	free(queue_set);

	return 0;
}

SYSCALL_DEFINE4(server_activate, const char *, namespace, const char *,
				identifier, void *, arg, int, length, {
					if (namespace == NULL || identifier == NULL)
						return -1;

					struct server *server = find_server(namespace, identifier);
					if (server == NULL)
						return -1;

					int ret = launch_server(server, arg, length);
					if (ret == -1) {
						print("ERROR: failed to launch server {%s}\n",
							  server->name);
						RETURN_ERROR;
					}

					ret = spawn_server(namespace, identifier);
					if (ret == -1)
						return -1;
				})

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

int launch_servers(void)
{
	if (limine_module_request.response == NULL) {
		RETURN_ERROR;
	}

	int ret = cgroup_insert(&cgroup_system);
	if (ret == -1)
		RETURN_ERROR;

	struct limine_file **modules = limine_module_request.response->modules;
	uint64_t module_count = limine_module_request.response->module_count;

	print("booting servers\n");

	for (uint64_t i = 0; i < module_count; i++) {
		if (strcmp(modules[i]->cmdline, "scheduler") == 0) {
			launch_schedulers(modules[i]);
		}
	}

	ret = create_namespace("IO");
	if (ret == -1)
		RETURN_ERROR;

	for (uint64_t i = 0; i < module_count; i++) {
		if (strcmp(modules[i]->cmdline, "pci") != 0 &&
			strcmp(modules[i]->cmdline, "ahci") != 0 &&
			strcmp(modules[i]->cmdline, "nvme") != 0 &&
			strcmp(modules[i]->cmdline, "init"))
			continue;

		print("launching IO server [%s]\n", modules[i]->cmdline);

		struct server *server = alloc(sizeof(struct server));

		ret = create_server("IO", modules[i]->cmdline, server);
		if (ret == -1) {
			print("ERROR: failed to initiate server meta {%s}\n",
				  modules[i]->cmdline);
			RETURN_ERROR;
		}

		server->file = modules[i];

		if (strcmp(modules[i]->cmdline, "pci") == 0) {
			struct mcfg *mcfg = acpi_find_sdt("MCFG");
			if (mcfg == NULL)
				RETURN_ERROR;

			struct pci_server_meta *server_meta =
				alloc(sizeof(struct pci_server_meta) +
					  logical_processor_cnt * sizeof(*server_meta->lapic_id) +
					  mcfg->length);

			server_meta->logical_processor_cnt = logical_processor_cnt;
			for (int i = 0; i < server_meta->logical_processor_cnt; i++) {
				server_meta->lapic_id[i] = logical_processor_locales[i].apic_id;
			}

			server_meta->mcfg =
				(void *)server_meta + sizeof(struct pci_server_meta) +
				logical_processor_cnt * sizeof(*server_meta->lapic_id);
			memcpy(server_meta->mcfg, mcfg, mcfg->length);

			int ret = launch_server(server, server_meta,
									sizeof(struct pci_server_meta) +
										logical_processor_cnt *
											sizeof(*server_meta->lapic_id) +
										mcfg->length);
			if (ret == -1) {
				print("ERROR: failed to launch server {%s}\n",
					  modules[i]->cmdline);
				RETURN_ERROR;
			}
		}

		if (strcmp(modules[i]->cmdline, "init") == 0) {
			int ret = launch_server(server, NULL, 0);
			if (ret == -1) {
				print("ERROR: failed to launch server {%s}\n",
					  modules[i]->cmdline);
				RETURN_ERROR;
			}
		}
	}

	ret = spawn_server("IO", "pci");
	if (ret == -1)
		RETURN_ERROR;

	ret = spawn_server("IO", "init");
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

static int launch_server(struct server *server, void *arg, int arg_length)
{
	if (server == NULL)
		RETURN_ERROR;

	struct elf64_file *elf = alloc(sizeof(struct elf64_file));
	elf->data.buffer = server->file->address;
	elf->data.length = server->file->size;

	struct aslr *aslr = alloc(sizeof(struct aslr));

	*aslr = (struct aslr){ .layout = NULL,
						   .minimum_vaddr = 0x100000000000,
						   .maximum_vaddr = 0x7fffffffffff };

	elf->aslr = aslr;

	int ret = elf64_file_init(elf);
	if (ret == -1)
		RETURN_ERROR;

	print("ASLR: applied to [%s]: %x -> %x\n", server->name,
		  elf->aslr_layout->lower_bound, elf->aslr_layout->upper_bound);

	ret = elf64_file_aux(elf, &elf->aux);
	if (ret == -1)
		RETURN_ERROR;

	struct context *context = NULL;
	ret = create_context(cgroup_system.cgid, &context);
	if (ret == -1 || context == NULL)
		RETURN_ERROR;

	context->comms.server = server->name;

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

	elf->page_table = context->page_table;

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

	server->context = context;

	return 0;
}

static int launch_schedulers(struct limine_file *file)
{
	int ret = create_namespace("SCHEDULER");
	if (ret == -1)
		RETURN_ERROR;

	struct server *servers[bootable_processor_cnt];

	for (int i = 0; i < bootable_processor_cnt; i++) {
		servers[i] = alloc(sizeof(struct server));
		master_scheduler = servers[i];

		char *server_name = alloc(SERVER_MAX_NAME_LENGTH);
		sprint(server_name, "SCHEDULER CORE%d", i);

		ret = create_server("SCHEDULER", server_name, servers[i]);
		if (ret == -1)
			RETURN_ERROR;

		logical_processor_locales[i].scheduling_server = servers[i];
		servers[i]->file = file;
	}

	struct sched_descriptor *descriptors = ({
		size_t page_cnt = DIV_ROUNDUP(bootable_processor_cnt *
										  sizeof(struct sched_descriptor),
									  PAGE_SIZE);
		uint64_t physical_base = pmm_alloc(page_cnt, 1);
		uint64_t virtual_base = physical_base + HIGH_VMA;

		struct portal_resp resp;
		struct portal_req *req =
			alloc(sizeof(struct portal_req) + sizeof(uint64_t) * page_cnt);

		*req =
			(struct portal_req){ .type = PORTAL_REQ_SHARE | PORTAL_REQ_DIRECT,
								 .prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
								 .length = sizeof(struct portal_req) +
										   sizeof(uint64_t) * page_cnt,
								 .share = { .identifier = "SCHEDULER META",
											.type = LINK_RAW,
											.create = 1 } };

		req->morphology.addr = virtual_base;
		req->morphology.length = page_cnt * PAGE_SIZE;
		req->morphology.pcnt = page_cnt;
		req->morphology.paddr = physical_base;

		ret = portal(req, &resp);
		if (ret == -1)
			RETURN_ERROR;
		free(req);

		(struct sched_descriptor *)virtual_base;
	});

	for (int i = 0; i < bootable_processor_cnt; i++) {
		struct sched_descriptor *descriptor = descriptors + i;

		descriptor->timer = invariant_tsc;
		descriptor->timer.read = NULL;
		descriptor->processor_id = i;
		descriptor->queue_default_refill = 0xa;
		descriptor->load = 0;
		descriptor->slice =
			(struct time){ .sec = 0, .nsec = MS_TO_NS(SCHED_TICK_RATE_MS) };

		if (launch_server(servers[i], descriptor,
						  sizeof(struct sched_descriptor)) == -1) {
			print("ERROR: failed to launch server\n");
			continue;
		}

		int ret = sched_establish_shared_link(servers[i]->context,
											  logical_processor_locales + i,
											  servers[i]->name);
		if (ret == -1)
			RETURN_ERROR;
	}

	return 0;
}

#include <arch/x86/paging.h>
#include <arch/x86/smp.h> 

#include <core/server.h>
#include <core/virtual.h>
#include <core/physical.h>
#include <core/scheduler.h>
#include <core/elf.h>
#include <core/debug.h>
#include <core/notification.h>
#include <core/syscall.h>

#include <fayt/slab.h>
#include <fayt/hash.h>
#include <fayt/bitmap.h>
#include <fayt/string.h>
#include <fayt/notification.h>
#include <fayt/debug.h>

static struct hash_table namespace_table;
static struct bitmap nid_bitmap;

struct hash_table context_table;
struct bitmap cid_bitmap;

static volatile struct limine_module_request limine_module_request = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0
};

static int launch_schedulers(struct limine_file*);
static int launch_server(struct server*, void*, int);

struct server *master_scheduler;

int create_server(const char *namespace_name, const char *name, struct server *server) {
	struct namespace *namespace;
	int ret = hash_table_search(&namespace_table, (void*)namespace_name,
		strlen(namespace_name), (void**)&namespace);
	if(ret == -1 || namespace == NULL) {
		RETURN_ERROR;
	}

	struct server_id id = (struct server_id) {
		.nid = namespace->nid,
		.sid = ({ int sid; int ret = bitmap_alloc(&namespace->sid_bitmap, &sid); ret == -1 ? ret : sid; })
	};

	*server = (struct server) {
		.name = ({void *copy = alloc(strlen(name) + 1); strcpy(copy, name); copy;}),
		.id = id
	};

	ret = hash_table_push(&namespace->server_table, (void*)server->name,
		server, strlen(server->name));
	if(ret == -1) RETURN_ERROR;

	return 0;
}

int create_namespace(const char *name) {
	struct namespace *namespace = alloc(sizeof(struct namespace));

	*namespace = (struct namespace) {
		.name = ({void *copy = alloc(strlen(name) + 1); strcpy(copy, name); copy; }),
		.nid = ({ int nid; int ret = bitmap_alloc(&nid_bitmap, &nid); ret == -1 ? ret : nid; })
	};

	int ret = hash_table_push(&namespace_table, (void*)namespace->name,
		namespace, strlen(namespace->name));
	if(ret == -1) RETURN_ERROR;

	return 0;
}

struct server *find_server(const char *namespace_name, const char *server_name) {
	struct namespace *namespace;
	int ret = hash_table_search(&namespace_table, (void*)namespace_name,
		strlen(namespace_name), (void**)&namespace);
	if(ret == -1 || namespace == NULL) {
		return NULL;
	}

	struct server *server;
	ret = hash_table_search(&namespace->server_table,
			(void*)server_name, strlen(server_name), (void**)&server);
	if(ret == -1) return NULL;

	return server;
}

int launch_servers(void) {
	if(limine_module_request.response == NULL) {
		RETURN_ERROR;
	}

	struct limine_file **modules = limine_module_request.response->modules;
	uint64_t module_count = limine_module_request.response->module_count;

	print("dufay: booting servers {%x}\n", module_count);

	for(uint64_t i = 0; i < module_count; i++)
		if(strcmp(modules[i]->cmdline, "scheduler") == 0)
			launch_schedulers(modules[i]);

	int ret = create_namespace("IO");
	if(ret == -1) RETURN_ERROR;

	for(uint64_t i = 0; i < module_count; i++) {
		if(strcmp(modules[i]->cmdline, "vfs") != 0 &&
			strcmp(modules[i]->cmdline, "pci") != 0 &&
			strcmp(modules[i]->cmdline, "ahci") != 0 && 
			strcmp(modules[i]->cmdline, "nvme") != 0 &&
			strcmp(modules[i]->cmdline, "block") != 0 && 
			strcmp(modules[i]->cmdline, "ext") != 0) continue;

		print("dufay: launching server {%s}\n", modules[i]->cmdline);

		char *server_name = alloc(SERVER_MAX_NAME_LENGTH);
		sprint(server_name, "%s", modules[i]->cmdline);

		struct server *server = alloc(sizeof(struct server));

		ret = create_server("IO", server_name, server);
		if(ret == -1) { 
			print("dufay: failed to initiate server meta {%s}\n", modules[i]->cmdline);
			RETURN_ERROR;
		}

		server->file = modules[i];
	
		ret = launch_server(server, NULL, 0);
		if(ret == -1) {
			print("dufay: failed to launch server {%s}\n", modules[i]->cmdline);
			RETURN_ERROR;
		}

		struct sched_queue_config *config = (void*)(pmm_alloc(1, 1) + HIGH_VMA);

		config->cid = server->context->comms.cid;
		config->cgroup = 0;

		config->nice = 0;
		if(strcmp(modules[i]->cmdline, "pci") == 0) config->nice = 0;
		if(strcmp(modules[i]->cmdline, "nvme") == 0) config->nice = 7;
		if(strcmp(modules[i]->cmdline, "vfs") == 0) config->nice = 11;

		config->offload = 0;

		ret = notification_queue(server->context, master_scheduler->context,
			SCHED_NOTIFY_ENQUEUE, NOTIFY_WEIGHT_TICK, 1, 0, (uint64_t)config - HIGH_VMA, 1);
		if(ret == -1) {
			print("dufay: failed to send scheduling notification on {%s}\n", modules[i]->cmdline);
			RETURN_ERROR;
		}
	}

	return 0;
}

static int launch_server(struct server *server, void *arg, int arg_length) {
	if(server == NULL) RETURN_ERROR;

	struct elf64_file *elf = alloc(sizeof(struct elf64_file));

	elf->data.buffer = server->file->address;
	elf->data.length = server->file->size;

	int ret = elf64_file_init(elf);
	if(ret == -1) RETURN_ERROR;

	ret = elf64_file_aux(elf, &elf->aux);
	if(ret == -1) RETURN_ERROR;

	struct context *context = alloc(sizeof(struct context));

	ret = create_blank_context(context); 
	if(ret == -1) RETURN_ERROR;

	struct ustack *ustack = alloc(sizeof(struct ustack));

	ustack->kernel_stack.sp = pmm_alloc(DIV_ROUNDUP(CONTEXT_DEFAULT_STACK_SIZE, PAGE_SIZE), 1)
		+ CONTEXT_DEFAULT_STACK_SIZE + HIGH_VMA;
	ustack->kernel_stack.size = CONTEXT_DEFAULT_STACK_SIZE;
	ustack->user_stack.sp = SERVER_DEFAULT_STACK_LOCATION + SERVER_DEFAULT_STACK_SIZE;
	ustack->user_stack.size = SERVER_DEFAULT_STACK_SIZE;
	ustack->active = 1;

	ret = USTACK_PUSH(context, ustack);
	if(ret == -1) { print("dufay: unable to push ustack\n"); RETURN_ERROR; }

	struct ucontext *ucontext = alloc(sizeof(struct ucontext));

	ucontext->fpu_context = alloc(CORE_LOCAL->fpu_context_size);
	ucontext->stack = ustack;

	ret = UCONTEXT_PUSH(context, ucontext);
	if(ret == -1) { print("dufay: failed to push ucontext on stack\n"); RETURN_ERROR; }
	
	context->ucontext_top = ucontext;

	elf->page_table = context->page_table;

	ret = elf64_file_load(elf);
	if(ret == -1) RETURN_ERROR;

	ucontext->regs.rip = elf->aux.at_entry;
	ucontext->regs.cs = 0x43;
	ucontext->regs.rflags = 0x202;
	ucontext->regs.ss = 0x3b;

	uintptr_t stack_physical = pmm_alloc(ucontext->stack->user_stack.sp / PAGE_SIZE, 1) + 
		SERVER_DEFAULT_STACK_SIZE; 
	uintptr_t stack_virtual = ucontext->stack->user_stack.sp;

	for(size_t i = 0; i < SERVER_DEFAULT_STACK_SIZE / PAGE_SIZE; i++) {
		context->page_table->map_page(context->page_table, stack_virtual - PAGE_SIZE * i,
				stack_physical - PAGE_SIZE * i,
				X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_US);
	}

	char *location = (void*)(stack_physical + HIGH_VMA);

	if(arg) {
		location -= arg_length;
		memcpy(location, arg, arg_length);
		ucontext->regs.rdi = stack_virtual - (stack_physical - ((uint64_t)location - HIGH_VMA));
	}

	location = (void*)((uint64_t)location & -16ll);
	ucontext->regs.rsp = stack_virtual - (stack_physical - ((uint64_t)location - HIGH_VMA));

	server->context = context;

	return 0;
}

static int launch_schedulers(struct limine_file *file) {
	int ret = create_namespace("SCHEDULER");
	if(ret == -1) RETURN_ERROR;

	struct server *servers[bootable_processor_cnt];

	for(int i = 0; i < bootable_processor_cnt; i++) {
		servers[i] = alloc(sizeof(struct server));
		master_scheduler = servers[i];

		char *server_name = alloc(SERVER_MAX_NAME_LENGTH);
		sprint(server_name, "SCHEDULER CORE%d", i);

		ret = create_server("SCHEDULER", server_name, servers[i]);
		if(ret == -1) RETURN_ERROR;

		logical_processor_locales[i].scheduling_server = servers[i];
		servers[i]->file = file;
	}

	struct sched_descriptor *descriptors = ({
		size_t page_cnt = DIV_ROUNDUP(bootable_processor_cnt * sizeof(struct sched_descriptor), PAGE_SIZE);
		uint64_t physical_base = pmm_alloc(page_cnt, 1);
		uint64_t virtual_base = physical_base + HIGH_VMA;

		struct portal_resp resp;
		struct portal_req *req = alloc(sizeof(struct portal_req) + sizeof(uint64_t) * page_cnt);

		*req = (struct portal_req) {
			.type = PORTAL_REQ_SHARE | PORTAL_REQ_DIRECT, 
			.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
			.length = sizeof(struct portal_req) + sizeof(uint64_t) * page_cnt,
			.share = {
				.identifier = "SCHEDULER META",
				.type = LINK_RAW, 
				.create = 1
			}
		};

		req->morphology.addr = virtual_base;
		req->morphology.length = page_cnt * PAGE_SIZE;
		for(int i = 0; i < page_cnt; i++, physical_base += PAGE_SIZE) req->morphology.paddr[i] = physical_base;

		ret = portal(req, &resp);
		if(ret == -1) RETURN_ERROR;
		free(req);

		(struct sched_descriptor*)virtual_base;
	});

	for(int i = 0; i < bootable_processor_cnt; i++) {
		struct sched_descriptor *descriptor = descriptors + i;

		descriptor->processor_id = i;
		descriptor->queue_default_refill = 0xa;
		descriptor->load = 0;

		if(launch_server(servers[i], descriptor, sizeof(struct sched_descriptor)) == -1) {
			print("dufay: failed to launch server\n");
			continue;
		}

		int ret = sched_establish_shared_link(servers[i]->context,
			logical_processor_locales + i, servers[i]->name);
		if(ret == -1) RETURN_ERROR;
	}

	return 0;
}

SYSCALL_DEFINE2(context, int, cid, void**, private, {
	struct context *context;

	int ret = SEARCH_CONTEXT(cid, &context);
	if(ret == -1) RETURN_ERROR;

	*private = context;	
})

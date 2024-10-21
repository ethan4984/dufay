#include <arch/x86/paging.h>
#include <arch/x86/smp.h> 

#include <core/server.h>
#include <core/virtual.h>
#include <core/physical.h>
#include <core/scheduler.h>
#include <core/elf.h>
#include <core/debug.h>

#include <fayt/slab.h>
#include <fayt/hash.h>
#include <fayt/bitmap.h>
#include <fayt/string.h>

static struct hash_table namespace_table;
static struct bitmap nid_bitmap;

struct hash_table context_table;
struct bitmap cid_bitmap;

static volatile struct limine_module_request limine_module_request = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0
};

static int launch_schedulers(struct limine_file*);

int create_server(const char *namespace_name, const char *name, struct server *server) {
	struct namespace *namespace;
	int ret = hash_table_search(&namespace_table, (void*)namespace_name,
		strlen(namespace_name), (void**)&namespace);
	if(ret == -1 || namespace == NULL) {
		return -1;
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
	if(ret == -1) return -1;

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
	if(ret == -1) return -1;

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
		return -1;
	}

	struct limine_file **modules = limine_module_request.response->modules;
	uint64_t module_count = limine_module_request.response->module_count;

	print("dufay: booting servers {%x}\n", module_count);

	for(uint64_t i = 0; i < module_count; i++) {
		if(strcmp(modules[i]->cmdline, "scheduler") == 0) {
			launch_schedulers(modules[i]);
		}
	}

	return 0;
}

static int launch_server(struct server *server, void *arg, int arg_length) {
	if(server == NULL) return -1;

	struct elf64_file *elf = alloc(sizeof(struct elf64_file));

	elf->data.buffer = server->file->address;
	elf->data.length = server->file->size;

	int ret = elf64_file_init(elf);
	if(ret == -1) return -1;

	ret = elf64_file_aux(elf, &elf->aux);
	if(ret == -1) return -1;

	struct context *context = alloc(sizeof(struct context));

	ret = create_blank_context(context); 
	if(ret == -1) return -1;

	elf->page_table = context->page_table;

	ret = elf64_file_load(elf);
	if(ret == -1) return -1;

	context->regs.rip = elf->aux.at_entry;
	context->regs.cs = 0x43;
	context->regs.rflags = 0x202;
	context->regs.ss = 0x3b;

	context->user_stack.sp = SERVER_DEFAULT_STACK_LOCATION + SERVER_DEFAULT_STACK_SIZE;
	context->user_stack.size = SERVER_DEFAULT_STACK_SIZE;

	uintptr_t stack_physical = pmm_alloc(context->user_stack.sp / PAGE_SIZE, 1) + 
		SERVER_DEFAULT_STACK_SIZE; 
	uintptr_t stack_virtual = context->user_stack.sp;

	for(size_t i = 0; i < SERVER_DEFAULT_STACK_SIZE / PAGE_SIZE; i++) {
		context->page_table->map_page(context->page_table, stack_virtual - PAGE_SIZE * i,
				stack_physical - PAGE_SIZE * i,
				X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_US);
	}

	char *location = (void*)(stack_physical + HIGH_VMA);

	if(arg) {
		location -= arg_length;
		memcpy(location, arg, arg_length);
		context->regs.rdi = stack_virtual - (stack_physical - ((uint64_t)location - HIGH_VMA));
	}

	location = (void*)((uint64_t)location & -16ll);
	context->regs.rsp = stack_virtual - (stack_physical - ((uint64_t)location - HIGH_VMA));

	server->context = context;

	return 0;
}

static int launch_schedulers(struct limine_file *file) {
	int ret = create_namespace("SCHEDULER");
	if(ret == -1) return -1;

	struct server *servers[bootable_processor_cnt];

	for(int i = 0; i < bootable_processor_cnt; i++) {
		servers[i] = alloc(sizeof(struct server));

		char *server_name = alloc(SERVER_MAX_NAME_LENGTH);
		sprint(server_name, "SCHEDULER CORE%d", i);

		ret = create_server("SCHEDULER", server_name, servers[i]);
		if(ret == -1) return -1;

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
		if(ret == -1) return -1;
		free(req);

		(struct sched_descriptor*)virtual_base;
	});

	for(int i = 0; i < bootable_processor_cnt; i++) {
		struct sched_descriptor *descriptor = descriptors + i;

		descriptor->processor_id = i;
		descriptor->queue_default_refill = 0;
		descriptor->load = 0;

		if(launch_server(servers[i], descriptor, sizeof(struct sched_descriptor)) == -1) {
			print("dufay: failed to launch server\n");
			continue;
		}

		int ret = sched_establish_shared_link(servers[i]->context, servers[i]->name);
		if(ret == -1) return -1;
	}

	return 0;
}

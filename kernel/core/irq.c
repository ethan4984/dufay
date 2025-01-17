#include <core/irq.h>
#include <core/debug.h>
#include <core/syscall.h>
#include <core/server.h>

#include <fayt/debug.h>

int instantiate_irq_cortex(const char *identifier) {
	if(identifier == NULL) RETURN_ERROR;

	struct limine_file *module = limine_search_module(identifier);
	if(module == NULL) RETURN_ERROR;

	struct server *server = alloc(sizeof(struct server));
	int ret = create_server("CORTEX", module->cmdline, server);
	if(ret == -1) RETURN_ERROR;

	server->file = module;

	return 0;
}

SYSCALL_DEFINE1(spawn_irq_cortex, const char*, identifier, {
	int ret = instantiate_irq_cortex(identifier);
	if(ret == -1) return -1;
})

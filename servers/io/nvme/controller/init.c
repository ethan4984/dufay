#include <fayt/address_space.h>
#include <fayt/syscall.h>
#include <fayt/debug.h>
#include <fayt/portal.h>
#include <fayt/stream.h>
#include <fayt/notification.h>
#include <fayt/string.h>
#include <fayt/slab.h>
#include <fayt/rb_tree.h>
#include <fayt/pci.h>
#include <fayt/irq.h>

#include <nvme.h>

#include "../common.h"

static void *spalloc(void*, uint64_t);
static void spfree(void*, uint64_t, uint64_t);

int main(struct pci_info *pci_info) {
	print("Booting nvme server\n"); // TODO launch distinct server for each discrete controller

	struct slab_pool pool = {
		.page_size = PAGE_SIZE,
		.page_alloc = spalloc,
		.page_free = spfree
	};

	slab_cache_create(&pool, "CACHE32", 32);
	slab_cache_create(&pool, "CACHE64", 64);
	slab_cache_create(&pool, "CACHE128", 128);
	slab_cache_create(&pool, "CACHE256", 256);
	slab_cache_create(&pool, "CACHE512", 512);
	slab_cache_create(&pool, "CACHE1024", 1024);
	slab_cache_create(&pool, "CACHE2048", 2048);
	slab_cache_create(&pool, "CACHE4096", 4096);
	slab_cache_create(&pool, "CACHE8192", 8192);
	slab_cache_create(&pool, "CACHE16384", 16384);

	print("Slab cache directory initialised\n");

	constexpr int NOTIFICATION_STACK_SIZE = 0x10000;
	for(int i = 0; i < 4; i++) {
		uintptr_t addr;
		int ret = as_allocate(&address_space, &addr, NOTIFICATION_STACK_SIZE);
		if(ret == -1) { print("ERROR: failed to allocate address for stack\n"); goto failure; }

		struct syscall_response response = SYSCALL2(SYSCALL_NOTIFICATION_DEFINE_STACK,
			addr + NOTIFICATION_STACK_SIZE, NOTIFICATION_STACK_SIZE);

		if(response.ret == -1) print("ERROR: failed to allocate notification stack\n");
		else print("Allocated notificaton stack #%d [%x:%x]\n",
			i, addr, NOTIFICATION_STACK_SIZE);
	}

	struct comm_bridge bridge = {
		.not = NOT_PCI_BAR, .weight = NOTIFY_WEIGHT_INSTANTANEOUS,
		.namespace = "IO", .destination = "pci"
	};

	bridge.data.limit = sizeof(struct pci_nbar);
	uintptr_t vaddr;
	int ret = as_allocate(&address_space, &vaddr,
		DIV_ROUNDUP(bridge.data.limit, PAGE_SIZE) * PAGE_SIZE);
	if(ret == -1) return -1;
	bridge.data.base = (void*)vaddr;

	struct syscall_response response = SYSCALL1(SYSCALL_NOTIFICATION_BUILD, &bridge);
	if(response.ret == -1) return -1;

	struct pci_nbar *nbar = (void*)vaddr;
	nbar->descriptor = pci_info->descriptor;
	nbar->bar_index = NVME_PCI_BAR;

	response = SYSCALL1(SYSCALL_NOTIFICATION_BROADCAST, &bridge);
	if(response.ret == -1) return -1;

	uintptr_t addr;
	ret = as_address(&address_space, &addr, nbar->bar.limit);
	if(ret == -1) RETURN_ERROR;

	struct portal_req portal_req = {
		.type = PORTAL_REQ_DIRECT,
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.length = sizeof(struct portal_req),
		.morphology = {
			.addr = addr, .length = nbar->bar.limit,
			.paddr = nbar->bar.base, .pcnt = DIV_ROUNDUP(nbar->bar.limit, PAGE_SIZE)
		}
	};

	struct portal_resp portal_resp;
	struct syscall_response syscall_response = SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
	if(syscall_response.ret == -1 || portal_resp.base != addr ||
		portal_resp.limit != nbar->bar.limit) RETURN_ERROR;

	int irq_vector;
	syscall_response = SYSCALL2(SYSCALL_ARCHCTL, ARCHCTL_RESERVE_IRQ, &irq_vector);
	if(syscall_response.ret == -1) RETURN_ERROR;

	struct pci_nmsi nmsi = {
		.descriptor = pci_info->descriptor,
		.irq_vector = irq_vector
	};

	if(pci_info->msix_capable) {
		print("Device is MSIX capable\n");
		nmsi.msix = true;
	} else if(pci_info->msi_capable) {
		print("Device is MSI capable\n");
		nmsi.msix = false;
	} else {
		print("Device is neither MSI or MSIX capable\n");
		return -1;
	}

	bridge = (struct comm_bridge) {
		.not = NOT_PCI_MSI, .weight = NOTIFY_WEIGHT_INSTANTANEOUS,
		.namespace = "IO", .destination = "pci",
		.data = { .base = &nmsi, .limit = sizeof(struct pci_nmsi) }
	};

	ret = notify(&bridge);
	if(ret == -1) return -1;

	syscall_response = SYSCALL2(SYSCALL_IRQ_CORTEX_INSTANTIATE,
		"nvme_irq", irq_vector);
	if(syscall_response.ret == -1) return -1;

	struct anchor anchor = { .identifier = NVME_IRQ_MMIO, .paddr = nbar->bar.base };
	syscall_response = SYSCALL2(SYSCALL_IRQ_CORTEX_ANCHOR, "nvme_irq", &anchor);
	if(syscall_response.ret == -1) return -1;

	ret = nvme(pci_info, (volatile struct nvme_regs*)addr,irq_vector);
	if(ret == -1) { print("ERROR: internal critical failure\n"); goto failure; }
failure:
	for(;;);
}

#include <stdarg.h>

static void log_write(struct stream_info*, char c) { SYSCALL1(SYSCALL_LOG, c); }
static struct stream_info print_stream = {
	.write = log_write
};

void print(const char *str, ...) {
	va_list arg; 
	va_start(arg, str);

	const char *prefix = "DUFAY: [NVME] ";
	for(; *prefix;) {
		print_stream.write(&print_stream, *prefix);
		prefix++;
	}

	stream_print(&print_stream, str, arg);

	va_end(arg);
}

void panic(const char *str, ...) {
	print("PANIC [ ");

	va_list arg; 
	va_start(arg, str);

	stream_print(&print_stream, str, arg);

	va_end(arg);

	print(" ]\n");

	for(;;);
}

struct address_space address_space = {
	.current = 0xa0000000,
	.base = 0xa0000000,
	.limit = 0x0000fffffffff0ff
};

static void *spalloc(void*, uint64_t s) {
	uintptr_t addr;

	int ret = as_allocate(&address_space, &addr, s * PAGE_SIZE);
	if(ret == -1) return NULL;

	return (void*)addr;
}

static void spfree(void*, uint64_t, uint64_t) { }

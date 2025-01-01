#include <fayt/debug.h>
#include <fayt/slab.h>
#include <fayt/string.h>
#include <fayt/portal.h>
#include <fayt/address_space.h>
#include <fayt/syscall.h>
#include <fayt/notification.h>
#include <fayt/pci.h>

#include <nvme.h>

struct nvme_controller {
	struct nvme_regs *regs;
	struct nvme_controller_id *id;

	struct {
		int major;
		int minor; 
		int tertiary;
	} version;

	unsigned int page_size_max;
	unsigned int page_size_min;
};

int nvme(struct pci_descriptor *pci_descriptor) {
	if(pci_descriptor == NULL) return -1;

	struct pci_notify_bar notify_bar =
		{ .descriptor = *pci_descriptor };

	struct comm_bridge bridge = {
		.not = PCI_NOTIFY_BAR,
		.weight = NOTIFY_WEIGHT_INSTANTANEOUS,
		.namespace = "IO",
		.destination = "pci",
		.data = {
			.ptr = pci_descriptor,
			.length = sizeof(struct pci_descriptor)
		}
	};

	int ret = notify_and_block(&bridge);
	if(ret == -1) RETURN_ERROR;

	uintptr_t addr;
	ret = as_address(&address_space, &addr, PAGE_SIZE);
	if(ret == -1) RETURN_ERROR;

	struct nvme_controller *controller = alloc(sizeof(struct nvme_controller));

	struct portal_req portal_req = {
		.type = PORTAL_REQ_DIRECT,
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.length = sizeof(struct portal_req),
		.morphology = {
			.addr = addr, .length = PAGE_SIZE, 
			.paddr = notify_bar.bar.base, .pcnt = DIV_ROUNDUP(notify_bar.bar.limit, PAGE_SIZE)
		}
	};

	struct portal_resp portal_resp;
	struct syscall_response syscall_response = SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
	if(syscall_response.ret == -1 || portal_resp.base != addr ||
		portal_resp.limit != PAGE_SIZE) RETURN_ERROR;

	struct nvme_regs *regs = (void*)addr;

	controller->version.major = (regs->vs >> 16) & 0xffff;
	controller->version.minor = (regs->vs >> 8) & 0xff;
	controller->version.tertiary = (regs->vs >> 0) & 0xff;

	print("dufay: nvme: version detected %d:%d:%d\n", controller->version.major,
		controller->version.minor, controller->version.tertiary);

	return 0;
}

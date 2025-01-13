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
	volatile struct nvme_regs *regs;
	volatile struct nvme_controller_id *id;

	struct {
		int major;
		int minor; 
		int tertiary;
	} version;

	int queue_entries;
	int page_size_max;
	int page_size_min;
	int page_size;
	int max_transfer_shift;
	int max_prps;
	int strides;
};

int nvme(struct pci_info *pci_info, volatile struct nvme_regs *regs) {
	if(pci_info == NULL || regs == NULL) return -1;

	struct nvme_controller *controller = alloc(sizeof(struct nvme_controller));

	controller->regs = regs;
	controller->version.major = (controller->regs->vs >> 16) & 0xffff;
	controller->version.minor = (controller->regs->vs >> 8) & 0xff;
	controller->version.tertiary = (controller->regs->vs >> 0) & 0xff;

	print("DUFAY: NVME: version detected %d:%d:%d\n", controller->version.major,
		controller->version.minor, controller->version.tertiary);

	controller->page_size_max = 1 << (12 + (controller->regs->cap >> 52 & 0xf));
	controller->page_size_min = 1 << (12 + (controller->regs->cap >> 48 & 0xf));

	if(controller->regs->cc & (1 << 0)) controller->regs->cc &= ~(1 << 0);
	for(; controller->regs->cc & (1 << 0););

	if(pci_info->msix_capable) {
		print("DUFAY: NVME: device is MSIX capable\n");
	} else if(pci_info->msi_capable) {
		print("DUFAY: NVME: device is MSI capable\n");
	} else {
		print("DUFAY: NVME: device is neither MSI or MSIX capable\n");
		return -1;
	}

	controller->queue_entries = controller->regs->cap & 0xffff;
	controller->strides = (controller->regs->cap >> 32) & 0xf;

	return 0;
}

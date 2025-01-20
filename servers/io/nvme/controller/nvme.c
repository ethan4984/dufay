#include <fayt/debug.h>
#include <fayt/slab.h>
#include <fayt/string.h>
#include <fayt/portal.h>
#include <fayt/address_space.h>
#include <fayt/syscall.h>
#include <fayt/notification.h>
#include <fayt/pci.h>
#include <fayt/bitmap.h>

#include <nvme.h>

struct nvme_controller;

struct nvme_queue_pair {
	int qid;
	int entry_cnt;
	int sq_head;
	int sq_tail;
	int cq_head;
	int cq_tail;
	bool phase;
	int vector;
	int irq;
	bool admin;

	struct nvme_controller *controller;

	volatile struct nvme_command *submission_queue;
	volatile struct nvme_completion *completion_queue;
	volatile uint32_t *submission_doorbell;
	volatile uint32_t *completion_doorbell;

	struct bitmap cid_bitmap;
};

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

	struct bitmap qid_bitmap;

	struct nvme_queue_pair *admin_queue;
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

	print("DUFAY: NVME: controller reset\n");

	struct pci_nmsi nmsi = {
		.descriptor = pci_info->descriptor,
		.irq_vector = pci_info->irq_vector
	};

	if(pci_info->msix_capable) {
		print("DUFAY: NVME: device is MSIX capable\n");
		nmsi.msix = true;
	} else if(pci_info->msi_capable) {
		print("DUFAY: NVME: device is MSI capable\n");
		nmsi.msix = false;
	} else {
		print("DUFAY: NVME: device is neither MSI or MSIX capable\n");
		return -1;
	}

	struct comm_bridge bridge = {
		.not = NOT_PCI_MSI, .weight = NOTIFY_WEIGHT_INSTANTANEOUS,
		.namespace = "IO", .destination = "pci",
		.data = { .base = &nmsi, .limit = sizeof(struct pci_nmsi) }
	};

	int ret = notify(&bridge);
	if(ret == -1) return -1;

	struct syscall_response syscall_response = SYSCALL2(SYSCALL_IRQ_CORTEX_INSTANTIATE,
		"nvme_irq", pci_info->irq_vector);
	if(syscall_response.ret == -1) return -1;

	controller->queue_entries = controller->regs->cap & 0xffff;
	controller->strides = (controller->regs->cap >> 32) & 0xf;

	controller->admin_queue = alloc(sizeof(struct nvme_queue_pair));
	if(controller->admin_queue == NULL) RETURN_ERROR;

	controller->qid_bitmap = (struct bitmap) {
		.data = alloc(NVME_QID_MAX / 8),
		.size = NVME_QID_MAX,
		.resizable = false
	};

	ret = bitmap_alloc(&controller->qid_bitmap, &controller->admin_queue->qid);
	if(ret == -1 || controller->admin_queue->qid) RETURN_ERROR;
	controller->admin_queue->controller = controller;
	controller->admin_queue->entry_cnt = controller->queue_entries;

	uintptr_t address;
	ret = as_address(&address_space, &address, controller->admin_queue->entry_cnt * sizeof(struct nvme_command));
	if(ret == -1) RETURN_ERROR;

	struct portal_resp portal_resp;
	struct portal_req portal_req = {
		.type = PORTAL_REQ_ANON | PORTAL_REQ_CONTINUOUS | PORTAL_REQ_PEEK,
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.morphology = {
			.addr = address,
			.length = ALIGN_UP(controller->admin_queue->entry_cnt * sizeof(struct nvme_command), PAGE_SIZE),
			.pcnt = DIV_ROUNDUP(controller->admin_queue->entry_cnt * sizeof(struct nvme_command), PAGE_SIZE)
		}
	};

	syscall_response = SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
	if(syscall_response.ret == -1 || portal_resp.base != address || portal_resp.limit !=
		ALIGN_UP(controller->admin_queue->entry_cnt * sizeof(struct nvme_command), PAGE_SIZE)) RETURN_ERROR;
	
	controller->regs->asq = portal_resp.morphology.paddr;

	ret = as_address(&address_space, &address, controller->admin_queue->entry_cnt * sizeof(struct nvme_command));
	if(ret == -1) RETURN_ERROR;

	portal_resp = (struct portal_resp) { 0 };
	portal_req.morphology.addr = address;

	syscall_response = SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
	if(syscall_response.ret == -1 || portal_resp.base != address || portal_resp.limit !=
		ALIGN_UP(controller->admin_queue->entry_cnt * sizeof(struct nvme_command), PAGE_SIZE)) RETURN_ERROR;
	
	controller->regs->aqa = (controller->admin_queue->entry_cnt - 1) << 16 |
		(controller->admin_queue->entry_cnt - 1);
	controller->regs->acq = portal_resp.morphology.paddr;

	controller->regs->cc = (1 << 0) | (0 << 4) | (0 << 11) | (0 << 14) | (6 << 16) | (4 << 20);
	for(;;) {
		if(controller->regs->cc & (1 << 0)) break;
		else if(controller->regs->csts & (1 << 1)) RETURN_ERROR;
	}

	print("DUFAY: NVME: controller enabled\n");

	return 0;
}

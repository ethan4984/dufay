#include <fayt/debug.h>
#include <fayt/slab.h>
#include <fayt/string.h>
#include <fayt/portal.h>
#include <fayt/address_space.h>
#include <fayt/syscall.h>
#include <fayt/notification.h>
#include <fayt/pci.h>
#include <fayt/bitmap.h>
#include <fayt/irq.h>

#include <nvme.h>

#include "../common.h"

#define DUFAY_ALLOCATE_UNBROKEN(SIZE, RESP) ({ \
	__label__ finish; \
	int ret = 0; \
	uintptr_t address; \
	ret = as_address(&address_space, &address, (SIZE)); \
	if(ret == -1) { ret = -1; goto finish; } \
	struct portal_req portal_req = { \
		.type = PORTAL_REQ_ANON | PORTAL_REQ_CONTINUOUS | PORTAL_REQ_PEEK, \
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE, \
		.morphology = { \
			.addr = address, \
			.length = ALIGN_UP((SIZE), PAGE_SIZE), \
			.pcnt = DIV_ROUNDUP((SIZE), PAGE_SIZE) \
		} \
	}; \
	struct syscall_response syscall_response = SYSCALL2(SYSCALL_PORTAL, &portal_req, (RESP)); \
	if(syscall_response.ret == -1 || (RESP)->base != address || (RESP)->limit != \
		ALIGN_UP((SIZE), PAGE_SIZE)) { ret = -1; goto finish; } \
finish: \
	ret; \
})

static int nvme_send_command(struct nvme_queue_pair *queue_pair, struct nvme_command *submission) {
	if(queue_pair == NULL || submission == NULL) RETURN_ERROR;

	int cid, ret = bitmap_alloc(&queue_pair->cid_bitmap, &cid);
	if(ret == -1) RETURN_ERROR;

	submission->cid = cid;

	queue_pair->submission_queue[queue_pair->sq_tail++] = *submission;
	if(queue_pair->sq_tail >= queue_pair->entry_cnt) queue_pair->sq_tail = 0;
	*queue_pair->submission_doorbell = queue_pair->sq_tail;

	return 0;
}

static int nvme_send_command_and_block(struct nvme_queue_pair *queue_pair, struct nvme_command *submission) {
	if(queue_pair == NULL || submission == NULL) RETURN_ERROR;

	int ret = nvme_send_command(queue_pair, submission);
	if(ret == -1) RETURN_ERROR;

	return 0;
}

static int nvme_fetch_controller_id(struct nvme_controller *controller) {
	if(controller == NULL) RETURN_ERROR;

	struct portal_resp portal_resp;
	int ret = DUFAY_ALLOCATE_UNBROKEN(sizeof(struct nvme_controller_id), &portal_resp);
	if(ret == -1) RETURN_ERROR;

	struct nvme_command identify_command = {
		.opcode = nvme_op_identify,
		.private.identify.cns = 1,
		.private.identify.prp1 = portal_resp.morphology.paddr
	};

	ret = nvme_send_command_and_block(controller->admin_queue, &identify_command);
	if(ret == -1) RETURN_ERROR;

	return 0;
}

static int nvme_queue_pair_instantiate(struct nvme_controller *controller,
	struct nvme_queue_pair **queue, int admin, int irq) {
	if(controller == NULL) RETURN_ERROR;

	controller->nvme_queue_pair_cnt++;
	if(sizeof(struct nvme_controller) + sizeof(struct nvme_queue_pair) *
		controller->nvme_queue_pair_cnt > PAGE_SIZE) RETURN_ERROR;
	struct nvme_queue_pair *queue_pair = &controller->nvme_queue_pair[controller->nvme_queue_pair_cnt - 1];
	if(admin) controller->admin_queue = queue_pair;

	int ret = bitmap_alloc(&controller->qid_bitmap, &queue_pair->qid);
	if(ret == -1 || queue_pair->qid) RETURN_ERROR;

	queue_pair->controller = controller;
	queue_pair->entry_cnt = controller->queue_entries;
	queue_pair->cid_bitmap = (struct bitmap) {
		.data = alloc(DIV_ROUNDUP(queue_pair->entry_cnt, 8)),
		.size = queue_pair->entry_cnt,
		.resizable = false
	};
	queue_pair->submission_doorbell_offset = PAGE_SIZE + (2 * 0) * (4 << controller->strides);
	queue_pair->submission_doorbell = (volatile uint32_t*)((void*)controller->regs +
		PAGE_SIZE + (2 * 0) * (4 << controller->strides));
	queue_pair->completion_doorbell_offset = PAGE_SIZE + (2 * 0 + 1) * (4 << controller->strides);
	queue_pair->completion_doorbell = (volatile uint32_t*)((void*)controller->regs +
		PAGE_SIZE + (2 * 0 + 1) * (4 << controller->strides));
	queue_pair->irq = irq;

	struct portal_resp portal_resp;
	ret = DUFAY_ALLOCATE_UNBROKEN(queue_pair->entry_cnt * sizeof(struct nvme_command), &portal_resp);
	if(ret == -1) RETURN_ERROR;

	if(admin) controller->regs->asq = portal_resp.morphology.paddr;
	queue_pair->completion_queue_paddr = portal_resp.morphology.paddr;
	queue_pair->submission_queue = (void*)portal_resp.base;

	ret = DUFAY_ALLOCATE_UNBROKEN(queue_pair->entry_cnt * sizeof(struct nvme_command), &portal_resp);
	if(ret == -1) RETURN_ERROR;

	queue_pair->completion_queue_paddr = portal_resp.morphology.paddr;
	queue_pair->completion_queue = (void*)portal_resp.base;
	if(admin) {
		controller->regs->acq = portal_resp.morphology.paddr;
		controller->regs->aqa = (queue_pair->entry_cnt - 1) << 16 |
			(queue_pair->entry_cnt - 1);
		goto finish;
	}
finish:
	if(queue) *queue = queue_pair;
	return 0;
}

int nvme(struct pci_info *pci_info, volatile struct nvme_regs *regs) {
	if(pci_info == NULL || regs == NULL) return -1;
	
	struct portal_resp portal_resp;
	int ret = DUFAY_ALLOCATE_UNBROKEN(sizeof(struct nvme_controller), &portal_resp);
	if(ret == -1) RETURN_ERROR;

	struct anchor anchor = (struct anchor) {
		.identifier = NVME_IRQ_CONTROLLER, .paddr = portal_resp.morphology.paddr
	};
	struct syscall_response syscall_response = SYSCALL2(SYSCALL_IRQ_CORTEX_ANCHOR, "nvme_irq", &anchor);
	if(syscall_response.ret == -1) return -1;

	struct nvme_controller *controller = (void*)portal_resp.base;

	controller->regs = regs;
	controller->version.major = (controller->regs->vs >> 16) & 0xffff;
	controller->version.minor = (controller->regs->vs >> 8) & 0xff;
	controller->version.tertiary = (controller->regs->vs >> 0) & 0xff;

	print("Version detected %d:%d:%d\n", controller->version.major,
		controller->version.minor, controller->version.tertiary);

	controller->page_size_max = 1 << (12 + (controller->regs->cap >> 52 & 0xf));
	controller->page_size_min = 1 << (12 + (controller->regs->cap >> 48 & 0xf));

	if(controller->regs->cc & (1 << 0)) controller->regs->cc &= ~(1 << 0);
	for(; controller->regs->cc & (1 << 0););

	print("Controller reset\n");

	struct pci_nmsi nmsi = {
		.descriptor = pci_info->descriptor,
		.irq_vector = pci_info->irq_vector
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

	struct comm_bridge bridge = {
		.not = NOT_PCI_MSI, .weight = NOTIFY_WEIGHT_INSTANTANEOUS,
		.namespace = "IO", .destination = "pci",
		.data = { .base = &nmsi, .limit = sizeof(struct pci_nmsi) }
	};

	ret = notify(&bridge);
	if(ret == -1) return -1;

	anchor = (struct anchor) { .identifier = NVME_IRQ_CONTROLLER, .paddr = 0 };
	syscall_response = SYSCALL2(SYSCALL_IRQ_CORTEX_ANCHOR, "nvme_irq", &anchor);
	if(syscall_response.ret == -1) return -1;

	controller->queue_entries = controller->regs->cap & 0xffff;
	controller->strides = (controller->regs->cap >> 32) & 0xf;
	controller->qid_bitmap = (struct bitmap) {
		.data = alloc(NVME_QID_MAX / 8),
		.size = NVME_QID_MAX,
		.resizable = false
	};

	ret = nvme_queue_pair_instantiate(controller, NULL, 1, pci_info->irq_vector);
	if(ret == -1) RETURN_ERROR;

	controller->regs->cc = (1 << 0) | (0 << 4) | (0 << 11) | (0 << 14) | (6 << 16) | (4 << 20);
	for(;;) {
		if(controller->regs->cc & (1 << 0)) break;
		else if(controller->regs->csts & (1 << 1)) RETURN_ERROR;
	}

	print("Controller enabled\n");

	ret = nvme_fetch_controller_id(controller);
	if(ret == -1) RETURN_ERROR;

	return 0;
}

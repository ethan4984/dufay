#include <fayt/debug.h>
#include <fayt/slab.h>
#include <fayt/string.h>
#include <fayt/portal.h>
#include <fayt/address.h>
#include <fayt/syscall.h>
#include <fayt/notification.h>
#include <fayt/pci.h>
#include <fayt/bitmap.h>
#include <fayt/irq.h>
#include <fayt/compiler.h>

#include <nvme.h>

#include "../common.h"

static int nvme_nread(struct notification_info *, void *private, int)
{
	struct nblkread *nblkread = private;
	if (unlikely(nblkread == NULL))
		RETURN_ERROR;
	return 0;

	/*	struct nvme_namespace *namespace;
	if(nblkread->location % namespace->blksize ||
		nblkread->count % namespace->blksize) RETURN_ERROR;

	int blk_start = nblkread->location / namespace->blksize;
	int blk_cnt = namespace->count / namespace->blksize;

	int ret = nvme_lba_rw(namespace, blk_start, blk_cnt, false, private);
	if(ret == -1) RETURN_ERROR;

	return ret;*/
}

static int nvme_nwrite(struct notification_info *, void *private, int)
{
	struct nblkread *nblkwrite = private;
	if (unlikely(nblkwrite == NULL))
		RETURN_ERROR;
	return 0;
	/*	struct nvme_namespace *namespace;
	if(nblkwrite->location % namespace->blksize ||
		nblkwrite->count % namespace->blksize) RETURN_ERROR;

	int blk_start = nblkwrite->location / namespace->blksize;
	int blk_cnt = namespace->count / namespace->blksize;

	int ret = nvme_lba_rw(namespace, blk_start, blk_cnt, false, private);
	if(ret == -1) RETURN_ERROR;

	return ret;*/
}

static int nvme_nioctl(struct notification_info *, void *private, int)
{
	struct nblkioctl *nblkioctl = private;
	if (unlikely(nblkioctl == NULL))
		RETURN_ERROR;
	return 0;
}

static int nvme_send_command(struct nvme_queue_pair *queue_pair,
							 struct nvme_command *submission, int cid,
							 int blocking)
{
	if (queue_pair == NULL || submission == NULL)
		RETURN_ERROR;

	struct nvme_queue_entry *queue_entry = &queue_pair->queue_entry[cid];
	*queue_entry = (struct nvme_queue_entry){ .blocking = blocking };

	if (cid > queue_pair->queue_entry_cnt)
		RETURN_ERROR;
	submission->cid = cid;

	queue_pair->submission_queue[queue_pair->sq_tail++] = *submission;
	if (queue_pair->sq_tail >= queue_pair->entry_cnt)
		queue_pair->sq_tail = 0;
	*queue_pair->submission_doorbell = queue_pair->sq_tail;

	return 0;
}

static int nvme_send_command_and_poll(struct nvme_queue_pair *queue_pair,
									  struct nvme_command *submission, int cid)
{
	if (queue_pair == NULL || submission == NULL)
		RETURN_ERROR;

	int ret = nvme_send_command(queue_pair, submission, cid, false);
	if (ret == -1)
		RETURN_ERROR;

	struct nvme_queue_entry *queue_entry =
		&queue_pair->queue_entry[submission->cid];
	for (; !queue_entry->response;)
		;

	if (queue_entry->completion.status >> 1) {
		print("command error: status [%x]\n", queue_entry->completion.status);
		return -1;
	}

	ret = bitmap_free(&queue_pair->cid_bitmap, queue_entry->cid);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

static int nvme_send_command_and_block(struct nvme_queue_pair *queue_pair,
									   struct nvme_command *submission, int cid)
{
	if (queue_pair == NULL || submission == NULL)
		RETURN_ERROR;

	int ret = nvme_send_command(queue_pair, submission, cid, true);
	if (ret == -1)
		RETURN_ERROR;

	struct nvme_queue_entry *queue_entry = &queue_pair->queue_entry[cid];
	struct syscall_response response =
		SYSCALL4(SYSCALL_FUTEX, &queue_entry->response, FUTEX_WAIT, true, true);
	if (response.ret == -1)
		RETURN_ERROR;

	if (queue_entry->completion.status >> 1) {
		print("command error: status [%x]\n", queue_entry->completion.status);
		return -1;
	}

	ret = bitmap_free(&queue_pair->cid_bitmap, queue_entry->cid);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

static int nvme_fetch_controller_id(struct nvme_controller *controller)
{
	if (controller == NULL)
		RETURN_ERROR;

	struct portal_resp portal_resp;
	int ret = DUFAY_ALLOCATE_UNBROKEN(sizeof(struct nvme_controller_id),
									  &portal_resp);
	if (ret == -1)
		RETURN_ERROR;

	struct nvme_command identify_command = { .opcode = nvme_op_identify,
											 .private.identify.cns = 1,
											 .private.identify.prp1 =
												 portal_resp.morphology.paddr };

	int cid;
	ret = bitmap_alloc(&controller->admin_queue->cid_bitmap, &cid);
	if (ret == -1)
		RETURN_ERROR;

	ret = nvme_send_command_and_block(controller->admin_queue,
									  &identify_command, cid);
	if (ret == -1)
		RETURN_ERROR;

	controller->controller_id = (void *)portal_resp.base;

	return 0;
}

static int nvme_fetch_namespaces(struct nvme_controller *controller)
{
	if (controller == NULL)
		RETURN_ERROR;

	struct portal_resp portal_resp;
	int ret = DUFAY_ALLOCATE_UNBROKEN(
		controller->controller_id->nn * sizeof(int), &portal_resp);
	if (ret == -1)
		RETURN_ERROR;

	struct nvme_command identify_command = { .opcode = nvme_op_identify,
											 .private.identify.cns = 2,
											 .private.identify.prp1 =
												 portal_resp.morphology.paddr };

	int cid;
	ret = bitmap_alloc(&controller->admin_queue->cid_bitmap, &cid);
	if (ret == -1)
		RETURN_ERROR;

	ret = nvme_send_command_and_block(controller->admin_queue,
									  &identify_command, cid);
	if (ret == -1)
		RETURN_ERROR;

	int *nsid = (void *)portal_resp.base;
	for (size_t i = 0; i < controller->controller_id->nn; i++) {
		if (!nsid[i])
			continue;

		struct portal_resp portal_resp;
		int ret = DUFAY_ALLOCATE_UNBROKEN(sizeof(struct nvme_namespace_id),
										  &portal_resp);
		if (ret == -1)
			RETURN_ERROR;

		struct nvme_command identify_command = {
			.opcode = nvme_op_identify,
			.private.identify.cns = 0,
			.private.identify.nsid = nsid[i],
			.private.identify.prp1 = portal_resp.morphology.paddr
		};

		int cid;
		ret = bitmap_alloc(&controller->admin_queue->cid_bitmap, &cid);
		if (ret == -1)
			RETURN_ERROR;

		ret = nvme_send_command_and_block(controller->admin_queue,
										  &identify_command, cid);
		if (ret == -1)
			RETURN_ERROR;

		struct nvme_namespace *namespace =
			alloc(sizeof(struct nvme_namespace_id));
		VECTOR_PUSH(controller->namespace, namespace);

		memcpy(&namespace->identity, (void *)portal_resp.base,
			   sizeof(struct nvme_namespace_id));

		namespace->nsid = nsid[i];
		namespace->max_prp = ({
			int lba_shift = namespace->identity
								.lbaf_list[namespace->identity.flbas & 0b1111]
								.ds;
			int shift = 12 + ((controller->regs->cap >> 48) & 0b1111);
			int max_transfer_shift =
				controller->controller_id->mdts ?
					shift + controller->controller_id->mdts :
					20;
			int max_lbas = 1 << (max_transfer_shift - lba_shift);
			(max_lbas * (1 << lba_shift)) / 0x1000;
		});
		namespace->lba_cnt = namespace->identity.nsze;
		namespace->lba_size =
			1 << (namespace->identity
					  .lbaf_list[namespace->identity.flbas & 0b11111]
					  .ds);
	}

	return 0;
}

int nvme_lba_rw(struct nvme_namespace *namespace, int base, int cnt, int rw,
				void *buffer)
{
	if (namespace == NULL || buffer == NULL)
		RETURN_ERROR;

	int cid, ret = bitmap_alloc(&namespace->queue_pair->cid_bitmap, &cid);
	if (ret == -1)
		RETURN_ERROR;

	struct nvme_command command = {};

	if ((unsigned)cnt * namespace->lba_size > PAGE_SIZE) {
		if ((unsigned)cnt * namespace->lba_size > PAGE_SIZE * 2) {
			int prp_cnt = (cnt - 1) * namespace->lba_size / PAGE_SIZE;
			if (prp_cnt > namespace->max_prp)
				RETURN_ERROR;

			for (int i = 0; i < prp_cnt; i++) {
				namespace->prp_list[i + cid * namespace->max_prp] =
					(uint64_t)buffer + PAGE_SIZE + i * PAGE_SIZE;
			}

			command.private.rw.prp2 =
				namespace->prp_list[cid * namespace->max_prp];
		} else {
			command.private.rw.prp2 = (uint64_t)buffer + PAGE_SIZE;
		}
	}

	if (rw)
		command.opcode = 0x1;
	else
		command.opcode = 0x2;

	command.private.rw.nsid = namespace->nsid;
	command.private.rw.slba = base;
	command.private.rw.length = cnt - 1;
	command.private.rw.prp1 = (uint64_t)buffer;

	ret = nvme_send_command_and_block(namespace->queue_pair, &command, cid);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

static int nvme_queue_pair_instantiate(struct nvme_controller *controller,
									   struct nvme_queue_pair **queue,
									   int admin, int irq, const char *name)
{
	if (controller == NULL || name == NULL ||
		strlen(name) >= NVME_QUEUE_NAME_LENGTH)
		RETURN_ERROR;

	controller->nvme_queue_pair_cnt++;
	if (sizeof(struct nvme_controller) +
			sizeof(struct nvme_queue_pair) * controller->nvme_queue_pair_cnt >
		PAGE_SIZE)
		RETURN_ERROR;
	struct nvme_queue_pair *queue_pair =
		&controller->nvme_queue_pair[controller->nvme_queue_pair_cnt - 1];
	if (admin)
		controller->admin_queue = queue_pair;

	int ret = bitmap_alloc(&controller->qid_bitmap, &queue_pair->qid);
	if (ret == -1 || (admin && queue_pair->qid))
		RETURN_ERROR;

	queue_pair->controller = controller;
	queue_pair->entry_cnt = controller->queue_entries;
	queue_pair->cid_bitmap =
		(struct bitmap){ .data = alloc(DIV_ROUNDUP(queue_pair->entry_cnt, 8)),
						 .size = queue_pair->entry_cnt,
						 .resizable = false };
	queue_pair->submission_doorbell_offset =
		PAGE_SIZE + (2 * 0) * (4 << controller->strides);
	queue_pair->submission_doorbell =
		(volatile uint32_t *)((void *)controller->regs + PAGE_SIZE +
							  (2 * 0) * (4 << controller->strides));
	queue_pair->completion_doorbell_offset =
		PAGE_SIZE + (2 * 0 + 1) * (4 << controller->strides);
	queue_pair->completion_doorbell =
		(volatile uint32_t *)((void *)controller->regs + PAGE_SIZE +
							  (2 * 0 + 1) * (4 << controller->strides));
	queue_pair->irq = irq;
	strcpy(queue_pair->name, name);

	struct portal_resp portal_resp;
	ret = DUFAY_ALLOCATE_UNBROKEN(
		queue_pair->entry_cnt * sizeof(struct nvme_command), &portal_resp);
	if (ret == -1)
		RETURN_ERROR;

	if (admin)
		controller->regs->asq = portal_resp.morphology.paddr;
	queue_pair->completion_queue_paddr = portal_resp.morphology.paddr;
	queue_pair->submission_queue = (void *)portal_resp.base;

	queue_pair->queue_entry_cnt = 128;
	ret = DUFAY_ALLOCATE_UNBROKEN(sizeof(struct nvme_queue_entry) *
									  queue_pair->queue_entry_cnt,
								  &portal_resp);
	if (ret == -1)
		RETURN_ERROR;

	queue_pair->queue_entry_paddr = portal_resp.morphology.paddr;
	queue_pair->queue_entry = (void *)portal_resp.base;

	ret = DUFAY_ALLOCATE_UNBROKEN(
		queue_pair->entry_cnt * sizeof(struct nvme_command), &portal_resp);
	if (ret == -1)
		RETURN_ERROR;

	queue_pair->completion_queue_paddr = portal_resp.morphology.paddr;
	queue_pair->completion_queue = (void *)portal_resp.base;
	if (admin) {
		controller->regs->acq = portal_resp.morphology.paddr;
		controller->regs->aqa = (queue_pair->entry_cnt - 1) << 16 |
								(queue_pair->entry_cnt - 1);
		goto finish;
	}

	struct nvme_command create_cq_command = {
		.opcode = nvme_op_create_cq,
		.private.create_cq.prp1 = queue_pair->completion_queue_paddr,
		.private.create_cq.cqid = queue_pair->qid,
		.private.create_cq.qsize = queue_pair->entry_cnt - 1,
		.private.create_cq.irq_vector = 0,
		.private.create_cq.cq_flags = (1 << 0) | (1 << 1)
	};

	int cid;
	ret = bitmap_alloc(&queue_pair->cid_bitmap, &cid);
	if (ret == -1)
		RETURN_ERROR;

	ret = nvme_send_command_and_block(controller->admin_queue,
									  &create_cq_command, cid);
	if (ret == -1)
		RETURN_ERROR;

	struct nvme_command create_sq_command = {
		.opcode = nvme_op_create_sq,
		.private.create_sq.prp1 = queue_pair->submission_queue_paddr,
		.private.create_sq.cqid = queue_pair->qid,
		.private.create_sq.sqid = queue_pair->qid,
		.private.create_sq.qsize = queue_pair->entry_cnt - 1,
		.private.create_sq.sq_flags = (1 << 0) | (1 << 1)
	};

	ret = bitmap_alloc(&queue_pair->cid_bitmap, &cid);
	if (ret == -1)
		RETURN_ERROR;

	ret = nvme_send_command_and_block(controller->admin_queue,
									  &create_sq_command, cid);
	if (ret == -1)
		RETURN_ERROR;
	print("io queue-pair created [%x]\n", queue_pair->qid);
finish:
	if (queue)
		*queue = queue_pair;
	return 0;
}

static struct pci_nbar *nbar;
static struct portal_resp controller_buffer;

static int nvme_initialise_irq(struct pci_info *pci_info, int *irq,
							   const char *name)
{
	if (pci_info == NULL || irq == NULL)
		RETURN_ERROR;

	struct syscall_response syscall_response =
		SYSCALL2(SYSCALL_ARCHCTL, ARCHCTL_RESERVE_IRQ, irq);
	if (syscall_response.ret == -1)
		RETURN_ERROR;

	struct pci_nmsi nmsi = { .descriptor = pci_info->descriptor,
							 .irq_vector = *irq };

	if (pci_info->msix_capable)
		nmsi.msix = true;
	else if (pci_info->msi_capable)
		nmsi.msix = false;
	else {
		print("Device is neither MSI or MSIX capable\n");
		return -1;
	}

	struct comm_bridge bridge = {
		.not= NOT_PCI_MSI,
		.weight = NOTIFY_WEIGHT_INSTANTANEOUS,
		.data = { .base = &nmsi, .limit = sizeof(struct pci_nmsi) }
	};

	int ret = notify(&bridge);
	if (ret == -1)
		return -1;

	syscall_response =
		SYSCALL3(SYSCALL_IRQ_CORTEX_INSTANTIATE, "nvme_irq", name, *irq);
	if (syscall_response.ret == -1)
		return -1;

	struct anchor anchor = { .identifier = NVME_IRQ_MMIO,
							 .paddr = nbar->bar.base };
	syscall_response = SYSCALL2(SYSCALL_IRQ_CORTEX_ANCHOR, name, &anchor);
	if (syscall_response.ret == -1)
		return -1;

	anchor = (struct anchor){ .identifier = NVME_IRQ_CONTROLLER,
							  .paddr = controller_buffer.morphology.paddr };
	syscall_response = SYSCALL2(SYSCALL_IRQ_CORTEX_ANCHOR, name, &anchor);
	if (syscall_response.ret == -1)
		return -1;

	return 0;
}

int nvme(struct pci_info *pci_info)
{
	nbar = ({
		struct pci_nbar nbar = { .descriptor = pci_info->descriptor,
								 .bar_index = NVME_PCI_BAR };

		struct comm_bridge bridge = { .not= NOT_PCI_BAR,
									  .weight = NOTIFY_WEIGHT_INSTANTANEOUS,
									  .data = { .base = &nbar,
												.limit = sizeof(nbar) } };

		int ret = notify(&bridge);
		if (ret == -1)
			RETURN_ERROR;

		(void *)bridge.data.base;
	});

	volatile struct nvme_regs *regs = ({
		uintptr_t addr;
		int ret = as_vmem_allocate(HANDLE_AS, &addr, nbar->bar.limit);
		if (ret == -1)
			RETURN_ERROR;

		struct portal_req portal_req = {
			.type = PORTAL_REQ_DIRECT,
			.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
			.length = sizeof(struct portal_req),
			.morphology = { .addr = addr,
							.length = nbar->bar.limit,
							.paddr = nbar->bar.base,
							.pcnt = DIV_ROUNDUP(nbar->bar.limit, PAGE_SIZE) }
		};

		struct portal_resp portal_resp;
		struct syscall_response syscall_response =
			SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
		if (syscall_response.ret == -1 || portal_resp.base != addr ||
			portal_resp.limit != nbar->bar.limit)
			RETURN_ERROR;

		(void *)addr;
	});

	int ret = DUFAY_ALLOCATE_UNBROKEN(sizeof(struct nvme_controller),
									  &controller_buffer);
	if (ret == -1)
		RETURN_ERROR;

	struct nvme_controller *controller = (void *)controller_buffer.base;

	int admin_irq;
	ret = nvme_initialise_irq(pci_info, &admin_irq, "nvme_admin");
	if (ret == -1)
		RETURN_ERROR;

	controller->regs = regs;
	controller->version.major = (controller->regs->vs >> 16) & 0xffff;
	controller->version.minor = (controller->regs->vs >> 8) & 0xff;
	controller->version.tertiary = (controller->regs->vs >> 0) & 0xff;

	print("Version detected %d:%d:%d\n", controller->version.major,
		  controller->version.minor, controller->version.tertiary);

	controller->page_size_max = 1 << (12 + (controller->regs->cap >> 52 & 0xf));
	controller->page_size_min = 1 << (12 + (controller->regs->cap >> 48 & 0xf));

	if (controller->regs->cc & (1 << 0))
		controller->regs->cc &= ~(1 << 0);
	for (; controller->regs->cc & (1 << 0);)
		;

	print("Controller reset\n");

	controller->queue_entries = controller->regs->cap & 0xffff;
	controller->strides = (controller->regs->cap >> 32) & 0xf;
	controller->qid_bitmap = (struct bitmap){ .data = alloc(NVME_QID_MAX / 8),
											  .size = NVME_QID_MAX,
											  .resizable = false };

	ret = nvme_queue_pair_instantiate(controller, NULL, 1, admin_irq,
									  "nvme_admin");
	if (ret == -1)
		RETURN_ERROR;

	controller->regs->cc = (1 << 0) | (0 << 4) | (0 << 11) | (0 << 14) |
						   (6 << 16) | (4 << 20);
	for (;;) {
		if (controller->regs->cc & (1 << 0))
			break;
		else if (controller->regs->csts & (1 << 1))
			RETURN_ERROR;
	}

	print("Controller enabled\n");

	ret = nvme_fetch_controller_id(controller);
	if (ret == -1)
		RETURN_ERROR;

	print("Vendor ID: [%x]\n", controller->controller_id->vid);
	print("Subsystem vendor ID: [%x]\n", controller->controller_id->ssvid);

	ret = nvme_fetch_namespaces(controller);
	if (ret == -1)
		RETURN_ERROR;

	for (size_t i = 0; i < controller->namespace.length; i++) {
		struct nvme_namespace *namespace = controller->namespace.data[i];
		if (namespace == NULL)
			continue;

		print("nsid: [%x]\n", namespace->nsid);
		print("\tlba cnt: [%x]\n", namespace->lba_cnt);
		print("\tlba size: [%x]\n", namespace->lba_size);
		print("\tmax prps: [%x]\n", namespace->max_prp);

		char name[NVME_QUEUE_NAME_LENGTH] = {};
		sprint(name, "nvme_io%d", i);

		int irq;
		ret = nvme_initialise_irq(pci_info, &irq, name);
		if (ret == -1)
			RETURN_ERROR;

		struct nvme_queue_pair *queue_pair = NULL;
		int ret = nvme_queue_pair_instantiate(controller, &queue_pair, false,
											  irq, name);
		if (ret == -1 || queue_pair == NULL)
			RETURN_ERROR;

		namespace->queue_pair = queue_pair;

		ret = DUFAY_ALLOCATE_UNBROKEN(namespace->max_prp *
										  namespace->queue_pair->entry_cnt *
										  sizeof(uint64_t),
									  &namespace->portal_resp_prp);
		if (ret == -1)
			RETURN_ERROR;
		namespace->prp_list = (uint64_t *)namespace->portal_resp_prp.base;
	}

	return 0;
}

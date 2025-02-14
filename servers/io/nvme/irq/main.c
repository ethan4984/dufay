#include <fayt/syscall.h>
#include <fayt/debug.h>
#include <fayt/irq.h>
#include <fayt/address_space.h>
#include <fayt/sched.h>
#include <fayt/syscall.h>

#include <nvme.h>

#include "../common.h"

static volatile struct nvme_regs *nvme_regs;
static struct nvme_controller *nvme_controller;

static int nvme_anchor_flush(struct anchor *root) {
	if(root == NULL) RETURN_ERROR;

	for(; root;) {
		switch(root->identifier) {
			case NVME_IRQ_MMIO:
				nvme_regs = (void*)root->paddr + 0xffff800000000000;
				break;
			case NVME_IRQ_CONTROLLER:
				nvme_controller = (void*)root->paddr + 0xffff800000000000;
				break;
			default: RETURN_ERROR;
		}

		root = root->next;
	}

	return 0;
}

int nvme_irq_handle(struct irq_state *state, struct anchor **private) {
	if(private == NULL) RETURN_ERROR;

	struct anchor *anchor_root = *private; 
	if(anchor_root == NULL) RETURN_ERROR;

	if(state->flush) {
		int ret = nvme_anchor_flush(anchor_root);
		if(ret == -1) RETURN_ERROR;
	}

	struct nvme_queue_pair *queue = NULL;
	for(int i = 0; i < nvme_controller->nvme_queue_pair_cnt; i++) {
		if((uint64_t)nvme_controller->nvme_queue_pair[i].irq == state->vector) {
			queue = &nvme_controller->nvme_queue_pair[i];
			break;
		}
	}
	if(queue == NULL) RETURN_ERROR;

	volatile struct nvme_completion *completion_queue = (void*)queue->completion_queue_paddr + 0xffff800000000000;
	volatile uint32_t *completion_doorbell = (void*)nvme_regs + queue->completion_doorbell_offset;
	struct nvme_queue_entry *queue_entry = (struct nvme_queue_entry*)(queue->queue_entry_paddr +
		0xffff800000000000) + completion_queue[queue->cq_head].cid;

	queue_entry->cid = completion_queue[queue->cq_head].cid;
	queue_entry->completion = completion_queue[queue->cq_head];

	*completion_doorbell = ++queue->cq_head;
	if(queue->cq_head == queue->entry_cnt) queue->cq_head = 0;

	if(!queue_entry->blocking) queue_entry->response = true;
	else {
		struct syscall_response response = SYSCALL4(SYSCALL_FUTEX,
			(uintptr_t)&queue_entry->response - 0xffff800000000000, FUTEX_WAKE, true, false);
		if(response.ret == -1) RETURN_ERROR;
	}

	return 0;
}

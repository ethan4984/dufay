#include <fayt/syscall.h>
#include <fayt/lock.h>
#include <fayt/debug.h>
#include <fayt/notification.h>
#include <fayt/circular_queue.h>
#include <fayt/compiler.h>
#include <fayt/address_space.h>
#include <fayt/slab.h>
#include <fayt/hash.h>
#include <fayt/string.h> 
#include <fayt/sched.h>

#include <sched.h>

static struct thread *thread_tree;
static struct hash_table thread_table;

static struct sched_descriptor *sched_desc;
static struct portal_link *sched_meta;

static void notify_enqueue_thread(struct notification_info*, void *data, int) {
	struct sched_queue_config *config = data;
	if(config == NULL) { print("DUFAY: SCHEDULER: config does not exist\n"); goto finish; }

	if(config->offload) {
		struct sched_descriptor *optimal_sched = sched_desc;

		int ret = OPERATE_LINK(sched_meta, LINK_RAW, 
			({
				for(size_t i = 0; i < sched_meta->data_limit / sizeof(struct sched_descriptor); i++) {
					struct sched_descriptor *desc = (struct sched_descriptor*)sched_meta->data + i;

					if(unlikely(optimal_sched == NULL)) optimal_sched = desc;
					else if(desc->load > optimal_sched->load) optimal_sched = desc;
				}
				0;
			})
		);

		if(ret == -1) {
			print("DUFAY: SCHEDULER: Critical failure to enqueue thread\n");
			goto finish;
		}

		if(optimal_sched == sched_desc) goto exit;

		struct comm_bridge bridge = {
			.not = NOT_SCHED_ENQUEUE,
			.cid = optimal_sched->cid,
			.weight = NOTIFY_WEIGHT_INSTANTANEOUS,
			.namespace = NULL,
			.destination = NULL
		};

		bridge.data.limit = sizeof(struct sched_queue_config);
		uintptr_t vaddr;
		ret = as_allocate(&address_space, &vaddr,
			DIV_ROUNDUP(bridge.data.limit, PAGE_SIZE));
		bridge.data.base = (void*)vaddr;
		if(ret == -1) {
			print("DUFAY: SCHEDULER: failed address allocation unable to offload thread to other core\n");
			goto finish;
		} 

		struct syscall_response response = SYSCALL1(SYSCALL_NOTIFICATION_BUILD, &bridge);
		if(response.ret == -1) {
			print("DUFAY: SCHEDULER: NOTIFICAITON BUILD FAILURE: unable to offload thread to other core\n");
			goto finish;
		}

		config->offload = 0;
		memcpy(bridge.data.base, config, bridge.data.limit);

		response = SYSCALL1(SYSCALL_NOTIFICATION_BROADCAST, &bridge);
		if(response.ret == -1) {
			print("DUFAY: SCHEDULER: NOTIFICATION BROADCAST FAILURE: unable to offload thread to other core\n");
			goto finish;
		} else goto finish;
	}
exit:
	struct thread *thread = alloc(sizeof(struct thread));
	if(thread == NULL) { }
	
	void *private;
	struct syscall_response response = SYSCALL2(SYSCALL_CONTEXT, config->cid, &private);
	if(response.ret == -1) {
		print("DUFAY: SCHEDULER: Unable to get context private address\n");
		goto finish; 
	}

	thread->cid = config->cid;
	thread->cgroup = config->cgroup;
	thread->weight = weight_set_nice(config->nice);
	thread->vruntime = VRUNTIME(thread->weight, config->phantom_runtime);
	thread->private = private;

	int ret = RB_GENERIC_INSERT(thread_tree, vruntime, thread);
	if(ret == -1) {
		print("DUFAY: SCHEDULER: Unable to insert on thread tree\n");
		goto finish; 
	}

finish:
	SYSCALL0(SYSCALL_NOTIFICATION_RETURN);
}

static void notify_dequeue_thread(struct notification_info *, void *data, int) {
	struct sched_queue_config *config = data;
	if(config == NULL) goto finish;

	struct thread *thread;
	int ret = hash_table_search(&thread_table, &config->cid, sizeof(config->cid), (void**)&thread);
	if(ret == -1 || thread == NULL) goto finish;

	ret = RB_GENERIC_DELETE(thread_tree, vruntime, thread); 
	if(ret == -1) goto finish;

	struct time epoch = sched_desc->timer.read(&sched_desc->timer);
	struct time delta = time_sub(epoch, thread->epoch);
	thread->vruntime -= VRUNTIME(thread->weight, time_to_ns(delta));
finish:
	SYSCALL0(SYSCALL_NOTIFICATION_RETURN);
}

static int traverse_and_queue(struct thread **thread) {
	if(thread == NULL) return -1;

	struct thread *enqueue = thread_tree;
	while(enqueue && enqueue->left) enqueue = enqueue->left;
	*thread = enqueue;

	return 0;
}

int sched(struct portal_link *link, struct sched_descriptor *desc) {
	if(link == NULL || desc == NULL) return -1;

	struct notification_action enqueue_action =
		{ .handler = notify_enqueue_thread };
	struct notification_action dequeue_action =
		{ .handler = notify_dequeue_thread };

	struct syscall_response response = SYSCALL3(SYSCALL_NOTIFICATION_ACTION,
		NOT_SCHED_ENQUEUE, &enqueue_action, NULL);
	if(response.ret == -1) { print("DUFAY: SCHEDULER: Failure to set notification\n"); return -1; }

	response = SYSCALL3(SYSCALL_NOTIFICATION_ACTION, NOT_SCHED_DEQUEUE, &dequeue_action, NULL);
	if(response.ret == -1) { print("DUFAY: SCHEDULER: Failure to set notification\n"); return -1; }

	print("DUFAY: SCHEDULER: Initialised enqueue and dequeue notifications\n");

	uintptr_t addr;
	int ret = as_allocate(&address_space, &addr, 0x10000);
	if(ret == -1) { print("DUFAY: SCHEDULER: Failed to allocate address\n"); }

	struct portal_resp portal_resp;
	struct portal_req portal_req = {
		.type = PORTAL_REQ_SHARE | PORTAL_REQ_ANON,
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.length = sizeof(struct portal_req), 
		.share = {
			.identifier = "SCHEDULER META",
			.type = LINK_RAW,
			.create = 0
		},
		.morphology = {
			.addr = addr,
			.length = 0x10000
		}
	};

	response = SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
	if(response.ret == -1) { print("DUFAY: SCHEDULER: Failed to establish link\n"); }

	sched_meta = (void*)portal_resp.base;
	sched_desc = desc;

	response = SYSCALL0(SYSCALL_NOTIFICATION_UNMUTE);
	if(response.ret == -1) { print("DUFAY: SCHEDULER: Failed to activate notification queue\n"); return -1; }

	print("DUFAY: SCHEDULER: Enabled notifiactions\n");

	for(;;) {
		SYSCALL0(SYSCALL_SCHED_ACQUIRE);

		struct time epoch = desc->timer.read(&desc->timer);

		for(int i = 0; i < sched_desc->queue_default_refill; i++) {
			struct thread *thread = NULL;
			int ret = traverse_and_queue(&thread);
			if(ret == -1) return -1;
			if(thread == NULL) goto end;

			ret = OPERATE_LINK(link, LINK_CIRCULAR,
				({
					thread->vruntime += VRUNTIME(thread->weight, time_to_ns(desc->slice));
					thread->epoch = epoch;

					RB_GENERIC_DELETE(thread_tree, vruntime, thread);
					RB_GENERIC_INSERT(thread_tree, vruntime, thread);

					//print("scheduler: cid=%x vruntime=%x [epoch: s [%d] ns [%d]]", thread->cid, thread->vruntime, epoch.sec, epoch.nsec);

					ret = circular_queue_push((void*)link + link->data_offset, &thread->private);
				})
			);

			epoch = time_add(epoch, desc->slice);

			if(ret == -1) {
				print("DUFAY: SCHEDULER: Failued to push onto the share queue\n");
				return -1;
			}
		}
end:
		SYSCALL0(SYSCALL_SCHED_RELEASE);
		SYSCALL0(SYSCALL_YIELD);
	}
}

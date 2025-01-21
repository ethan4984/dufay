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
static struct portal_link *sched_meta_link;
static struct portal_link *sched_queue_link;

static int traverse_and_queue(struct thread**);

static int sched_flush_queue(void) {
	struct time epoch = sched_desc->timer.read(&sched_desc->timer);

	int ret = OPERATE_LINK(sched_queue_link, LINK_CIRCULAR,
		({
			circular_queue_flush((void*)sched_queue_link + sched_queue_link->data_offset);
			0;
		})
	);
	if(ret == -1) return -1;

	for(int i = 0; i < sched_desc->queue_default_refill; i++) {
		struct thread *thread = NULL;
		int ret = traverse_and_queue(&thread);
		if(ret == -1) return -1;
		if(thread == NULL) break;

		ret = OPERATE_LINK(sched_queue_link, LINK_CIRCULAR,
			({
				thread->vruntime += VRUNTIME(thread->weight, time_to_ns(sched_desc->slice));
				thread->epoch = epoch;

				RB_GENERIC_DELETE(thread_tree, vruntime, thread);
				RB_GENERIC_INSERT(thread_tree, vruntime, thread);

				ret = circular_queue_push((void*)sched_queue_link +
					sched_queue_link->data_offset, &thread->private);
				ret;
			})
		);

		epoch = time_add(epoch, sched_desc->slice);

		if(ret == -1) {
			print("ERROR: failued to push onto the share queue\n");
			return -1;
		}
	}

	return 0;
}

static void notify_enqueue_thread(struct notification_info*, void *data, int) {
	struct sched_queue_config_set *config_set = data;
	if(config_set == NULL) { print("ERROR: config set is null\n"); goto finish; }

	for(int i = 0; i < config_set->cnt; i++) {
		struct sched_queue_config *config = config_set->config + i;

		if(config->offload) {
			struct sched_descriptor *optimal_sched = sched_desc;

			int ret = OPERATE_LINK(sched_meta_link, LINK_RAW, 
				({
					for(size_t i = 0; i < sched_meta_link->data_limit / sizeof(struct sched_descriptor); i++) {
						struct sched_descriptor *desc = (struct sched_descriptor*)sched_meta_link->data + i;

						if(unlikely(optimal_sched == NULL)) optimal_sched = desc;
						else if(desc->load > optimal_sched->load) optimal_sched = desc;
					}
					0;
				})
			);

			if(ret == -1) {
				print("ERROR: Critical failure to enqueue thread\n");
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
				print("ERROR: failed address allocation unable to offload thread to other core\n");
				continue;
			} 

			struct syscall_response response = SYSCALL1(SYSCALL_NOTIFICATION_BUILD, &bridge);
			if(response.ret == -1) {
				print("ERROR: Unable to offload thread to other core\n");
				continue;
			}

			config->offload = 0;
			memcpy(bridge.data.base, config, bridge.data.limit);

			response = SYSCALL1(SYSCALL_NOTIFICATION_BROADCAST, &bridge);
			if(response.ret == -1) {
				print("ERROR: unable to offload thread to other core\n");
				goto finish;
			} else continue;
		}
exit:
		struct thread *thread = alloc(sizeof(struct thread));
		if(thread == NULL) { }
		
		void *private;
		struct syscall_response response = SYSCALL2(SYSCALL_CONTEXT, config->cid, &private);
		if(response.ret == -1) {
			print("ERROR: unable to get context private address\n");
			continue;
		}

		thread->cid = config->cid;
		thread->cgroup = config->cgroup;
		thread->weight = weight_set_nice(config->nice);
		thread->vruntime = VRUNTIME(thread->weight, config->phantom_runtime);
		thread->private = private;

		int ret = hash_table_push(&thread_table, &thread->cid, thread, sizeof(thread->cid));
		if(ret == -1) {
			print("ERROR: unable to push thread onto thread_table\n");
			continue;
		}

		ret = RB_GENERIC_INSERT(thread_tree, vruntime, thread);
		if(ret == -1) {
			print("ERROR: unable to insert on thread tree\n");
			continue;
		}
	}

	int ret = sched_flush_queue();
	if(ret == -1) { print("ERROR: failed to flush queue\n"); goto finish; }
finish:
	SYSCALL0(SYSCALL_NOTIFICATION_RETURN);
}

static void notify_dequeue_thread(struct notification_info *, void *data, int) {
	struct sched_queue_config_set *config_set = data;
	if(config_set == NULL) goto finish;

	for(int i = 0; i < config_set->cnt; i++) {
		struct sched_queue_config *config = config_set->config + i;

		struct thread *thread = NULL;
		int ret = hash_table_search(&thread_table, &config->cid, sizeof(config->cid), (void**)&thread);
		if(ret == -1 || thread == NULL) continue; 

		ret = RB_GENERIC_DELETE(thread_tree, vruntime, thread); 
		if(ret == -1) continue;

		struct time epoch = sched_desc->timer.read(&sched_desc->timer);
		struct time delta = time_sub(epoch, thread->epoch);
		thread->vruntime -= VRUNTIME(thread->weight, time_to_ns(delta));
	}

	int ret = sched_flush_queue();
	if(ret == -1) { print("ERROR: failed to activate notification queue\n"); goto finish; }
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

	sched_queue_link = link;
	sched_desc = desc;

	struct notification_action enqueue_action =
		{ .handler = notify_enqueue_thread };
	struct notification_action dequeue_action =
		{ .handler = notify_dequeue_thread };

	struct syscall_response response = SYSCALL3(SYSCALL_NOTIFICATION_ACTION,
		NOT_SCHED_ENQUEUE, &enqueue_action, NULL);
	if(response.ret == -1) { print("ERROR: failure to set notification\n"); return -1; }

	response = SYSCALL3(SYSCALL_NOTIFICATION_ACTION, NOT_SCHED_DEQUEUE, &dequeue_action, NULL);
	if(response.ret == -1) { print("ERROR: failure to set notification\n"); return -1; }

	print("Initialised enqueue and dequeue notifications\n");

	uintptr_t addr;
	int ret = as_allocate(&address_space, &addr, 0x10000);
	if(ret == -1) { print("ERROR: failed to allocate address\n"); }

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
	if(response.ret == -1) { print("ERROR: failed to establish link\n"); }

	sched_meta_link = (void*)portal_resp.base;

	response = SYSCALL0(SYSCALL_NOTIFICATION_UNMUTE);
	if(response.ret == -1) { print("ERROR: failed to activate notification queue\n"); return -1; }

	print("Enabled notifiactions\n");

	for(;;) {
		SYSCALL0(SYSCALL_SCHED_ACQUIRE);

		int ret = sched_flush_queue();
		if(ret == -1) { print("ERROR: critical failure to refill queue\n"); return -1; }

		SYSCALL0(SYSCALL_SCHED_RELEASE);
		SYSCALL0(SYSCALL_YIELD);
	}

	return -1;
}

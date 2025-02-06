#include <fayt/syscall.h>
#include <fayt/lock.h>
#include <fayt/debug.h>
#include <fayt/notification.h>
#include <fayt/circular_queue.h>
#include <fayt/compiler.h>
#include <fayt/address_space.h>
#include <fayt/bitmap.h>
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

static struct bitmap cid_bitmap;
static struct bitmap asid_bitmap;

//	CGROUP AND CAPABILITIES
//		METHOD FOR GROUPING CONTEXTS TOGETHER, WITHIN A NEW CGROUP ALL CIDS START AGAIN AT 0, AND THERE ARE
//		PERMISSIONS TO BE ASSOCIATED WITH EACH CGROUP. IF YOU CREATE A NEW CGROUP, ITS PERMISSIONS
//		WILL EITHER BE EQUAL OR LESS THAN THE CALLER. FOR OUR EASE, EACH CLASSIFICATION OF CGROUP
//		WILL HAVE COMMON CAPABILITIES (SECURITY) TO BE PASSED ONTO ITS CHILDREN, ALL CONTEXTS WITHIN
//		A CGROUP INHERIT ITS CAPABILITIES, THO A CONTEXT MAY HAVE MORE OR LESS, IT MUST BE EXPLICITLY
//		DEINFED.
//		
//		struct cgroup {
//			struct bitmap cid_bitmap;
//			struct hash_table cid_table;
//
//			int cgid;
//		};
//
//		struct sched_proc_id {
//			int cgid;
//			int cid;
//		};
//
//		THE KERNEL CREATES A CGROUP INITIALLY, INTENDED FOR SYSTEM SERVICES, WILL BELONG TO CGID=0
//		IT IS THE RESPONSIBILITY OF THE KERNEL TO CREATE AND MAINTAIN THESE GROUPS, IF USER-SPACE
//		WISHES TO CREATE OR DESTROY ONE IT MUST INVOLVE A SYSTEM CALL. THIS IS BECAUSE CGROUPS WILL
//		BE CLOSELY INTERTWINED WITH CAPABILITIES, AND ALSO I DO NOT FEEL LIKE MAINTAINING MORE PARALLEL
//		BUFFERS OF THE SAME DATA BETWEEN THE KERNEL AND THE USER.
//
//		WITHIN THE KERNEL, WE WILL PROVIDE AN INTERFACE FOR THE CREATION, DELETION, VALIDATION, AND
//		SHARING OF CAPABILITIES. FOR CAPABILITIES THAT REQUIRE SUPPORT FROM DEVICE SERVERS (SUCH AS
//		WHETHER OR NOT WE ARE CLEAR TO OPERATE ON A DEVICE SERVER, OR WHETHER OR NOT WE ARE CLEAR TO
//		OPERATE ON A FILE) WE WILL USE A VERY RESOURCEFUL TRICK OF ENGINEERING TO DO SO WITHOUT CONTEXT
//		SWITCHING BACK TO THE VALIDATING, SOME KIND OF GENERAL BUT EXTENSIBLE INTERFACE FOR REPRESENTING
//		AN ARBITRARY OPERATION ON AN ARBITRARY THING, AN ARBITRARY CAPABILITY.
//
//		WE WILL REPRESENT THREADS IN THE FORM A CGROUP/CID PAIR, THROUGH THIS PAIR THE SCHEDULING SERVERS
//		ARE ABLE TO COMMUNICATE WITH THE KERNEL. WHEN WE CLONE A THREAD, IT IS CREATED ON DEMAND, ALL
//		INTERNAL KERNEL CONTEXT STRUCTURES THAT SUPPORT THE THREAD WILL BE CREATED AND APPENDED TO 
//		THE KERNELS INTERNAL REPRESENTATION OF STATE OF THE SYSTEM. 
//
//		struct sched_queue_entry {
//			int cid;
//			int cgroup;
//
//			struct {
//				int active;
//				int asid;
//				int fork;
//			} birth;
//		};
//
//		THIS STRUCTURE REPRESENTS OUR SCHEDULING QUEUE ENTRIES BETWEEN THE SERVER AND USERSPACE. WHEN A THREAD
//		HAS YET TO BE CREATED, THE SCHEDUELR WILL PASS THE BIRTH.ACTIVE=1, AND THEN PASS A FLAG INDICATING WHETHER
//		OR NOT THE NEW TRHEAD SHOULD INHERIT THE ADDRESS SPACE OF THE PARENT. 
//
//		IT HAS BEEN DECIDED THAT ANY DESIGN WILL REQUIRE A BACK-QUEUE FROM THE KERNEL, LETS CALL IT A VOMIT
//		QUEUE, WHERE THE KERNEL CAN SEND WHATEVER KIND OF MESSAGES IT WANTS BACK TO THE SCHEDULING SERVER.
//		SUCH AS IT FAILED TO SUCCESSED IN CREATING A NEW THREAD. OR A FAILURE TO ENQUEUE
//
//		METHOD FOR CAPABILITIES (PUBLIC KEY CRYPTOGRAPHY?)
//

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

				struct sched_queue_entry queue_entry = {
					.proc_id = thread->proc_id,
					.asid = thread->asid
				};

				ret = circular_queue_push((void*)sched_queue_link +
					sched_queue_link->data_offset, &queue_entry);
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

static void notify_clone(struct notification_info*, void *data, int) {
	if(data == NULL) goto finish;

	struct thread *thread = alloc(sizeof(struct thread));
	if(thread == NULL) panic("heap depleted");

	int cid;
	int ret = bitmap_alloc(&cid_bitmap, &cid);
	if(ret == -1) { print("unable to allocate cid\n"); goto finish; }

	int asid;
	ret = bitmap_alloc(&asid_bitmap, &asid);
	if(ret == -1) { print("unable to allocate asid\n"); goto finish; } 

	struct portal_resp portal_resp;
	struct portal_req portal_req = {
		.type = PORTAL_REQ_COW,
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.length = sizeof(struct portal_req), 
		.cow = {
			.source = {
				.asid = -1,
				.base = -1
			},
			.destination = {
				.asid = -1,
				.base = -1
			},
			.limit = -1
		}
	};

	struct syscall_response response = SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
	if(response.ret == -1) { print("failed to clone addres space\n"); goto finish; } 

	sched_desc->load++;

	ret = hash_table_push(&thread_table, &thread->proc_id, thread, sizeof(thread->proc_id));
	if(ret == -1) {
		print("unable to push thread onto thread_table\n");
		goto finish;
	}

	ret = RB_GENERIC_INSERT(thread_tree, vruntime, thread);
	if(ret == -1) {
		print("unable to push thread onto thread_table\n");
		goto finish;
	}
finish:
	SYSCALL0(SYSCALL_NOTIFICATION_RETURN);
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
				.proc_id = optimal_sched->proc_id,
				.not = NOT_SCHED_ENQUEUE,
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
		if(thread == NULL) panic("heap depleted");
		
		thread->proc_id = config->proc_id;
		thread->weight = weight_set_nice(config->nice);
		thread->vruntime = VRUNTIME(thread->weight, config->phantom_runtime);

		sched_desc->load++;

		int ret = hash_table_push(&thread_table, &thread->proc_id, thread, sizeof(thread->proc_id));
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
		int ret = hash_table_search(&thread_table, &config->proc_id, sizeof(config->proc_id), (void**)&thread);
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
	struct notification_action clone_action =
		{ .handler = notify_clone };

	struct syscall_response response = SYSCALL3(SYSCALL_NOTIFICATION_ACTION,
		NOT_SCHED_ENQUEUE, &enqueue_action, NULL);
	if(response.ret == -1) { print("ERROR: failure to set notification\n"); return -1; }

	response = SYSCALL3(SYSCALL_NOTIFICATION_ACTION, NOT_SCHED_DEQUEUE, &dequeue_action, NULL);
	if(response.ret == -1) { print("ERROR: failure to set notification\n"); return -1; }

	response = SYSCALL3(SYSCALL_NOTIFICATION_ACTION, NOT_SCHED_CLONE, &clone_action, NULL);
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

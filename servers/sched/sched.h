#ifndef SCHEDULE_H_
#define SCHEDULE_H_

#include <fayt/rb_tree.h>

#include <portal.h>

constexpr int DEFAULT_TIME_SLICE = 10;
constexpr int NICE_VALUE_MAX = 19;

#define VRUNTIME(WEIGHT, SLICE) ((SLICE) * (1024 / (WEIGHT)));
#define WEIGHT(NICE) (1024 * (1 << (19 - (NICE))));

struct thread {
	int cid;
	int cgroup;

	int weight;
	int vruntime;

	RB_META(struct thread);
};

struct sched_descriptor {
	int processor_id;
	int queue_default_refill;
	int load;
	int cid;
};

struct sched_queue_config {
	int cid;
	int cgroup;
	int nice;
	int offload;
	int phantom_runtime;
};

int sched(struct portal_link*, struct sched_descriptor*);

#endif

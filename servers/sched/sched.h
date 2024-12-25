#ifndef SCHEDULE_H_
#define SCHEDULE_H_

#include <fayt/rb_tree.h>
#include <fayt/lock.h>

#include <portal.h>

constexpr int DEFAULT_TIME_SLICE = 10000000;

#define NICE_MIN (-20)
#define NICE_MAX (19)
#define WEIGHT_MAX (1024)
#define WEIGHT_MIN (1)

#define WEIGHT(N) (WEIGHT_MAX - ((N) - NICE_MIN) * (WEIGHT_MAX - WEIGHT_MIN) / (NICE_MAX - NICE_MIN))
#define VRUNTIME(W, S) ((S) * (WEIGHT_MAX / (W)))

struct thread {
	int cid;
	int cgroup;

	int weight;
	uint64_t vruntime;

	void *private;

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

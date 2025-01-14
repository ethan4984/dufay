#ifndef SCHEDULE_H_
#define SCHEDULE_H_

#include <fayt/rb_tree.h>
#include <fayt/lock.h>
#include <fayt/sched.h>
#include <fayt/time.h>

#include <portal.h>

constexpr int DEFAULT_TIME_SLICE = 10000000;
constexpr int nice_to_weight[40] = {
	88761, 71755, 56483, 46273, 36291,
	29154, 23254, 18705, 14949, 11916,
	9548, 7620, 6100, 4904, 3906,
	3121, 2501, 1991, 1586, 1277,
	1024, 820, 655, 526, 423,
	335, 272, 215, 172, 137,
	110, 87, 70, 56, 45,
	36, 29, 23, 18, 15
};

#define NICE_MIN (-20)
#define NICE_MAX (19)
#define VRUNTIME(W, S) (((S) * (W)) / 1024)

struct thread {
	int cid;
	int cgroup;

	struct time epoch;

	uint64_t weight;
	uint64_t vruntime;

	void *private;

	RB_META(struct thread);
};

static inline uint64_t weight_set_nice(int nice) {
	if(nice < NICE_MIN) nice = NICE_MIN;
	if(nice > NICE_MAX) nice = NICE_MAX;
	return nice_to_weight[nice + 20];
}

int sched(struct portal_link*, struct sched_descriptor*);

#endif

#ifndef CORE_SCHEDULER_CFS_H_
#define CORE_SCHEDULER_CFS_H_

#include <core/scheduler/thread.h>

#include <aria/dictionary.h>
#include <aria/time.h>

struct cfs_unit {
	struct time epoch;
	int nice;
	uint64_t weight;
	uint64_t runtime;
	uint64_t vruntime;
	RB_META(struct cfs_unit);

	struct thread *thread;
};

struct cfs {
	struct cfs_unit *unit_tree;
	struct dictionary *unit_table;
};

int cfs_enqueue(struct scheduler *, struct thread *);
int cfs_dequeue(struct scheduler *, struct thread *);
int cfs_traverse(struct scheduler *, struct thread **);
int cfs_init(struct scheduler *);
int cfs_destroy(struct scheduler *);

constexpr int DEFAULT_TIME_SLICE = 10000000;
constexpr int nice_to_weight[40] = {
	88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
	9548,  7620,  6100,	 4904,	3906,  3121,  2501,	 1991,	1586,  1277,
	1024,  820,	  655,	 526,	423,   335,	  272,	 215,	172,   137,
	110,   87,	  70,	 56,	45,	   36,	  29,	 23,	18,	   15
};

#define NICE_MIN (-20)
#define NICE_MAX (19)
#define VRUNTIME(W, S) (((S) * (W)) / 1024)
#define SCHED_DEFAULT_NICE 0

static inline uint64_t weight_set_nice(int nice)
{
	if (nice < NICE_MIN)
		nice = NICE_MIN;
	if (nice > NICE_MAX)
		nice = NICE_MAX;
	return nice_to_weight[nice + 20];
}

#endif

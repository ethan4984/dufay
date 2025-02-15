#ifndef FAYT_SCHED_H_ 
#define FAYT_SCHED_H_

#include <fayt/bitmap.h>
#include <fayt/hash.h>
#include <fayt/time.h>

constexpr int SCHED_RESERVED_CID = 0x20;
constexpr int SCHED_TICK_RATE_MS = 20;

constexpr int NOT_SCHED_ENQUEUE = 1;
constexpr int NOT_SCHED_DEQUEUE = 2;
constexpr int NOT_SCHED_CLONE = 3;

struct sched_proc_id {
	int cgid;
	int cid;
};

struct sched_descriptor {
	struct timer timer;
	int processor_id;
	int queue_default_refill;
	int load;
	struct sched_proc_id proc_id;
	struct time slice;
};

struct sched_queue_config {
	struct sched_proc_id proc_id;
	int nice;
	int offload;
	int phantom_runtime;
	int flags;
};

struct sched_cgroup {
	struct bitmap cid_bitmap;
	struct hash_table cid_table;

	int cgid;
};

struct sched_queue_config_set {
	int cnt;
	struct sched_queue_config config[];
};

struct sched_queue_entry {
	struct sched_proc_id proc_id;
	int asid;

	struct {
		int active;
		int asid;
		int fork;
	} birth;
};

constexpr int ARCHCTL_SCHED_ACQUIRE = 1;
constexpr int ARCHCTL_SCHED_RELEASE = 2;
constexpr int ARCHCTL_RESERVE_IRQ = 3;
constexpr int ARCHCTL_RELEASE_IRQ = 4;
constexpr int ARCHCTL_YIELD = 5;

constexpr int FUTEX_WAIT = 1;
constexpr int FUTEX_WAKE = 2;

#endif

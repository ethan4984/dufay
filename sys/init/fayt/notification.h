#ifndef FAYT_NOTIFICATION_H_
#define FAYT_NOTIFICATION_H_

#include <fayt/sched.h>

constexpr int NOTIFY_WEIGHT_SCHEDULED = (1 << 0);
constexpr int NOTIFY_WEIGHT_TICK = (1 << 1);
constexpr int NOTIFY_WEIGHT_INSTANTANEOUS = (1 << 2);

struct [[gnu::packed]] comm_bridge {
	int not;

	struct sched_proc_id proc_id;
	int weight;

	struct {
		void *base;
		int limit;
	} data;

	const char *namespace;
	const char *destination;

	int lnkidx;
};

struct notification_info {

}; 

struct notification_action {
	void (*handler)(struct notification_info*, void*, int);
};

int notify(struct comm_bridge*);

#endif

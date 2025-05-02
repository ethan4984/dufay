#ifndef CORE_RESOURCE_H_
#define CORE_RESOURCE_H_

#include <fayt/rb_tree.h>

#include <stdint.h>
#include <stddef.h>

struct rboundary {
	uintptr_t base;
	size_t length;
	int allocated;

	struct rboundary *next;
	struct rboundary *last;

	RB_META(struct rboundary);
};

struct rpool {
	uintptr_t base;
	size_t total;
	size_t free;

	int segment_cnt;
	struct rboundary **segments;
};

int rinit(struct rpool *, uintptr_t *, size_t);
int rdestroy(struct rpool *);

#endif

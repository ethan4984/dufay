#ifndef BCACHE_H_
#define BCACHE_H_

#include <core/capability.h>

#include <fayt/rb_tree.h>

struct blk;
struct bcache {
	struct blk *blk_tree;
	struct capability_binding *capability_binding;
};

struct blk {
	size_t lba;
	void *buffer;
	RB_META(struct blk);
};

struct blk_handle {
	size_t lba_start;
	size_t lba_cnt;
	size_t lba_size;
	void *private;
};

#endif

#ifndef BCACHE_H_
#define BCACHE_H_

#include <core/handle.h>

#include <fayt/rb_tree.h>

struct blk;
struct bcache {
	struct blk *blk_tree;
	struct handle_binding *handle_binding;
};

struct blk {
	size_t lba;
	void *buffer;
	RB_META(struct blk);
};

#endif

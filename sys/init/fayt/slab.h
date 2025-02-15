#ifndef FAYT_SLAB_H_
#define FAYT_SLAB_H_

#include <fayt/lock.h>

#include <stdint.h>
#include <stddef.h>

struct slab;
struct slab_pool;

struct cache {
	struct slab_pool *pool;

	int object_size;
	int active_slabs;
	int pages_per_slab;

	const char *name;

	struct slab *slab_empty;
	struct slab *slab_partial;
	struct slab *slab_full;

	struct cache *next;

	struct spinlock lock;
};

struct slab {
	int available_objects;
	int total_objects;

	char *bitmap;
	void *buffer;

	struct cache *cache;

	struct slab *next; 
	struct slab *last;
};

struct slab_pool {
	int page_size;
	void *data;
	
	void *(*page_alloc)(void*, uint64_t);
	void (*page_free)(void*, uint64_t, uint64_t);
};

int slab_cache_create(struct slab_pool *pool, const char *name, size_t object_size);

void *alloc(size_t size);
void *realloc(void *obj, size_t size);
void free(void *obj);

#endif

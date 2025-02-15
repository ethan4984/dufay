#include <fayt/slab.h>
#include <fayt/string.h>
#include <fayt/lock.h>
#include <fayt/compiler.h>
#include <fayt/debug.h>

static struct slab *cache_alloc_slab(struct cache*);
static void *slab_alloc(struct slab*);
static void *cache_alloc_obj(struct cache*);
static int cache_move_slab(struct slab**, struct slab**, struct slab*);
static size_t slab_get_object_size(struct slab*, void*);
static size_t cache_get_object_size(struct cache*, void*);
static int slab_free_object(struct slab*, void*);
static int cache_free_object(struct cache*, void*);

#define OBJECTS_PER_SLAB 512

static struct cache *root_cache;

static struct slab *cache_alloc_slab(struct cache *cache) {
	if(unlikely(cache == NULL)) return NULL;

	struct slab_pool *pool = cache->pool;
	if(unlikely(pool == NULL)) return NULL;

	struct slab *new_slab = (struct slab*)pool->page_alloc(pool->data, cache->pages_per_slab);
	if(new_slab == NULL) return NULL;

	new_slab->bitmap = (void*)(new_slab + 1);
	new_slab->buffer = (void*)(ALIGN_UP((uintptr_t)new_slab->bitmap + OBJECTS_PER_SLAB, 16));

	new_slab->available_objects = OBJECTS_PER_SLAB;
	new_slab->total_objects = OBJECTS_PER_SLAB;
	new_slab->cache = cache;

	if(cache->slab_empty) {
		cache->slab_empty->last = new_slab;
	} 

	new_slab->next = cache->slab_empty;
	cache->slab_empty = new_slab;

	return new_slab;
}

static void *slab_alloc(struct slab *slab) {
	if(unlikely(slab == NULL)) return NULL;

	for(int i = 0; i < slab->total_objects; i++) {
		if(!BIT_TEST(slab->bitmap, i)) {
			BIT_SET(slab->bitmap, i);
			slab->available_objects--;

			memset(slab->buffer + (i * slab->cache->object_size), 0, slab->cache->object_size);

			return slab->buffer + (i * slab->cache->object_size);
		}
	}

	return NULL;
}

static void *cache_alloc_obj(struct cache *cache) {
	if(unlikely(cache == NULL)) return NULL;
	struct slab *slab = NULL;

	spinlock(&cache->lock);

	if(cache->slab_partial) {
		slab = cache->slab_partial;
	} else if(cache->slab_empty) {
		slab = cache->slab_empty;
	}

	if(!slab) {
		slab = cache_alloc_slab(cache);
		cache->slab_empty = slab;
	}

	void *addr = slab_alloc(slab);

	if(slab->available_objects == 0) {
		cache_move_slab(&cache->slab_full, &cache->slab_partial, slab);
	} else if(slab->available_objects == (slab->total_objects - 1)) {
		cache_move_slab(&cache->slab_partial, &cache->slab_empty, slab);
	}

	spinrelease(&cache->lock);

	return addr;
}

int slab_cache_create(struct slab_pool *pool, const char *name, size_t object_size) {
	struct cache cache = { 0 };

	cache.pages_per_slab = DIV_ROUNDUP(object_size * OBJECTS_PER_SLAB +
			sizeof(struct slab) + OBJECTS_PER_SLAB, pool->page_size) + 16;
	cache.object_size = object_size;
	cache.name = name;
	cache.pool = pool;

	struct slab *root_slab = cache_alloc_slab(&cache);
	if(root_slab == NULL) RETURN_ERROR;

	*(struct cache*)root_slab->buffer = cache;
	struct cache *new_cache = (struct cache*)root_slab->buffer;

	root_slab->cache = new_cache;
	root_slab->buffer += sizeof(struct cache);
	root_slab->buffer = (void*)(ALIGN_UP((uintptr_t)root_slab->buffer, 16));
	root_slab->available_objects -= DIV_ROUNDUP(sizeof(struct cache), object_size);
	root_slab->total_objects = root_slab->available_objects;

	new_cache->slab_empty = root_slab;
	new_cache->next = root_cache;

	root_cache = new_cache;

	return 0;
}

static int cache_move_slab(struct slab **dest_head, struct slab **src_head, struct slab *src) {
	if(!src || !*src_head) RETURN_ERROR; 
	if(src->next != NULL) src->next->last = src->last;
	if(src->last != NULL) src->last->next = src->next;
	if(*src_head == src) *src_head = src->next;

	if(!*dest_head) {
		src->last = NULL;
		src->next = NULL;

		*dest_head = src; 

		return 0;
	}

	src->next = *dest_head;
	src->last = NULL;

	if(*dest_head) {
		(*dest_head)->last = src;
	}

	*dest_head = src;

	return 0;
}

static size_t slab_get_object_size(struct slab *slab, void *obj) {
	if(unlikely(slab == NULL)) return 0;
	if(unlikely(slab->cache == NULL)) return 0;

	spinlock(&slab->cache->lock);

	struct slab *root = slab;

	while(slab) {
		if(slab->buffer <= obj && (slab->buffer + slab->cache->object_size * slab->total_objects) > obj) {
			spinrelease(&root->cache->lock);
			return slab->cache->object_size;
		}

		slab = slab->next;
	}

	spinrelease(&root->cache->lock);

	return 0;
}

static size_t cache_get_object_size(struct cache *cache, void *obj) {
	if(unlikely(cache == NULL)) RETURN_ERROR;

	size_t partial_object_size = slab_get_object_size(cache->slab_partial, obj);
	if(partial_object_size)	return partial_object_size;

	size_t full_object_size = slab_get_object_size(cache->slab_full, obj);
	if(full_object_size) return full_object_size;

	return 0;
}

static int slab_free_object(struct slab *slab, void *obj) {
	if(unlikely(slab == NULL)) RETURN_ERROR;

	spinlock(&slab->cache->lock);

	struct slab *root = slab;

	while(slab) {
		if(slab->buffer <= obj && (slab->buffer + slab->cache->object_size * slab->total_objects) > obj) {
			size_t index = ((uintptr_t)obj - (uintptr_t)slab->buffer) / slab->cache->object_size;

			if(BIT_TEST(slab->bitmap, index)) {
				BIT_CLEAR(slab->bitmap, index);
				slab->available_objects++;

				spinrelease(&root->cache->lock);

				return slab->cache->object_size;
			}
		}

		slab = slab->next;
	}

	spinrelease(&root->cache->lock);

	return 0;
}

static int cache_free_object(struct cache *cache, void *obj) {
	if(cache == NULL || obj == NULL) RETURN_ERROR;
	if(cache->slab_partial && slab_free_object(cache->slab_partial, obj)) return 0;
	else if(cache->slab_full && slab_free_object(cache->slab_full, obj)) return 0;
	else return 0;
}

void *alloc(size_t size) {
	if(!size) return NULL;

	int round_size = pow2_roundup(size + 1);
	if(round_size <= 16) {
		round_size = 32;
	}

	struct cache *cache = root_cache;

	while(cache) {
		if(cache->object_size == round_size) {
			return cache_alloc_obj(cache);
		}

		cache = cache->next;
	}

	return NULL;
}

void free(void *obj) {
	if(!obj) {
		return;
	}

	struct cache *cache = root_cache;

	while(cache) {
		if(cache_free_object(cache, obj) == 0) {
			return;
		}

		cache = cache->next;
	}
}

void *realloc(void *obj, size_t size) {
	if(obj == NULL) {
		return alloc(size);
	}

	struct cache *cache = root_cache;
	size_t object_size = 0;

	while(cache) {
		object_size = cache_get_object_size(cache, obj);	

		if(object_size) {
			break;
		}

		cache = cache->next;
	}

	if(object_size >= size) {
		return obj;
	}

	void *ret = alloc(size);

	memcpy(ret, obj, object_size);
	free(obj);

	return ret;
}

/*
 Slab allocator implementation with per-cpu object caching (magazines)
 As described in "The Slab Allocator: An Object-Caching Kernel Memory Allocator"
 and "Magazines and Vmem: Extending the Slab Allocator to Many CPUs and
 Arbitrary Resources" */

#ifndef SLAB_H
#define SLAB_H
#include <fayt/dictionary.h>
#include <fayt/lock.h>
#include <stddef.h>
#include <sys/queue.h>

#define SLAB_CACHE_NAME_LEN 16

#ifdef __cplusplus
extern "C" {
#endif

struct kmem_slab;

struct kmem_bufctl {
  /// Next bufctl
  struct kmem_bufctl *next;

  /// Pointer to the buffer
  void *buffer;

  /// Pointer to the slab that owns this buffer
  struct kmem_slab *slab;
};

struct kmem_slab {
  /// Linkage into SlabCache::slabs
  TAILQ_ENTRY(kmem_slab) list_hook;

  /// Reference count indicating how many objects are in use
  size_t refcount;

  /// Freelist of buffers
  struct kmem_bufctl *buflist;
};

struct kmem_magazine {
  /// Next Magazine in list
  struct kmem_magazine *next;

  /// 1 or more rounds
  void *rounds[1];
};

struct kmem_maglist {
  struct kmem_magazine *head;
};

struct kmem_cache;

struct kmem_magtype {
  size_t rounds;
  size_t align;
  size_t minbuf;
  size_t maxbuf;

  /// Magazine cache
  struct kmem_cache *cache;
};

struct kmem_cpu {
  struct kmem_cpu *next;
  struct spinlock lock;

  /// Currently loaded magazine
  struct kmem_magazine *loaded;

  /// Previously loaded magazine
  struct kmem_magazine *previous;

  /// Number of rounds in loaded magazine
  int rounds;

  /// Number of rounds in previous magazine
  int rounds_previous;

  /// Number of rounds in a full mmagazine
  size_t magazine_size;
};

struct kmem_cache {
  /// Cache name (for debugging)
  char name[SLAB_CACHE_NAME_LEN];

  /// Size of each object in the cache
  size_t object_size;

  /// Size of a single chunk
  size_t chunk_size;

  /// Size of a slab
  size_t slab_size;

  /// Offset for buf-to-bufctl conversion
  size_t offset;

  /// Object alignment
  size_t object_align;

  /// Max color for slab coloring
  size_t maxcolor;

  /// Current color for slab coloring
  size_t color;

  /// Cache lock
  struct spinlock lock;

  TAILQ_HEAD(, kmem_slab) full_slabs;
  TAILQ_HEAD(, kmem_slab) slabs;

  /// For cache freelist
  struct kmem_cache *next;

  /// Buffer-to-bufctl hash map
  struct dictionary bufmap;

  struct spinlock depot_lock;
  size_t depot_contention;

  /// Magazine lists
  struct kmem_maglist empty_magazines;
  struct kmem_maglist full_magazines;

  /// Magazine type
  struct kmem_magtype *magtype;

  /// Per-CPU data
  struct kmem_cpu *cpu;

  void (*constructor)(void *);
  void (*destructor)(void *);
};

void kmem_init();

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
                                     size_t align, void (*ctor)(void *),
                                     void (*dtor)(void *));
void kmem_cache_destroy(struct kmem_cache *cache);
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *ptr);
void kmem_cache_dump(struct kmem_cache *cache);

void *kmem_malloc(size_t size);
void kmem_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
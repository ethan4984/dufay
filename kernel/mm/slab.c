#define CPU_COUNT 16
#include "slab.h"

#include <fayt/debug.h>
#include <fayt/string.h>
#include <sys/mman.h>
#include <fayt/dictionary.h>

#include <stdint.h>
#include "physical.h"
#include <core/cpu.h>

#define SLAB_ALIGN 8
#define CACHES_NUM 32

#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

// Every power-of-two size from 8 to 2048 (inclusively)
#define GENERIC_CACHES_NUM 9

const char *generic_caches_names[GENERIC_CACHES_NUM] = {
	"generic-8",   "generic-16",  "generic-32",	  "generic-64",	 "generic-128",
	"generic-256", "generic-512", "generic-1024", "generic-2048"
};

// 1/8th of a page
#define SMALL_SLAB_SIZE 512

// This is an arbitrary number, ideally this should be determined on cache
// creation to minimize internal fragmentation
#define OBJECTS_PER_SLAB 16

// Caches used to allocate out-of-line bufctls for large slabs
static struct kmem_cache *bufctl_cache;
static struct kmem_cache *slab_cache;

// Static slab caches
static struct kmem_cache caches[CACHES_NUM];
static struct kmem_cache *cache_freelist;

// Generic caches for power-of-two-sizes
static struct kmem_cache *generic_caches[GENERIC_CACHES_NUM];

// size, align, min_buf, max_buf, cache
static struct kmem_magtype magtypes[] = {
	{ 1, 8, 3200, 65536, NULL }, { 3, 16, 256, 32768, NULL },
	{ 7, 32, 64, 16384, NULL },	 { 15, 64, 0, 8192, NULL },
	{ 31, 64, 0, 4096, NULL },	 { 47, 64, 0, 2048, NULL },
	{ 63, 64, 0, 1024, NULL },	 { 95, 64, 0, 512, NULL },
	{ 143, 64, 0, 0, NULL },
};

size_t cache_num = 0;

// These should ideally be replaced by a proper resource allocator, but this is fine for now.
void *alloc_pages(size_t num_pages)
{
	return (void *)pmm_alloc(num_pages, 1) + HIGH_VMA;
}

void free_pages(void *ptr, size_t num_pages)
{
	pmm_free((uintptr_t)ptr - HIGH_VMA, num_pages);
}

static struct kmem_slab *slab_create_small(struct kmem_cache *cp)
{
	struct kmem_slab *ret;
	size_t capacity = 0;
	uint8_t *buf = (uint8_t *)alloc_pages(1);

	// We employ a simple coloring scheme;
	// Everytime a slab is created, the color is shifted by the alignment,
	// This is done until we reach the max color (which is when there is no space
	// left in the slab buffer). This ensures uniform buffer address distribution
	cp->color += cp->object_align;

	if (cp->color > cp->maxcolor) {
		cp->color = 0;
	}

	// Slab info is located at the end of the page
	ret = (struct kmem_slab *)((uintptr_t)buf + PAGE_SIZE -
							   sizeof(struct kmem_slab));

	capacity =
		(PAGE_SIZE - sizeof(struct kmem_slab) - cp->color) / cp->chunk_size;

	ret->buflist = NULL;

	buf += cp->color;

	for (size_t i = 0; i < capacity; i++) {
		struct kmem_bufctl *bufctl =
			(struct kmem_bufctl *)(buf + i * cp->chunk_size + cp->offset);

		// Add bufctl to freelist
		// NOTE: A bufctl field other than `next` must NOT be modified.
		bufctl->next = ret->buflist;
		ret->buflist = bufctl;

		if (cp->constructor) {
			cp->constructor((buf + i * cp->chunk_size));
		}
	}

	return ret;
}

static struct kmem_slab *slab_create_large(struct kmem_cache *cp)
{
	cp->color += cp->object_align;

	if (cp->color > cp->maxcolor) {
		cp->color = 0;
	}

	size_t capacity = (cp->slab_size - cp->color) / cp->chunk_size;

	// slab_size is guaranteed to be a multiple of PAGE_SIZE
	uint8_t *buf = (uint8_t *)alloc_pages(cp->slab_size / PAGE_SIZE);

	struct kmem_slab *ret = (struct kmem_slab *)kmem_cache_alloc(slab_cache);

	ret->buflist = NULL;

	for (size_t i = 0; i < capacity; i++) {
		struct kmem_bufctl *bufctl =
			(struct kmem_bufctl *)kmem_cache_alloc(bufctl_cache);

		bufctl->buffer = (void *)(buf + cp->color + i * cp->chunk_size);
		bufctl->slab = ret;

		bufctl->next = ret->buflist;

		ret->buflist = bufctl;

		if (cp->constructor) {
			cp->constructor(bufctl->buffer);
		}
	}

	return ret;
}

static struct kmem_slab *slab_create(struct kmem_cache *cp)
{
	if (cp->object_size <= SMALL_SLAB_SIZE) {
		return slab_create_small(cp);
	} else {
		return slab_create_large(cp);
	}
}

static void slab_destroy(struct kmem_cache *cp, struct kmem_slab *slab)
{
	if (cp->object_size > SMALL_SLAB_SIZE) {
		struct kmem_bufctl *bufctl = slab->buflist;

		while (bufctl) {
			struct kmem_bufctl *next = bufctl->next;
			kmem_cache_free(bufctl_cache, bufctl);
			bufctl = next;
		}

		kmem_cache_free(slab_cache, slab);
	} else {
		free_pages((void *)(ALIGN_DOWN((uintptr_t)slab, PAGE_SIZE)),
				   cp->slab_size / PAGE_SIZE);
	}
}

static inline size_t get_curr_cpu()
{
	return CORE_LOCAL->core_id;
}

static void cpu_reload(struct kmem_cpu *cpu, struct kmem_magazine *mag,
					   int rounds)
{
	cpu->previous = cpu->loaded;
	cpu->rounds_previous = cpu->rounds;
	cpu->loaded = mag;
	cpu->rounds = rounds;
}

static struct kmem_magazine *maglist_alloc(struct kmem_maglist *list)
{
	struct kmem_magazine *ret = NULL;

	if ((ret = list->head) != NULL) {
		list->head = ret->next;
	}

	return ret;
}

static void maglist_free(struct kmem_maglist *list, struct kmem_magazine *mag)
{
	mag->next = list->head;
	list->head = mag;
}

#define trylock(x) \
	(!__atomic_test_and_set((void *)&(x)->lock, __ATOMIC_ACQUIRE))

static struct kmem_magazine *alloc_from_depot(struct kmem_cache *cp,
											  struct kmem_maglist *list)
{
	if (!trylock(&cp->depot_lock)) {
		spinlock(&cp->depot_lock);
		cp->depot_contention++;
	}

	void *ret = maglist_alloc(list);
	spinrelease(&cp->depot_lock);
	return ret;
}

static void free_to_depot(struct kmem_cache *cp, struct kmem_magazine *mag,
						  struct kmem_maglist *list)
{
	spinlock(&cp->depot_lock);
	maglist_free(list, mag);
	spinrelease(&cp->depot_lock);
}

void *kmem_cache_alloc(struct kmem_cache *cp)
{
	struct kmem_slab *slab = NULL;
	struct kmem_bufctl *bufctl = NULL;
	void *buf = NULL;
	struct kmem_cpu *cpu = NULL;

	cpu = &cp->cpu[get_curr_cpu()];

	spinlock(&cpu->lock);

	for (;;) {
		// Try getting a round from the per-cpu cache
		if (cpu->rounds > 0) {
			buf = cpu->loaded->rounds[--cpu->rounds];
			spinrelease(&cpu->lock);
			return buf;
		}

		// Loaded magazine is empty, if the previous magazine is not empty, exchange
		// them
		if (cpu->rounds_previous > 0) {
			cpu_reload(cpu, cpu->previous, cpu->rounds_previous);
			continue;
		}

		// Try to get a full magazine from the depot
		struct kmem_magazine *mag = alloc_from_depot(cp, &cp->full_magazines);

		if (mag) {
			// Put previous magazine on empty list because it will get emptied on
			// reload()
			if (cpu->previous) {
				free_to_depot(cp, cpu->previous, &cp->empty_magazines);
			}

			cpu_reload(cpu, mag, cpu->magazine_size);
			continue;
		}

		break;
	}

	spinrelease(&cpu->lock);

	// Fall back to slab layer
	spinlock(&cp->lock);

	slab = TAILQ_FIRST(&cp->slabs);

	if (!slab) {
		spinrelease(&cp->lock);

		// Freelist is empty, create a new slab
		slab = slab_create(cp);

		if (!slab) {
			return NULL;
		}

		spinlock(&cp->lock);

		TAILQ_INSERT_TAIL(&cp->slabs, slab, list_hook);
	}

	// If this is the last buffer in the slab, remove the slab from the freelist
	if (!(bufctl = slab->buflist)->next) {
		TAILQ_REMOVE(&cp->slabs, slab, list_hook);
		TAILQ_INSERT_TAIL(&cp->full_slabs, slab, list_hook);
	}

	slab->buflist = bufctl->next;
	slab->refcount++;

	if (cp->object_size > SMALL_SLAB_SIZE) {
		buf = bufctl->buffer;
		dictionary_push(&cp->bufmap, buf, bufctl, sizeof(buf));
	} else {
		buf = (void *)((uintptr_t)bufctl - cp->offset);
	}

	spinrelease(&cp->lock);

	return buf;
}

void kmem_cache_free(struct kmem_cache *cp, void *ptr)
{
	struct kmem_cpu *cpu = &cp->cpu[get_curr_cpu()];

	spinlock(&cpu->lock);

	for (;;) {
		// There's space in the magazine, put the object there
		if ((size_t)cpu->rounds < cpu->magazine_size) {
			cpu->loaded->rounds[cpu->rounds++] = ptr;
			spinrelease(&cpu->lock);
			return;
		}

		// Loaded magazine is full, try to exchange it with the previous one if it
		// was empty
		if (cpu->rounds_previous == 0) {
			cpu_reload(cpu, cpu->previous, 0);
			continue;
		}

		// Try to get an empty magazine from the depot
		struct kmem_magazine *mag = alloc_from_depot(cp, &cp->empty_magazines);

		if (mag) {
			// Put the previous magazine on the full list
			if (cpu->previous) {
				free_to_depot(cp, cpu->previous, &cp->full_magazines);
			}

			cpu_reload(cpu, mag, 0);
			continue;
		}

		// No empty magazines in the depot, try to allocate a new one
		struct kmem_magazine *new_mag;

		spinrelease(&cpu->lock);

		new_mag = (struct kmem_magazine *)kmem_cache_alloc(cp->magtype->cache);

		spinlock(&cpu->lock);

		// We got a new empty magazine, add it to the empty depot and retry
		if (new_mag) {
			free_to_depot(cp, new_mag, &cp->empty_magazines);
			continue;
		}

		break;
	}

	spinrelease(&cpu->lock);

	// First destroy the object
	if (cp->destructor) {
		cp->destructor(ptr);
	}

	spinlock(&cp->lock);

	struct kmem_bufctl *bufctl = NULL;
	struct kmem_slab *slab;

	// Then find the bufctl for the buffer
	if (cp->object_size > SMALL_SLAB_SIZE) {
		int r =
			dictionary_search(&cp->bufmap, ptr, sizeof(ptr), (void **)&bufctl);

		if (r != 0) {
			spinrelease(&cp->lock);
			return;
		}

		slab = bufctl->slab;

		dictionary_delete(&cp->bufmap, ptr, sizeof(ptr));
	} else {
		bufctl = (struct kmem_bufctl *)((uintptr_t)ptr + cp->offset);
		slab = (struct kmem_slab *)(ALIGN_DOWN((uintptr_t)ptr, PAGE_SIZE) +
									PAGE_SIZE - sizeof(struct kmem_slab));
	}

	// There were no buffers in the slab, so it wasn't in the freelist.
	// Now that there is a buffer, add it back to the freelist
	if (!slab->buflist) {
		TAILQ_REMOVE(&cp->full_slabs, slab, list_hook);
		TAILQ_INSERT_HEAD(&cp->slabs, slab, list_hook);
	}

	// Insert bufctl into the slab freelist
	bufctl->next = slab->buflist;
	slab->buflist = bufctl;

	// There are no more outstanding allocations,
	// It is safe to reclaim the slab
	if (!--slab->refcount) {
		TAILQ_REMOVE(&cp->slabs, slab, list_hook);
		spinrelease(&cp->lock);
		slab_destroy(cp, slab);
		return;
	}

	spinrelease(&cp->lock);
}

void kmem_cache_dump(struct kmem_cache *cp)
{
	struct kmem_slab *slab;
	print("Cache: \"%s\":\n", cp->name);
	print("- Object size: %zu\n", cp->object_size);
	print("- Chunk size: %zu\n", cp->chunk_size);
	print("- Object align: %zu\n", cp->object_align);
	print("- Slab size: %zu\n", cp->slab_size);
	print("- Max color: %zu\n", cp->maxcolor);
	print("- Color: %zu\n", cp->color);
	print("- Depot Contentions: %zu\n", cp->depot_contention);
	print("- Non-full slabs:\n");

	TAILQ_FOREACH(slab, &cp->slabs, list_hook)
	{
		print("  |- Slab: %p\n", slab);
		print("     |- Refcount: %zu\n", slab->refcount);
		print("     |- BufList:\n");

		for (struct kmem_bufctl *bufctl = slab->buflist; bufctl;
			 bufctl = bufctl->next) {
			print("        |- BufCtl: %p\n", bufctl);
		}
	}
}

void kmem_init()
{
	cache_freelist = NULL;

	for (size_t i = 0; i < CACHES_NUM; i++) {
		caches[i].next = cache_freelist;
		caches[i].cpu = alloc(logical_processor_cnt * sizeof(struct kmem_cpu));
		cache_freelist = &caches[i];
	}

	bufctl_cache =
		kmem_cache_create("bufctl", sizeof(struct kmem_bufctl), 0, NULL, NULL);
	slab_cache =
		kmem_cache_create("slab", sizeof(struct kmem_slab), 0, NULL, NULL);

	// Initialize generic caches
	for (size_t i = 0; i < GENERIC_CACHES_NUM; i++) {
		generic_caches[i] = kmem_cache_create(generic_caches_names[i],
											  1UL << (i + 3), 0, NULL, NULL);
	}

	for (size_t i = 0; i < LENGTHOF(magtypes); i++) {
		struct kmem_magtype *mtp = &magtypes[i];

		mtp->cache = kmem_cache_create("magazine",
									   (mtp->rounds + 1) * sizeof(void *),
									   mtp->align, NULL, NULL);
	}
}

void kmem_deinit()
{
	for (int i = 0; i < CACHES_NUM; i++) {
		if (caches[i].name[0] == 0) {
			break;
		}

		kmem_cache_destroy(&caches[i]);
	}
}

static inline int get_cache_index(size_t size)
{
	if (size <= 8)
		return 0;
	if (size <= 16)
		return 1;
	if (size <= 32)
		return 2;
	if (size <= 64)
		return 3;
	if (size <= 128)
		return 4;
	if (size <= 256)
		return 5;
	if (size <= 512)
		return 6;
	if (size <= 1024)
		return 7;
	if (size <= 2048)
		return 8;

	return -1;
}

static inline void *slab_alloc(size_t size)
{
	size_t index = get_cache_index(size);

	if (index == (size_t)-1) {
		return alloc_pages(ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE);
	}

	return kmem_cache_alloc(generic_caches[index]);
}

void *kmem_malloc(size_t size)
{
	size_t real_size = size + sizeof(size_t);
	void *ptr = slab_alloc(real_size);

	*(size_t *)ptr = size;
	return (void *)((uintptr_t)ptr + sizeof(size_t));
}

static void do_slab_free(void *ptr, size_t size)
{
	size_t index = get_cache_index(size);

	if (index == (size_t)-1) {
		return free_pages(ptr, ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE);
	}

	return kmem_cache_free(generic_caches[index], ptr);
}

void kmem_free(void *ptr)
{
	size_t *size_ptr = (size_t *)((char *)ptr - sizeof(size_t));
	size_t size = *size_ptr;
	do_slab_free(size_ptr, size + sizeof(size_t));
}

void kmem_malloc_dump()
{
	for (size_t i = 0; i < GENERIC_CACHES_NUM; i++) {
		kmem_cache_dump(generic_caches[i]);
	}
}

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
									 size_t align, void (*constructor)(void *),
									 void (*destructor)(void *))
{
	if (!cache_freelist) {
		return NULL;
	}

	// Remove cache from freelist
	struct kmem_cache *cache = cache_freelist;
	cache_freelist = cache->next;

	cache_num++;

	size_t object_size = 0;
	size_t chunk_size = 0;
	size_t object_align = 0;
	size_t slab_size = 0;
	size_t maxcolor = 0;
	size_t offset = 0;

	object_size = chunk_size = size;
	object_align = align == 0 ? SLAB_ALIGN : align;

	if (object_align >= SLAB_ALIGN) {
		chunk_size = ALIGN_UP(chunk_size, SLAB_ALIGN);
		offset = chunk_size - SLAB_ALIGN;
	} else {
		offset = 0;
	}

	chunk_size = ALIGN_UP(chunk_size, object_align);

	if (object_size <= SMALL_SLAB_SIZE) {
		slab_size = PAGE_SIZE;
		maxcolor = (slab_size - sizeof(struct kmem_slab)) % chunk_size;
	} else {
		slab_size = ALIGN_UP(chunk_size * OBJECTS_PER_SLAB, PAGE_SIZE);
		maxcolor = slab_size % chunk_size;
	}

	TAILQ_INIT(&cache->slabs);
	TAILQ_INIT(&cache->full_slabs);

	memcpy(cache->name, name, strlen(name));
	cache->name[strlen(name)] = '\0';

	cache->object_size = object_size;
	cache->chunk_size = chunk_size;
	cache->object_align = object_align;
	cache->slab_size = slab_size;
	cache->maxcolor = maxcolor;
	cache->color = 0;
	cache->offset = offset;
	cache->next = NULL;
	cache->constructor = constructor;
	cache->destructor = destructor;
	cache->depot_contention = 0;

	// Magazine layer initialization
	struct kmem_magtype *magtype = NULL;

	for (magtype = magtypes; chunk_size <= magtype->minbuf; magtype++)
		continue;

	cache->magtype = magtype;

	// KERNEL: do per-cpu init

	for (size_t i = 0; i < logical_processor_cnt; i++) {
		cache->cpu[i].magazine_size = magtype->rounds;
		cache->cpu[i].rounds = -1;
		cache->cpu[i].rounds_previous = -1;
		cache->cpu[i].loaded = NULL;
		cache->cpu[i].previous = NULL;
	}

	return cache;
}

void kmem_cache_destroy(struct kmem_cache *cp)
{
	struct kmem_slab *slab, *slab_temp;

	TAILQ_FOREACH_SAFE(slab, &cp->slabs, list_hook, slab_temp)
	{
		TAILQ_REMOVE(&cp->slabs, slab, list_hook);
		slab_destroy(cp, slab);
	}

	TAILQ_FOREACH_SAFE(slab, &cp->full_slabs, list_hook, slab_temp)
	{
		TAILQ_REMOVE(&cp->full_slabs, slab, list_hook);
		slab_destroy(cp, slab);
	}

	cache_num--;

	cp->next = cache_freelist;
	cache_freelist = cp;
}

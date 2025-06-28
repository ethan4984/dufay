#ifndef CORE_OBJECT_H_
#define CORE_OBJECT_H_

#include <stddef.h>
#include <stdint.h>
#include <mm/slab.h>

struct object_header {
	// Which class is the object?
	uint8_t class;

	// Count of handles to that object
	uint32_t refcount;
};

struct object_class {
	/* Short but descriptive name */
	char name[8];
	void (*constructor)(void *ptr);
	void (*destructor)(void *ptr);
	size_t size;
	struct kmem_cache *cache;
};

/*
  Register a new object class
  NOTE: the kmem cache will be initialized by this function if `data.cache` is NULL
*/
int object_register_class(uint8_t class, struct object_class data);

int object_new(void **out, uint8_t class);

void object_retain(void *obj);
void object_release(void *obj);

#endif

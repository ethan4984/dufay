#ifndef OBJECT_H_
#define OBJECT_H_

#include <stddef.h>
#include <stdint.h>

struct object_header {
	// Which class is the object?
	uint8_t class;

	// Count of handles to that object
	uint32_t refcount;
};

struct object_class {
	void (*constructor)(void *ptr);
	void (*destructor)(void *ptr);
	size_t size;
};

int object_register_class(uint8_t class, struct object_class data);

int object_new(void **out, uint8_t class);

void object_retain(void *obj);
void object_release(void *obj);

#endif

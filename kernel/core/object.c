#include <core/object.h>

#include <fayt/debug.h>
#include <fayt/slab.h>

static struct object_class classes[256];

int object_register_class(uint8_t class, struct object_class data)
{
	if (classes[class].size > 0) {
		RETURN_ERROR;
	}

	classes[class] = data;

	return 0;
}

int object_new(void **out, uint8_t class)
{
	struct object_class obj_class = classes[class];

	if (obj_class.size == 0) {
		*out = NULL;
		RETURN_ERROR;
	}

	struct object_header *newobj =
		alloc(sizeof(struct object_header) + obj_class.size);

	*out = ((char *)newobj) + sizeof(struct object_header);

	obj_class.constructor(*out);

	return 0;
}

void object_retain(void *obj)
{
	struct object_header *hdr =
		(struct object_header *)((char *)obj - sizeof(struct object_header));

	__atomic_fetch_add(&hdr->refcount, 1, __ATOMIC_RELAXED);
}

void object_release(void *obj)
{
	struct object_header *hdr =
		(struct object_header *)((char *)obj - sizeof(struct object_header));

	uint32_t rc = __atomic_fetch_sub(&hdr->refcount, 1, __ATOMIC_RELAXED);

	if (rc == 1) {
		struct object_class obj_class = classes[hdr->class];

		obj_class.destructor(obj);

		free(obj);
	}
}

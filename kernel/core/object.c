#include <core/object.h>

#include <fayt/debug.h>
#include <fayt/slab.h>
#include <fayt/string.h>
#include <mm/slab.h>

static struct object_class classes[256];

static void obj_constructor(void *obj)
{
	struct object_header *hdr = (struct object_header *)(obj);
	struct object_class class = classes[hdr->class];

	/* We pre-initialize objects as the allocator does not guarantee zero-initialization */
	memset(obj, 0, sizeof(struct object_header) + class.size);

	if (class.constructor != NULL) {
		/* Call the constructor for this object */
		class.constructor((char *)obj + sizeof(struct object_header));
	}
}

static void obj_destructor(void *obj)
{
	struct object_header *hdr = (struct object_header *)(obj);
	struct object_class class = classes[hdr->class];
	if (class.destructor != NULL) {
		/* Call the constructor for this object */
		class.destructor((char *)obj + sizeof(struct object_header));
	}
}

int object_register_class(uint8_t class, struct object_class data)
{
	if (classes[class].size > 0) {
		RETURN_ERROR;
	}

	if (data.cache == NULL) {
		/* Create a new cache for this object type */
		data.cache = kmem_cache_create(data.name,
									   sizeof(struct object_header) + data.size,
									   0, obj_constructor, obj_destructor);

		if (data.cache == NULL) {
			RETURN_ERROR;
		}
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

	/* No need to call the constructor here, as kmem_cache_alloc
	   will handle it for us. */
	struct object_header *newobj = kmem_cache_alloc(obj_class.cache);

	if (newobj == NULL)
		RETURN_ERROR;

	*out = ((char *)newobj + sizeof(struct object_header));

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

		kmem_cache_free(obj_class.cache, hdr);
	}
}

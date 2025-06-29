#include <arch/amd64/smp.h>

#include <core/object.h>
#include <core/sched.h>
#include <core/syscall.h>
#include <core/capability.h>

#include <aria/debug.h>
#include <aria/base.h>
#include <aria/vector.h>

void capability_table_init(struct capability_table *table)
{
	VECTOR_INIT(table->values, 1);
	table->bitmap.resizable = true;
	table->bitmap.size = 64;
}

struct capability_binding *capability_lookup(struct capability_table *table,
											 capability_t handle)
{
	if (table->bitmap.size < (int)handle || !table->bitmap.data) {
		return NULL;
	}

	// Corresponding bit is set, we got a handle
	if (BIT_SET(table->bitmap.data, handle)) {
		return &table->values.data[handle];
	}

	return NULL;
}

int capability_create(struct capability_table *table, void *obj, uint8_t access,
					  capability_t *out_handle)
{
	int free_bit = -1;

	if (bitmap_alloc(&table->bitmap, &free_bit) == -1)
		return -1;

	*out_handle = free_bit;

	VECTOR_INDEX(table->values, (struct capability_binding){}, *out_handle);

	table->values.data[free_bit].access = access;
	table->values.data[free_bit].obj = obj;

	object_retain(obj);

	return 0;
}

int capability_destroy(struct capability_table *table, capability_t handle)
{
	struct capability_binding *binding;

	// This handle doesn't exist
	if ((binding = capability_lookup(table, handle)) == NULL)
		return -1;

	bitmap_free(&table->bitmap, handle);

	object_release(binding->obj);

	return 0;
}

// Creates an object and returns an handle to it
SYSCALL_DEFINE2(create_obj, uint8_t, class, uint8_t, access, {
	void *obj;
	capability_t out_handle;

	if (object_new(&obj, class) == -1)
		RETURN_ERROR;

	if (capability_create(CORE_LOCAL->current_thread->process->capability_table,
						  obj, access, &out_handle) == -1)
		RETURN_ERROR;

	return out_handle;
});

// Duplicates an handle with new permissions
SYSCALL_DEFINE2(duplicate_obj, capability_t, handle, uint8_t, access, {
	struct capability_binding *binding = capability_lookup(
		CORE_LOCAL->current_thread->process->capability_table, handle);
	capability_t out_handle;

	if (!binding)
		RETURN_ERROR;

	// FIXME: handle the case where we try creating SEND-ONCE when we have SEND
	// rights
	if (!(binding->access & access))
		RETURN_ERROR;

	object_retain(binding->obj);

	if (capability_create(CORE_LOCAL->current_thread->process->capability_table,
						  binding->obj, access, &out_handle) == -1)
		RETURN_ERROR;

	return out_handle;
});

// Destroys an handle
SYSCALL_DEFINE1(destroy_obj, capability_t, handle, {
	struct capability_binding *binding = capability_lookup(
		CORE_LOCAL->current_thread->process->capability_table, handle);

	if (!binding)
		RETURN_ERROR;

	if (capability_destroy(
			CORE_LOCAL->current_thread->process->capability_table, handle) ==
		-1)
		RETURN_ERROR;

	return 0;
});

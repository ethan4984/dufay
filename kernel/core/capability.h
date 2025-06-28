#ifndef HANDLE_H_
#define HANDLE_H_

#include <aria/bitmap.h>
#include <aria/vector.h>
#include <aria/capability.h>

struct capability_binding {
	uint8_t access;
	void *obj;
};

struct capability_table {
	VECTOR(struct capability_binding) values;
	struct bitmap bitmap;
};

void capability_table_init(struct capability_table *table);

// Looks up an handle in the handle table and returns the associated pointer
struct capability_binding *capability_lookup(struct capability_table *table,
											 capability_t handle);

// Allocates an handle in the table
int capability_create(struct capability_table *table, void *obj, uint8_t access,
					  capability_t *out_handle);

// Destroys an handle
int capability_destroy(struct capability_table *table, capability_t handle);

#endif

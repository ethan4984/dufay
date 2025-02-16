#ifndef HANDLE_H_
#define HANDLE_H_

#include <fayt/bitmap.h>
#include <fayt/vector.h>
#include <fayt/handle.h>

struct handle_binding {
	uint8_t access;
	void *obj;
};

struct handle_table {
	VECTOR(struct handle_binding) values;
	struct bitmap bitmap;
};

void handle_table_init(struct handle_table *table);

// Looks up an handle in the handle table and returns the associated pointer
struct handle_binding *handle_lookup(struct handle_table *table,
									 handle_t handle);

// Allocates an handle in the table
int handle_create(struct handle_table *table, void *obj, uint8_t access,
				  handle_t *out_handle);

// Destroys an handle
int handle_destroy(struct handle_table *table, handle_t handle);

#endif

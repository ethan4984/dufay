#ifndef FAYT_HASH_H_
#define FAYT_HASH_H_

#include <stddef.h>
#include <stdint.h>

struct hash_table {
	void **keys;
	void **data;

	size_t capacity;
	size_t element_cnt;
};

int hash_table_push(struct hash_table*, void*, void*, size_t);
int hash_table_delete(struct hash_table*, void*, size_t);
int hash_table_destroy(struct hash_table*);

int hash_table_search(struct hash_table*, void*, size_t, void**);

#endif

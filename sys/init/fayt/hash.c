#include <fayt/slab.h>
#include <fayt/string.h>
#include <fayt/hash.h>
#include <fayt/debug.h>

static uint64_t fnv_hash(char *data, size_t byte_cnt) {
	uint64_t hash = 0xcbf29ce484222325;

	for(size_t i = 0; i < byte_cnt; i++) {
		hash ^= *(data + i);
		hash *= 0x100000001b3;
	}

	return hash;
}

int hash_table_search(struct hash_table *table, void *key, size_t key_size, void **ret) {
	if(table == NULL || key == NULL || ret == NULL) RETURN_ERROR;
	if(table->capacity == 0) return 0;

	uint64_t hash = fnv_hash(key, key_size);
	size_t index = hash & (table->capacity - 1);

	for(; index < table->capacity; index++) {
		if(table->keys[index] != NULL && memcmp(table->keys[index], key, key_size) == 0) {
			*ret = table->data[index];
			return 0;
		}
	}

	return -1;
}

int hash_table_push(struct hash_table *table, void *key, void *data, size_t key_size) {
	if(table == NULL || key == NULL) RETURN_ERROR;
	if(table->capacity == 0) {
		table->capacity = 16;

		table->data = alloc(table->capacity * sizeof(void*));
		if(table->data == NULL) RETURN_ERROR;

		table->keys = alloc(table->capacity * sizeof(void*));
		if(table->keys == NULL) RETURN_ERROR;
	}

	uint64_t hash = fnv_hash(key, key_size);
	size_t index = hash & (table->capacity - 1);

	for(; index < table->capacity; index++) {
		if(table->keys[index] == NULL || memcmp(table->keys[index], key, key_size) == 0) {
			void *key_copy = alloc(key_size);
			memcpy(key_copy, key, key_size);
			table->keys[index] = key_copy;
			table->data[index] = data;
			table->element_cnt++;
			return 0;
		}
	}

	table->capacity *= 2;

	table->data = realloc(table->data, table->capacity * sizeof(void*));
	if(table->data == NULL) RETURN_ERROR;

	table->keys = realloc(table->keys, table->capacity * sizeof(void*));
	if(table->keys == NULL) RETURN_ERROR;

	return hash_table_push(table, key, data, key_size);
}

int hash_table_delete(struct hash_table *table, void *key, size_t key_size) {
	if(table == NULL) RETURN_ERROR;
	if(table->capacity == 0) RETURN_ERROR;

	uint64_t hash = fnv_hash(key, key_size);
	size_t index = hash & (table->capacity - 1);

	for(; index < table->capacity; index++) {
		if(table->keys[index] != NULL && memcmp(table->keys[index], key, key_size) == 0) {
			table->keys[index] = NULL;
			table->data[index] = NULL;
			table->element_cnt--;
			return 0;
		}
	}

	RETURN_ERROR;
}

int hash_table_destroy(struct hash_table *table) {
	if(table == NULL) RETURN_ERROR;

	free(table->data);

	return 0;
}

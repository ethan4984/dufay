#include <fayt/slab.h>
#include <fayt/bitmap.h>
#include <fayt/string.h>
#include <fayt/debug.h>

int bitmap_alloc(struct bitmap *bitmap, int *ret) {
	if(bitmap == NULL || ret == NULL) RETURN_ERROR;
	if(bitmap->data == NULL) goto init;

	for(int i = 0; i < (bitmap->size * 8); i++) {
		if(BIT_TEST(bitmap->data, i) == 0) {
			BIT_SET(bitmap->data, i); *ret = i;
			return 0;
		}
	}

init:
	if(bitmap->data == NULL || bitmap->resizable == BITMAP_RESIZEABLE) {
		if(!bitmap->size) bitmap->size = 2;
		else bitmap->size *= 2;

		bitmap->data = realloc(bitmap->data, bitmap->size);
		if(bitmap->data == NULL) RETURN_ERROR;

		return bitmap_alloc(bitmap, ret);
	}

	return 0;
}

int bitmap_free(struct bitmap *bitmap, int index) {
	if(bitmap == NULL) RETURN_ERROR;
	if(index > (bitmap->size * 8)) return 0;

	BIT_CLEAR(bitmap->data, index);

	return 0;
}

int bitmap_dup(struct bitmap *bitmap, struct bitmap *dest) {
	if(bitmap == NULL || dest == NULL) RETURN_ERROR;

	dest->size = bitmap->size;
	dest->resizable = bitmap->resizable;
	dest->data = alloc(DIV_ROUNDUP(bitmap->size, 8));

	memcpy8(dest->data, bitmap->data, DIV_ROUNDUP(bitmap->size, 8));

	return 0;
}

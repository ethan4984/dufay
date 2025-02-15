#ifndef FAYT_VECTOR_H_
#define FAYT_VECTOR_H_

#include <fayt/slab.h>

#define VECTOR(TYPE) \
	struct { \
		TYPE *data; \
		size_t length; \
		size_t buffer_capacity; \
	}

#define VECTOR_INIT(THIS, SIZE) \
	(THIS).buffer_capacity = SIZE; \
	(THIS).data = realloc((THIS).data, (THIS).buffer_capacity * sizeof(*(THIS).data));

#define VECTOR_PUSH(THIS, ELEMENT) ({ \
	__label__ _ret; \
	if((THIS).data == NULL) { \
		VECTOR_INIT(THIS, 1); \
	} \
	size_t _cap = (THIS).length + 1; \
	if(_cap <= (THIS).buffer_capacity) { \
		goto _ret; \
	} \
	(THIS).buffer_capacity *= 2; \
	(THIS).data = realloc((THIS).data, (THIS).buffer_capacity * sizeof(*(THIS).data)); \
_ret: \
	(THIS).data[(THIS).length++] = ELEMENT; \
})

#define VECTOR_PEEK(THIS, ELEMENT) ({ \
	__label__ finish; \
	finish: \
})

#define VECTOR_INDEX(THIS, ELEMENT, INDEX) ({ \
	if((INDEX) > (THIS).buffer_capacity) { \
		(THIS).buffer_capacity += (INDEX) - (THIS).buffer_capacity; \
		(THIS).data = realloc((THIS.data), (THIS).buffer_capacity * sizeof(*(THIS).data)); \
	} \
	if((INDEX) > (THIS).length) { \
		(THIS).length += (INDEX) - (THIS).length; \
	} \
	(THIS).data[INDEX] = ELEMENT; \
})

#define VECTOR_REMOVE_BY_INDEX(THIS, INDEX) ({ \
	__label__ _ret; \
	if((THIS).length < (INDEX)) { \
		goto _ret; \
	} \
	for(size_t _i = (INDEX) + 1; _i < (THIS).length; _i++) { \
		(THIS).data[_i - 1] = (THIS).data[_i]; \
	} \
	(THIS).length--; \
_ret: \
})

#define VECTOR_REMOVE_BY_VALUE(THIS, VALUE) ({ \
	size_t _j = 0; \
	for(; _j < (THIS).length; _j++) { \
		if((THIS).data[_j] == VALUE) { \
			VECTOR_REMOVE_BY_INDEX(THIS, _j); \
			break; \
		} \
	} \
	_j; \
})

#define VECTOR_POP(THIS, RET) ({ \
	int _status = 0; \
	if((THIS).length <= 0) { \
		_status = -1; \
	} else { \
		RET = (THIS).data[--(THIS).length]; \
	} \
	_status; \
})

#define VECTOR_CLEAR(THIS) \
	free((THIS).data); \
	(THIS).length = 0; \
	(THIS).buffer_capacity = 0;

#endif

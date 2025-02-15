#include <fayt/stream.h>
#include <fayt/string.h>
#include <fayt/compiler.h>

static void sprint_write(struct stream_info*, char);

struct sprint_info {
	char *stream;
	int index;
};

struct stream_info sprint_stream_info = {
	.write = sprint_write
};

int strcmp(const char *str0, const char *str1) {
	for(size_t i = 0;; i++) {
		if(str0[i] != str1[i]) {
			return str0[i] - str1[i];
		}

		if(!str0[i]) {
			return 0;
		}
	}
}

int strncmp(const char *str0, const char *str1, size_t n) {
	for(size_t i = 0; i < n; i++) {
		if(str0[i] != str1[i]) {
			return str0[i] - str1[i];
		}

		if(!str0[i]) {
			return 0;
		}
	}

	return 0;
}

char *strcpy(char *dest, const char *src) {
	if(!src || !dest) {
		return dest;
	}

	size_t i = 0;

	for(; src[i]; i++) {
		dest[i] = src[i];
	}

	dest[i] = 0;

	return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
	size_t i = 0;

	for(; i < n && src[i]; i++) {
		dest[i] = src[i];
	}

	dest[i] = 0;

	return dest;
}

char *strchr(const char *str, char c) {
	while(*str++) {
		if(*str == c) {
			return (char*)str;
		}
	}
	return NULL;
}

int memcmp(const char *str0, const char *str1, size_t n) {
	for(size_t i = 0; i < n; i++) {
		if(str0[i] != str1[i]) {
			return str0[i] - str1[i];
		}
	}

	return 0;
}

void memcpy(void *dest, const void *src, size_t n) {
	memcpy8(dest, src, n);
}

void memset(void *src, int data, size_t n) {
	memset8(src, data, n);
}

void sprint(char *str, ...) {
	va_list arg;
	va_start(arg, str);

	struct sprint_info sprint_info = {
		.stream = str, 
		.index = 0
	};

	spinlock(&sprint_stream_info.lock);

	sprint_stream_info.private = &sprint_info;
	stream_print(&sprint_stream_info, va_arg(arg, char*), arg);

	spinrelease(&sprint_stream_info.lock);

	va_end(arg);
}

static void sprint_write(struct stream_info *stream, char c) {
	if(unlikely(stream == NULL)) return;

	struct sprint_info *info = stream->private;
	if(unlikely(info == NULL)) return;

	info->stream[info->index++] = c;
}

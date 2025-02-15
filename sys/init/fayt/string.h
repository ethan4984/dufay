#ifndef FAYT_STRING_H_
#define FAYT_STRING_H_

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define DIV_ROUNDUP(a, b) (((a) + ((b) - 1)) / (b))
#define ALIGN_UP(a, b) (DIV_ROUNDUP(a, b) * b)
#define LENGTHOF(a) (sizeof(a) / sizeof(a[0]))
#define ABS(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))
#define BIT_SET(a, b) ((a)[(b) / 8] |= (1 << ((b) % 8)))
#define BIT_CLEAR(a, b) ((a)[(b) / 8] &= ~(1 << ((b) % 8)))
#define BIT_TEST(a, b) (((a)[(b) / 8] >> ((b) % 8)) & 0x1)

static inline void memset8(uint8_t *src, uint8_t data, size_t n) {
	for(size_t i = 0; i < n; i++) {
		*src++ = data;
	}
}

static inline void memset16(uint16_t *src, uint16_t data, size_t n) {
	for(size_t i = 0; i < n; i++) {
		*src++ = data;
	}
}

static inline void memset32(uint32_t *src, uint32_t data, size_t n) {
	for(size_t i = 0; i < n; i++) {
		*src++ = data;
	}
}

static inline void memset64(uint64_t *src, uint64_t data, size_t n) {
	for(size_t i = 0; i < n; i++) {
		*src++ = data;
	}
}

static inline void memcpy8(uint8_t *dest, const uint8_t *src, size_t n) {
	for(size_t i = 0; i < n; i++) {
		dest[i] = src[i];
	}
}

static inline void memcpy16(uint16_t *dest, const uint16_t *src, size_t n) {
	for(size_t i = 0; i < n; i++) {
		*dest++ = *src++;
	}
}

static inline void memcpy32(uint32_t *dest, const uint32_t *src, size_t n) {
	for(size_t i = 0; i < n; i++) {
		*dest++ = *src++;
	}
}

static inline void memcpy64(uint64_t *dest, const uint64_t *src, size_t n) {
	for(size_t i = 0; i < n; i++) {
		*dest++ = *src++;
	}
}

static inline size_t strlen(const char *str) {
	size_t len = 0;
	while(str[len]) len++;
	return len;
}

static inline size_t abs(int64_t n) {
	return (n < 0) ? -n : n;
}

static inline int pow2_roundup(int n) {
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;
	return n;
}

int strcmp(const char *str0, const char *str1);
int strncmp(const char *str0, const char *str1, size_t n);
int memcmp(const char *str0, const char *str, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strchr(const char *str, char c);
void memcpy(void *dest, const void *src, size_t n);
void memset(void *src, int data, size_t n);
void sprint(char *str, ...);

#endif

#ifndef CORE_DEBUG_H_
#define CORE_DEBUG_H_

#include <stdint.h>

void print(const char *str, ...);
void panic(const char *str, ...);
void stacktrace(uint64_t *rbp);
void print_unlocked(const char *str, ...);

#define _STRINGIFY(X) #X
#define STRINGIFY(X) _STRINGIFY(X)

#define ASSERT(EXPR)                                            \
	({                                                          \
		if (!(EXPR)) {                                          \
			panic("Assertion \"" #EXPR "\" failed at " __FILE__ \
				  ":" STRINGIFY(__LINE__));                     \
		}                                                       \
	})

#endif

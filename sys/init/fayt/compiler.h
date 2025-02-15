#ifndef FAYT_COMPILER_H_
#define FAYT_COMPILER_H_

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

#endif

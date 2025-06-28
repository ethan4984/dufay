#ifndef CORE_CLOCK_H_
#define CORE_CLOCK_H_
#include <stdint.h>

#define NANOSECONDS_PER_SECOND (1000000000UL)

typedef uint64_t nanoseconds_t;

void hardclock();

#endif

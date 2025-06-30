#ifndef CORE_SCHED_IMPL_H_
#define CORE_SCHED_IMPL_H_

#if defined(CONFIG_SCHED_ULE)
#include "ule.h"
#elif defined(CONFIG_SCHED_RR)
#include "rr.h"
#endif

#endif
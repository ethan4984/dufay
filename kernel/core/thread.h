#ifndef CORE_THREAD_H_
#define CORE_THREAD_H_
#include <core/sched.h>

void thread_switch(struct thread *cur, struct thread *next);

void thread_load(struct thread *td);

#endif

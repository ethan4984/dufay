#ifndef CORE_MUTEX_H_
#define CORE_MUTEX_H_
#include <core/wait.h>

struct mutex {
	struct dispatch_header hdr;
	struct thread *owner;
};

void mutex_init(struct mutex *mutex, const char *name);
void mutex_lock(struct mutex *mutex, nanoseconds_t timeout);
void mutex_unlock(struct mutex *mutex);

#endif

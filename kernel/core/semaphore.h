#ifndef CORE_SEMAPHORE_H_
#define CORE_SEMAPHORE_H_

#include <core/wait.h>

struct semaphore {
	struct dispatch_header hdr;
};

void semaphore_init(struct semaphore *sem, const char *name, int count);
void semaphore_acquire(struct semaphore *sem);
void semaphore_release(struct semaphore *sem);

#endif

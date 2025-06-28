#include <core/wait.h>
#include <arch/amd64/smp.h>
#include <core/lock.h>
#include <core/mutex.h>

void mutex_init(struct mutex *mutex, const char *name)
{
	dispatch_object_init(&mutex->hdr, DISPATCH_SYNCHRONIZATION, name);
	mutex->hdr.signaled_count = 1;
}

void mutex_lock(struct mutex *mutex, nanoseconds_t timeout)
{
	wait_one(&mutex->hdr, timeout);

	mutex->owner = CORE_LOCAL->current_thread;
}

void mutex_unlock(struct mutex *mutex)
{
	ipl_t ipl = spinlock_acquire(&mutex->hdr.lock);

	mutex->hdr.signaled_count = 1;

	mutex->owner = try_satisfy_dispatch_object(&mutex->hdr);

	spinlock_release(&mutex->hdr.lock, ipl);
}

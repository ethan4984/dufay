#include <core/wait.h>
#include <arch/x86/cpu.h>
#include <arch/x86/smp.h>
#include <core/lock.h>
#include <core/mutex.h>

void mutex_init(struct mutex *mutex, const char *name)
{
	dispatch_object_init(&mutex->hdr, DISPATCH_SYNCHRONIZATION, name);
	mutex->hdr.signaled_count = 1;
}

void mutex_lock(struct mutex *mutex)
{
	wait_one(&mutex->hdr, 0);

	mutex->owner = CORE_LOCAL->current_thread;
}

void mutex_unlock(struct mutex *mutex)
{
	spinlock_irqsave(&mutex->hdr.lock);

	mutex->hdr.signaled_count = 1;

	mutex->owner = try_satisfy_dispatch_object(&mutex->hdr);

	spinrelease_irqsave(&mutex->hdr.lock);
}

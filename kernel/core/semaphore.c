#include <core/semaphore.h>
#include <arch/x86/smp.h>
#include <core/wait.h>
#include <core/lock.h>

void semaphore_init(struct semaphore *sem, const char *name, int count)
{
	dispatch_object_init(&sem->hdr, DISPATCH_SYNCHRONIZATION, name);
	sem->hdr.signaled_count = count;
}

void semaphore_acquire(struct semaphore *sem)
{
	wait_one(&sem->hdr, 0);
}

void semaphore_release(struct semaphore *sem)
{
	spinlock_irqsave(&sem->hdr.lock);

	sem->hdr.signaled_count++;

	try_satisfy_dispatch_object(&sem->hdr);

	spinrelease_irqsave(&sem->hdr.lock);
}

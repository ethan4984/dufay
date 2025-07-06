#ifndef LOCK_H_
#define LOCK_H_

#include <arch/port.h>
#include <aria/lock.h>
#include <core/ipl.h>

static inline ipl_t spinlock_acquire(struct spinlock *spinlock)
{
	ipl_t oldipl = ipldispatch();
	//spinlock->last_acq = (uintptr_t)__builtin_return_address(0);
	raw_spinlock(spinlock);
	return oldipl;
}

static inline ipl_t spinlock_acquire_at(struct spinlock *spinlock, ipl_t ipl)
{
	ipl_t oldipl = ipl_raise(ipl);
	//spinlock->last_acq = (uintptr_t)__builtin_return_address(0);
	raw_spinlock(spinlock);
	return oldipl;
}

static inline void spinlock_release(struct spinlock *spinlock, ipl_t ipl)
{
	raw_spinrelease(spinlock);
	ipl_lower(ipl);
}

static inline void spinlock_irqsave(struct spinlock *spinlock)
{
	spinlock->interrupts = arch_interrupt_state();
	arch_disable_interrupts();

	//	spinlock->last_acq = (uintptr_t)__builtin_return_address(0);

	raw_spinlock(spinlock);
}

static inline void spinrelease_irqsave(struct spinlock *spinlock)
{
	raw_spinrelease(spinlock);
	if (spinlock->interrupts)
		arch_enable_interrupts();
	else
		arch_disable_interrupts();
}

#endif

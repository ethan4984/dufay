#ifndef LOCK_H_
#define LOCK_H_

#include <arch/port.h>
#include <fayt/lock.h>

static inline void spinlock_irqsave(struct spinlock *spinlock)
{
	spinlock->interrupts = arch_interrupt_state();
	arch_disable_interrupts();

	raw_spinlock(&spinlock->lock);
}

static inline void spinrelease_irqsave(struct spinlock *spinlock)
{
	raw_spinrelease(&spinlock->lock);
	if (spinlock->interrupts)
		arch_enable_interrupts();
	else
		arch_disable_interrupts();
}

#endif

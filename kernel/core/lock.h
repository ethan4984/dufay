#ifndef LOCK_H_
#define LOCK_H_

#include <aria/lock.h>

static inline void spinlock_irqsave(struct spinlock *spinlock)
{
	spinlock->interrupts = get_interrupt_state();
	__asm__ volatile("cli");
	raw_spinlock(&spinlock->lock);
}

static inline void spinrelease_irqsave(struct spinlock *spinlock)
{
	raw_spinrelease(&spinlock->lock);
	if (spinlock->interrupts)
		__asm__ volatile("sti");
	else
		__asm__ volatile("cli");
}

#endif

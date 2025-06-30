#ifndef CORE_IPL_H_
#define CORE_IPL_H_
#include <arch/port.h>
#include <stdint.h>

/*
 * Raise interrupt priority level to `ipl` and return the previous value.
 */
ipl_t ipl_raise(ipl_t ipl);

/*
 * Lower interrupt priority level to `ipl`.
 */
void ipl_lower(ipl_t ipl);

/*
 * Returns the current interrupt priority level.
 */
ipl_t ipl_get();

void dispatch_software_interrupts(ipl_t newipl);

bool is_softint_pending(struct cpu_local *cpu, ipl_t ipl);
void clear_softint_pending(struct cpu_local *cpu, ipl_t ipl);
void set_softint_pending(struct cpu_local *cpu, ipl_t ipl);

#define ipldispatch() ipl_raise(IPL_DISPATCH)
#define ipldevice() ipl_raise(IPL_DEVICE)

#endif

#include <arch/x86/smp.h>

#include <core/scheduler.h>
#include <core/syscall.h>

#include <core/debug.h>

#include <fayt/string.h>
#include <fayt/compiler.h>

#define SYSRET(RET, ERROR) ({ \
	CORE_LOCAL->error = ERROR; \
	ERROR ? ERROR : RET; \
})

struct syscall_handle {
	int (*handler)(struct registers*);
};

extern int syscall_log(struct registers*); 
extern int syscall_portal(struct registers*);
extern int syscall_yield(struct registers*);
extern int syscall_notification_action(struct registers*);
extern int syscall_notification_define_stack(struct registers*);
extern int syscall_notification_return(struct registers*);
extern int syscall_notification_unmute(struct registers*);
extern int syscall_notification_mute(struct registers*);

static struct syscall_handle syscall_handles[] = {
	{ .handler = syscall_log }, // 0
	{ .handler = syscall_portal }, // 1
	{ .handler = syscall_yield }, // 2
	{ .handler = syscall_notification_action }, // 3
	{ .handler = syscall_notification_define_stack }, // 4
	{ .handler = syscall_notification_return }, // 5
	{ .handler = syscall_notification_mute }, // 6
	{ .handler = syscall_notification_unmute }, // 7
	{ .handler = syscall_notification_mute } // 8
};

uint64_t syscall_handler(struct registers *regs) {
	int syscall_index = regs->rax;

	if(syscall_index >= LENGTHOF(syscall_handles)) {
		print("SYSCALL: unknown index %x\n", syscall_index);
		return SYSRET(-1, 0);
	}

	if(unlikely(CORE_LOCAL->current_context == NULL)) panic("dufay: critical error\n");
	else if(CORE_LOCAL->current_context->comms.sysperm & (1 << syscall_index))
	return SYSRET(-1, 0);

	int error;

	if(syscall_handles[syscall_index].handler) {
		error = syscall_handles[syscall_index].handler(regs);
	} else {
		print("SYSCALL: handler not properly initialised\n");
		return SYSRET(-1, 0);
	}

	return SYSRET(error ? error : 0, error);
}

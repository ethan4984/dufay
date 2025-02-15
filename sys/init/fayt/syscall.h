#ifndef FAYT_SYSCALL_H_
#define FAYT_SYSCALL_H_

#include <stdint.h>
#include <stddef.h>

constexpr int SYSCALL_LOG = 0;
constexpr int SYSCALL_PORTAL = 1;
constexpr int SYSCALL_ARCHCTL = 2;
constexpr int SYSCALL_NOTIFICATION_ACTION = 3;
constexpr int SYSCALL_NOTIFICATION_DEFINE_STACK = 4;
constexpr int SYSCALL_NOTIFICATION_RETURN = 5;
constexpr int SYSCALL_NOTIFICATION_MUTE = 6;
constexpr int SYSCALL_NOTIFICATION_UNMUTE = 7;
constexpr int SYSCALL_NOTIFICATION_BUILD = 8;
constexpr int SYSCALL_NOTIFICATION_BROADCAST = 9;
constexpr int SYSCALL_SERVER_ACTIVATE = 10;
constexpr int SYSCALL_NOTIFICATION_WAIT = 11;
constexpr int SYSCALL_NOTIFICATION_DESTROY = 12;
constexpr int SYSCALL_IRQ_CORTEX_INSTANTIATE = 13;
constexpr int SYSCALL_IRQ_CORTEX_ANCHOR = 14;

struct syscall_response {
	int ret; 
	int code;
};

#ifdef SYSCALL_INTERRUPT

#define SYSCALL0(NUM) ({ \
	struct syscall_response _response; \
	__asm__ volatile ("int $0x80" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM) \
					: "memory"); \
	_response; \
})

#define SYSCALL1(NUM, ARG0) ({ \
	struct syscall_response _response; \
	__asm__ volatile ("int $0x80" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0) \
					: "memory"); \
	_response; \
})

#define SYSCALL2(NUM, ARG0, ARG1) ({ \
	struct syscall_response _response; \
	__asm__ volatile ("int $0x80" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1) \
					: "memory"); \
	_response; \
})

#define SYSCALL3(NUM, ARG0, ARG1, ARG2) ({ \
	struct syscall_response _response; \
	__asm__ volatile ("int $0x80" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1), "d"(ARG2) \
					: "memory"); \
	_response; \
})

#define SYSCALL4(NUM, ARG0, ARG1, ARG2, ARG3) ({ \
	struct syscall_response _response; \
	register uint64_t arg3 __asm__("r10") = (uint64_t)ARG3; \
	__asm__ volatile ("int $0x80" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1), "d"(ARG2), "r"(arg3) \
					: "memory"); \
	_response; \
})

#define SYSCALL5(NUM, ARG0, ARG1, ARG2, ARG3, ARG4) ({ \
	register uint64_t arg3 __asm__("r10") = (uint64_t)ARG3; \
	register uint64_t arg4 __asm__("r8") = (uint64_t)ARG4; \
	__asm__ volatile ("int $0x80" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1), "d"(ARG2), \
					"r"(arg3), "r"(arg4) \
					: "memory"); \
})

#define SYSCALL6(NUM, ARG0, ARG1, ARG2, ARG3, ARG4, ARG5) ({ \
	register uint64_t arg3 __asm__("r10") = (uint64_t)ARG3; \
	register uint64_t arg4 __asm__("r8")	= (uint64_t)ARG4; \
	register uint64_t arg5 __asm__("r9")	= (uint64_t)ARG5; \
	__asm__ volatile ("int $0x80" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1), "d"(ARG2), \
					"r"(arg3), "r"(arg4), "r"(arg5) \
					: "memory"); \
})

#else

#define SYSCALL0(NUM) ({ \
	struct syscall_response _response; \
	__asm__ volatile ("syscall" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM) \
					: "rcx", "r11", "memory"); \
	_response; \
})

#define SYSCALL1(NUM, ARG0) ({ \
	struct syscall_response _response; \
	__asm__ volatile ("syscall" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0) \
					: "rcx", "r11", "memory"); \
	_response; \
})

#define SYSCALL2(NUM, ARG0, ARG1) ({ \
	struct syscall_response _response; \
	__asm__ volatile ("syscall" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1) \
					: "rcx", "r11", "memory"); \
	_response; \
})

#define SYSCALL3(NUM, ARG0, ARG1, ARG2) ({ \
	struct syscall_response _response; \
	__asm__ volatile ("syscall" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1), "d"(ARG2) \
					: "rcx", "r11", "memory"); \
	_response; \
})

#define SYSCALL4(NUM, ARG0, ARG1, ARG2, ARG3) ({ \
	struct syscall_response _response; \
	register uint64_t arg3 __asm__("r10") = (uint64_t)ARG3; \
	__asm__ volatile ("syscall" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1), "d"(ARG2), \
					"r"(arg3) \
					: "rcx", "r11", "memory"); \
	_response; \
})

#define SYSCALL5(NUM, ARG0, ARG1, ARG2, ARG3, ARG4) ({ \
	register uint64_t arg3 __asm__("r10") = (uint64_t)ARG3; \
	register uint64_t arg4 __asm__("r8") = (uint64_t)ARG4; \
	__asm__ volatile ("syscall" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1), "d"(ARG2), \
					"r"(arg3), "r"(arg4) \
					: "rcx", "r11", "memory"); \
})

#define SYSCALL6(NUM, ARG0, ARG1, ARG2, ARG3, ARG4, ARG5) ({ \
	register uint64_t arg3 __asm__("r10") = (uint64_t)ARG3; \
	register uint64_t arg4 __asm__("r8")	= (uint64_t)ARG4; \
	register uint64_t arg5 __asm__("r9")	= (uint64_t)ARG5; \
	__asm__ volatile ("syscall" \
					: "=a"(_response.ret), "=d"(_response.code) \
					: "a"(NUM), "D"(ARG0), "S"(ARG1), "d"(ARG2), \
					"r"(arg3), "r"(arg4), "r"(arg5) \
					: "rcx", "r11", "memory"); \
})

#endif

#endif

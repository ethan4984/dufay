#ifndef CORE_SYSCALL_H_
#define CORE_SYSCALL_H_

#define SYSCALL_CONSTRUCT_ERROR(RET, ERROR) (int)((RET) | (ERROR >> 32))

#if defined(__amd64__)
#define SYSCALL_DEFINE0(NAME, BODY)        \
	int syscall_##NAME(struct registers *) \
	{                                      \
		BODY;                              \
		return 0;                          \
	}

#define SYSCALL_DEFINE1(NAME, TYPE0, ARG0, BODY) \
	int syscall_##NAME(struct registers *regs)   \
	{                                            \
		TYPE0 ARG0 = (TYPE0)regs->rdi;           \
		BODY;                                    \
		return 0;                                \
	}

#define SYSCALL_DEFINE2(NAME, TYPE0, ARG0, TYPE1, ARG1, BODY) \
	int syscall_##NAME(struct registers *regs)                \
	{                                                         \
		TYPE0 ARG0 = (TYPE0)regs->rdi;                        \
		TYPE1 ARG1 = (TYPE1)regs->rsi;                        \
		BODY;                                                 \
		return 0;                                             \
	}

#define SYSCALL_DEFINE3(NAME, TYPE0, ARG0, TYPE1, ARG1, TYPE2, ARG2, BODY) \
	int syscall_##NAME(struct registers *regs)                             \
	{                                                                      \
		TYPE0 ARG0 = (TYPE0)regs->rdi;                                     \
		TYPE1 ARG1 = (TYPE1)regs->rsi;                                     \
		TYPE2 ARG2 = (TYPE2)regs->rdx;                                     \
		BODY;                                                              \
		return 0;                                                          \
	}

#define SYSCALL_DEFINE4(NAME, TYPE0, ARG0, TYPE1, ARG1, TYPE2, ARG2, TYPE3, \
						ARG3, BODY)                                         \
	int syscall_##NAME(struct registers *regs)                              \
	{                                                                       \
		TYPE0 ARG0 = (TYPE0)regs->rdi;                                      \
		TYPE1 ARG1 = (TYPE1)regs->rsi;                                      \
		TYPE2 ARG2 = (TYPE2)regs->rdx;                                      \
		TYPE3 ARG3 = (TYPE3)regs->r10;                                      \
		BODY;                                                               \
		return 0;                                                           \
	}

#define SYSCALL_DEFINE5(NAME, TYPE0, ARG0, TYPE1, ARG1, TYPE2, ARG2, TYPE3, \
						ARG3, TYPE4, ARG4, BODY)                            \
	int syscall_##NAME(struct registers *regs)                              \
	{                                                                       \
		TYPE0 ARG0 = (TYPE0)regs->rdi;                                      \
		TYPE1 ARG1 = (TYPE1)regs->rsi;                                      \
		TYPE2 ARG2 = (TYPE2)regs->rdx;                                      \
		TYPE3 ARG3 = (TYPE3)regs->r10;                                      \
		TYPE4 ARG4 = (TYPE4)regs->r8;                                       \
		BODY;                                                               \
		return 0;                                                           \
	}

#else
/* stub */
#define SYSCALL_DEFINE0(NAME, BODY)            \
	int syscall_##NAME(struct registers *regs) \
	{                                          \
		BODY;                                  \
		return 0;                              \
	}

#define SYSCALL_DEFINE1(NAME, TYPE0, ARG0, BODY) \
	int syscall_##NAME(struct registers *regs)   \
	{                                            \
		TYPE0 ARG0;                              \
		BODY;                                    \
		return 0;                                \
	}

#define SYSCALL_DEFINE2(NAME, TYPE0, ARG0, TYPE1, ARG1, BODY) \
	int syscall_##NAME(struct registers *regs)                \
	{                                                         \
		TYPE0 ARG0;                                           \
		TYPE1 ARG1;                                           \
		BODY;                                                 \
		return 0;                                             \
	}

#define SYSCALL_DEFINE3(NAME, TYPE0, ARG0, TYPE1, ARG1, TYPE2, ARG2, BODY) \
	int syscall_##NAME(struct registers *regs)                             \
	{                                                                      \
		TYPE0 ARG0;                                                        \
		TYPE1 ARG1;                                                        \
		TYPE2 ARG2;                                                        \
		BODY;                                                              \
		return 0;                                                          \
	}

#define SYSCALL_DEFINE4(NAME, TYPE0, ARG0, TYPE1, ARG1, TYPE2, ARG2, TYPE3, \
						ARG3, BODY)                                         \
	int syscall_##NAME(struct registers *regs)                              \
	{                                                                       \
		TYPE0 ARG0;                                                         \
		TYPE1 ARG1;                                                         \
		TYPE2 ARG2;                                                         \
		TYPE3 ARG3;                                                         \
		BODY;                                                               \
		return 0;                                                           \
	}

#define SYSCALL_DEFINE5(NAME, TYPE0, ARG0, TYPE1, ARG1, TYPE2, ARG2, TYPE3, \
						ARG3, TYPE4, ARG4, BODY)                            \
	int syscall_##NAME(struct registers *regs)                              \
	{                                                                       \
		TYPE0 ARG0;                                                         \
		TYPE1 ARG1;                                                         \
		TYPE2 ARG2;                                                         \
		TYPE3 ARG3;                                                         \
		TYPE4 ARG4;                                                         \
		BODY;                                                               \
		return 0;                                                           \
	}

#endif

#endif

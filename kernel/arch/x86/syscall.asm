global syscall_main
global syscall_main_int

extern syscall_handler

syscall_main:
	swapgs

	mov qword [gs:8], rsp ; save user stack
	mov rsp, qword [gs:0] ; restore kernel stack

	sti

	push rcx ; rip
	push r11 ; rflags

	push 0x3b ; ss
	push qword [gs:8] ; rsp
	push r11 ; rflags
	push 0x43 ; cs
	push rcx ; rip

	push 0
	push 0

	push rax
	push rbx
	push rcx
	push rdx
	push rbp
	push rdi
	push rsi
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	mov rdi, rsp
	call syscall_handler

	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rsi
	pop rdi
	pop rbp
	pop rdx
	pop rcx
	pop rbx
	pop rax
	add rsp, 56

	pop r11 ; rfla
	pop rcx ; rip

	cli

	mov rdx, qword [gs:16] ; error
	mov rsp, qword [gs:8] ; user stack

	swapgs

	o64 sysret

;syscall_main_int:
;	cld
;
;	push 0
;	push 128
;
;	push rax
;	push rbx
;	push rcx
;	push rdx
;	push rbp
;	push rdi
;	push rsi
;	push r8
;	push r9
;	push r10
;	push r11
;	push r12
;	push r13
;	push r14
;	push r15
;
;	mov rdi, rsp
;	call syscall_handler
;
;	pop r15
;	pop r14
;	pop r13
;	pop r12
;	pop r11
;	pop r10
;	pop r9
;	pop r8
;	pop rsi
;	pop rdi
;	pop rbp
;	pop rdx
;	pop rcx
;	pop rbx
;	pop rax
;	add rsp, 16
;	iretq

global amd64_context_switch

amd64_context_switch:
    ; Save callee-saved registers
    push r15
    push r14
    push r13
    push r12
    push rbx
    push rbp

    mov [rdi+56], rsp ; Save stack
    mov rsp, [rsi+56] ; Load next thread's stack
    mov rax, rdi   ; Save prev thread in rax

    xor rbx, rbx            
    xchg [rdi+72], rbx   ; Unlock previous thread

    ; Restore next thread's registers
    pop rbp
    pop rbx
    pop r12
    pop r13
    pop r14
    pop r15

    ret


global amd64_load_context
amd64_load_context:
    mov rsp, [rdi+56] ; Load thread's stack
    xor rax, rax

    ; Restore next thread's registers
    pop rbp
    pop rbx
    pop r12
    pop r13
    pop r14
    pop r15

    ret


global _amd64_thread_entry
_amd64_thread_entry:
mov rdi, r12
mov rsi, rax
extern amd64_thread_entry
call amd64_thread_entry

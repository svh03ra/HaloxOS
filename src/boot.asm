; Copyright Svh03ra (C) 2026, All rights reserved.
[BITS 32]

section .multiboot
align 4
    dd 0x1BADB002
    dd 0x00000007
    dd -(0x1BADB002 + 0x00000007)
    dd 0
    dd 640
    dd 480
    dd 32

section .text
global start
extern kernel_main

start:
    cli
    mov esp, stack_top
    push ebx
    push eax
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

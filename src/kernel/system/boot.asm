; Copyright Svh03ra (C) 2026, All rights reserved
; This repository is licensed under the GNU General Public License.

[BITS 32]

section .multiboot
align 4
%ifndef HALOXOS_BOOT_SCREEN_WIDTH
%define HALOXOS_BOOT_SCREEN_WIDTH 640
%endif

%ifndef HALOXOS_BOOT_SCREEN_HEIGHT
%define HALOXOS_BOOT_SCREEN_HEIGHT 480
%endif

%ifndef HALOXOS_BOOT_SCREEN_DEPTH
%define HALOXOS_BOOT_SCREEN_DEPTH 0
%endif

MB_MAGIC equ 0x1BADB002
MB_FLAGS equ 0x00000007

    dd MB_MAGIC
    dd MB_FLAGS
    dd -(MB_MAGIC + MB_FLAGS)
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0
    dd HALOXOS_BOOT_SCREEN_WIDTH
    dd HALOXOS_BOOT_SCREEN_HEIGHT
    dd HALOXOS_BOOT_SCREEN_DEPTH

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

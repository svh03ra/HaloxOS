; Copyright Svh03ra (C) 2026, All rights reserved
; Source File: loader/boot.asm, zstd boot loader entry.
;
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

; Multiboot flags: 0x1 module align, 0x2 memory info, 0x4 video mode request.
;
; The loader is what GRUB actually boots, so the loader's header must carry
; the video-mode request the kernel used to send directly. GRUB programs
; the VBE mode from THIS header and fills the multiboot framebuffer fields,
; which the loader then passes through to the kernel untouched. Without
; this, GRUB reverts to text mode and the kernel loses its high-color
; linear framebuffer (the compressed kernel header is invisible to GRUB).
%if HALOXOS_BOOT_SCREEN_DEPTH == 4
MB_FLAGS equ 0x00000003
MB_MODE_TYPE equ 1
%else
MB_FLAGS equ 0x00000007
MB_MODE_TYPE equ 0
%endif

    dd MB_MAGIC
    dd MB_FLAGS
    dd -(MB_MAGIC + MB_FLAGS)
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0
    dd MB_MODE_TYPE
    dd HALOXOS_BOOT_SCREEN_WIDTH
    dd HALOXOS_BOOT_SCREEN_HEIGHT
    dd HALOXOS_BOOT_SCREEN_DEPTH

section .text
global loader_start
global jump_to_kernel
extern loader_main

loader_start:
    cli
    mov esp, loader_stack_top
    ; GRUB multiboot state: eax = magic, ebx = info pointer.
    ; Preserve both and hand them to the C entry.
    mov dword [saved_magic], eax
    mov dword [saved_mbi], ebx
    push ebx
    push eax
    call loader_main
.hang:
    cli
    hlt
    jmp .hang

; void jump_to_kernel(uint32_t entry, uint32_t magic, uint32_t mbi)
; cdecl: [esp+4]=entry, [esp+8]=magic, [esp+12]=mbi.
;
; The kernel entry expects the GRUB multiboot register state (EAX=magic,
; EBX=info pointer) per the multiboot specification, NOT C arguments on
; the stack. Restores the registers and jumps; the kernel switches to its
; own stack immediately.
jump_to_kernel:
    mov eax, [esp+8]
    mov ebx, [esp+12]
    jmp dword [esp+4]

section .data
global saved_magic
global saved_mbi
saved_magic:  dd 0
saved_mbi:    dd 0

section .bss
align 16
loader_stack_bottom:
    resb 16384
loader_stack_top:

; Mark the stack non-executable for the ELF linker.
section .note.GNU-stack noalloc noexec nowrite progbits

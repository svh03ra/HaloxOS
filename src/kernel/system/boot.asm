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

; Multiboot flags: 0x1 module align, 0x2 memory info, 0x4 video mode request.
;
; IMPORTANT: the graphics header's mode_type is independent from the depth.
; mode_type=0 means linear graphics and mode_type=1 means EGA text.  The
; previous code always emitted mode_type=1, which allowed GRUB to hand us an
; EGA/VGA text mode even for 8/16-bit builds. The kernel would then fall
; through to the native 640x480x4 VGA backend, making HALOXOS_CONFIG_SCREEN_BPP
; appear to be permanently stuck at 4bpp.
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
global start
extern kernel_main
; Bounds of the whole .bss, defined by linker.ld (see the note below).
extern __bss_start
extern __bss_end

; The zstd loader decompresses this kernel at its link address and jumps
; here with the original GRUB multiboot registers (eax = magic, ebx = info).
; GRUB never loaded this ELF, so .bss has to be zeroed by the kernel itself.
start:
    cli
    ; zero the whole .bss range
    mov ecx, __bss_start
.zero_bss:
    cmp ecx, __bss_end
    jae .bss_done
    mov dword [ecx], 0
    add ecx, 4
    jmp .zero_bss
.bss_done:
    mov esp, stack_top
    push ebx
    push eax
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .bss
; NOTE: __bss_start/__bss_end are defined by linker.ld around the whole
; .bss section (every kernel object plus this stack). Do not add local
; labels here - they would shadow the linker-provided bounds and the
; boot-time zeroing loop would only clear the stack.

stack_bottom:
    resb 16384
stack_top:

; Mark the stack non-executable for the ELF linker.
section .note.GNU-stack noalloc noexec nowrite progbits

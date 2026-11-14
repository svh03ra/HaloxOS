; Copyright Svh03ra (C) 2026, All rights reserved
; Source File: loader/efi/jump.asm, leave UEFI long mode for the 32-bit loader.
;
; This repository is licensed under the GNU General Public License.

; UEFI applications execute in 64-bit long mode, while HaloxOS's native
; loader is a 32-bit protected-mode Multiboot payload.  More importantly, a
; x64 firmware may place this PE image above 4 GiB.  A far transfer to a
; 32-bit compatibility segment then truncates EIP, so executing the transition
; code from the image itself can jump into unrelated low memory on modern
; laptops.  efi_enter_loader therefore copies the small, fully
; position-independent handoff below 4 GiB before it changes CS.
;
; The declaration uses EFIAPI, so every supported GCC target calls this entry
; with the Microsoft x64 convention:
;   ECX = loader entry, EDX = Multiboot info, R8D = low handoff page.
; The copied code needs no absolute reference to this PE image.  It derives the
; copied GDT address with RIP-relative LEA, patches its copied GDTR, switches
; to compatibility mode, disables paging and jumps to the 32-bit loader.

[BITS 64]

section .text
global efi_enter_loader

efi_enter_loader:
    cli
    cld

    ; Preserve the two low-address payload arguments before REP MOVSB borrows
    ; RDI/RSI.  Explicitly clear RDX's high half: the C type is UINT32 and the
    ; destination must remain a canonical low address even on a strict ABI.
    mov  r10d, ecx
    mov  r11d, edx
    mov  r8d, r8d
    mov  rdi, r8
    lea  rsi, [rel haloxos_low_handoff]
    mov  ecx, haloxos_low_handoff_end - haloxos_low_handoff
    rep  movsb

    mov  edi, r10d
    mov  esi, r11d
    jmp  r8

; ---------------------------------------------------------------------------
; Everything from here through haloxos_low_handoff_end is copied as one block.
; All references inside it are RIP-relative, so the same bytes run correctly at
; whatever low physical page AllocatePages supplied.
; ---------------------------------------------------------------------------
haloxos_low_handoff:
    ; The GDTR's base is an absolute value, so point its copied copy at the
    ; copied GDT before loading it.  No firmware memory is referenced after
    ; this point.
    lea  rax, [rel haloxos_gdt]
    mov  [rel haloxos_gdt_desc + 2], rax
    lgdt [rel haloxos_gdt_desc]

    ; Enter the 32-bit compatibility code segment.  Paging can only be turned
    ; off after CS.L is clear, which is why the far return comes first.
    mov  eax, 0x10
    push rax
    lea  rax, [rel haloxos_compat]
    push rax
    retfq

; Everything below runs in 32-bit compatibility mode.
[BITS 32]
haloxos_compat:
    mov  eax, cr0
    and  eax, 0x7FFFFFFF                 ; clear CR0.PG, leave long mode
    mov  cr0, eax

    ; Flat 32-bit protected mode.  loader_start installs its own stack soon
    ; afterwards; this one only has to be valid through the direct jump.
    mov  ax, 0x18
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x0007C000

    mov  eax, 0x2BADB002
    mov  ebx, esi
    jmp  edi

[BITS 64]
align 8
haloxos_gdt:
    dq 0x0000000000000000     ; 0x00: null
    dq 0x0000000000000000     ; 0x08: null
    dq 0x00CF9A000000FFFF     ; 0x10: 32-bit code
    dq 0x00CF92000000FFFF     ; 0x18: 32-bit data
haloxos_gdt_end:
haloxos_gdt_desc:
    dw haloxos_gdt_end - haloxos_gdt - 1
    dq 0                        ; patched at the copied handoff address
haloxos_low_handoff_end:

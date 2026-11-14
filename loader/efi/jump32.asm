; Copyright Svh03ra (C) 2026, All rights reserved
; Source File: loader/efi/jump32.asm, leave IA32 UEFI for the 32-bit loader.
;
; This repository is licensed under the GNU General Public License.

; The IA32 counterpart of jump.asm.  A 32-bit firmware already runs the loader
; in the mode the boot chain wants - flat 32-bit protected mode - so unlike the
; 64-bit path there is no long mode to come back out of, and the only thing left
; to undo is the firmware's paging.  The HaloxOS boot chain and kernel address
; physical memory directly, exactly as GRUB hands them over on a BIOS machine,
; so CR0.PG is cleared before the jump rather than a page table being installed.
;
; The descriptor table is installed even though the firmware's segments are
; already flat, and for the same reason jump.asm installs one: its numbering is
; the boot chain's contract.  The kernel's interrupt gates name selector 0x10
; and its data segments are 0x18 (src/kernel/system/interrupts.c), while IA32
; firmware numbers its own segments differently - EDK II puts code at 0x08 and
; data at 0x10 - so a gate delivered through the firmware's table would name a
; data descriptor and triple fault at the first interrupt.  Code at 0x10 and
; data at 0x18, as on the 64-bit path.

[BITS 32]

section .text
; The C object linked against this stub is ELF32, produced by "gcc -m32" (ld
; converts the pair to a PE image, the same way the 64-bit loader is built),
; and ELF carries no leading underscore on external names.  Both loaders
; therefore export the plain spelling - jump.asm for the x64 ABI and this one
; because -m32 targets ELF here, not COFF.  EFIAPI is cdecl on i386, so the
; three arguments arrive on the stack.
global efi_enter_loader

efi_enter_loader:
    cli

    ; The entry and info arguments are on the stack per cdecl, so they are read
    ; here, before the stack pointer is replaced further down.
    ; The third argument is a low trampoline page used only by jump.asm: an
    ; IA32 firmware is already below 4 GiB and does not need it.  The
    ; firmware's own SS still applies to these two loads, which is why they come
    ; before the descriptor table below is swapped.
    mov  edi, [esp + 4]        ; loader entry
    mov  esi, [esp + 8]        ; multiboot info block

    ; Nothing between this and the far jump may touch memory: the segments still
    ; hold the firmware's selectors, and those do not name anything in the table
    ; being installed.
    lgdt [haloxos_gdt_desc]

    ; A direct far jump rather than a pushed address and a far return: it needs
    ; no stack access, and the stack is still the firmware's.
    jmp  0x10:haloxos_flat

haloxos_flat:
    ; Data selectors from the new table.  SS is loaded with the rest because the
    ; stack pointer is set in the very next instruction - and interrupts are
    ; already off - so there is no window in which a stack access could use a
    ; stale selector.
    mov  ax, 0x18
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x0007C000

    ; Paging off: this is what turns the firmware's address space into the flat
    ; physical one the boot chain runs in.
    mov  eax, cr0
    and  eax, 0x7FFFFFFF
    mov  cr0, eax

    ; Multiboot register handoff: EAX = magic, EBX = info block.
    mov  eax, 0x2BADB002
    mov  ebx, esi
    jmp  edi

section .data
align 8
haloxos_gdt:
    dq 0x0000000000000000     ; 0x00: null
    dq 0x0000000000000000     ; 0x08: null, as in the bootloader's own table
    dq 0x00CF9A000000FFFF     ; 0x10: 32-bit code - the kernel's CS and gates
    dq 0x00CF92000000FFFF     ; 0x18: 32-bit data - the kernel's DS/ES/SS
haloxos_gdt_end:
haloxos_gdt_desc:
    dw haloxos_gdt_end - haloxos_gdt - 1
    dd haloxos_gdt

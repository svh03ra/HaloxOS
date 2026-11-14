; Copyright Svh03ra (C) 2026, All rights reserved
; This repository is licensed under the GNU General Public License.

[BITS 32]

extern keyboard_packet_from_isr

global idt_load
global irq0_stub
global irq1_stub
global irq9_stub
global irq10_stub
global irq11_stub
global irq12_stub
global irq_default_stub
global isr_default_stub
global isr0_stub
global isr1_stub
global isr2_stub
global isr3_stub
global isr4_stub
global isr5_stub
global isr6_stub
global isr7_stub
global isr8_stub
global isr9_stub
global isr10_stub
global isr11_stub
global isr12_stub
global isr13_stub
global isr14_stub
global isr15_stub
global isr16_stub
global isr17_stub
global isr18_stub
global isr19_stub
global isr20_stub
global isr21_stub
global isr22_stub
global isr23_stub
global isr24_stub
global isr25_stub
global isr26_stub
global isr27_stub
global isr28_stub
global isr29_stub
global isr30_stub
global isr31_stub
global cpu_halt_once

extern timer_tick_from_isr
extern mouse_packet_from_isr
extern cpu_exception_handler
extern acpi_sci_from_isr

idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

irq0_stub:
    pusha
    call timer_tick_from_isr
    mov al, 0x20
    out 0x20, al
    popa
    iretd

; IRQ1 (keyboard, including firmware USB-legacy emulation).  Reading port
; 0x60 here is mandatory: a controller with an unread output byte will not
; normally raise the next keyboard interrupt.  The C helper also routes a
; mis-delivered auxiliary byte correctly, because both PS/2 ports share the
; 8042 output buffer.
irq1_stub:
    pusha
    call keyboard_packet_from_isr
    mov al, 0x20
    out 0x20, al
    popa
    iretd

; IRQ12 (PS/2 mouse): drains the 8042 output queue in real time. Without
; this, packets wait in the queue until the next 60Hz poll_input() and a
; heavy render frame (~120ms) batch-applies them -> cursor teleport and
; felt "desync". Consuming here keeps latency to interrupt time (~1ms).
irq12_stub:
    pusha
    call mouse_packet_from_isr
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iretd

; IRQ9/IRQ10/IRQ11 (the ACPI SCI candidates, slave lines): the fixed
; power-button event arrives here when armed (power.c). The C handler
; reads and clears the PM1 status; the EOI is dual because every one of
; these lives on the slave PIC.
%macro IRQ_SCI 1
irq%1_stub:
    pusha
    call acpi_sci_from_isr
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iretd
%endmacro

IRQ_SCI 9
IRQ_SCI 10
IRQ_SCI 11

irq_default_stub:
    pusha
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iretd

isr_default_stub:
    iretd

%macro ISR_NOERR 1
isr%1_stub:
    cli
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
isr%1_stub:
    cli
    push dword %1
    jmp isr_common_stub
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31

; ---------------------------------------------------------------------
; Crash stack
;
; An exception arrives on whatever stack the interrupted code was using.
; On a stack fault, a deep recursion or a corrupted ESP that stack is
; exactly what cannot be trusted, and pushing the handler frame onto it
; faults again -> double fault -> reset, with no crash screen at all.
; The stub therefore copies the CPU frame onto this dedicated 16 KiB
; stack before the first C push, so the crash handler can always run.
;
; crash_old_esp keeps the interrupted stack pointer: the CPU frame is
; still there, and that is where the resume path returns to.
; ---------------------------------------------------------------------
section .bss
alignb 16
crash_stack_bottom:
    resb 16384
; The stack grows down, so ESP starts at the top of the reserved block.
global crash_stack_top
crash_stack_top:
crash_stack_active:
    resb 1
alignb 4
crash_old_esp:
    resd 1

; ---------------------------------------------------------------------
; crash_guard_call / crash_guard_restore
;
; int crash_guard_call(int (*paint)(void));
;
; Runs one crash-screen paint with a recovery point of its own. If the
; paint faults (a bad framebuffer, a stale boot pointer in the memory map,
; a broken graphics state), the exception handler jumps to .resume, which
; returns -1 to the caller instead of freezing on a half-drawn frame.
;
; All of that control flow lives here on purpose. A C setjmp-style branch
; depends on how the optimiser keeps the value of a second return, which is
; exactly the kind of subtlety a crash handler cannot afford: a wrong guess
; turns the fallback into an endless paint-fault loop.
; ---------------------------------------------------------------------
global crash_guard_active
crash_guard_active:
    resb 1
alignb 4
crash_guard_eip:
    resd 1
crash_guard_esp:
    resd 1
crash_guard_ebp:
    resd 1
crash_guard_ebx:
    resd 1
crash_guard_esi:
    resd 1
crash_guard_edi:
    resd 1

section .text
global crash_guard_call
global crash_guard_restore

crash_guard_call:
    push ebp
    push edi
    push esi
    push ebx
    mov [crash_guard_ebp], ebp
    mov [crash_guard_ebx], ebx
    mov [crash_guard_esi], esi
    mov [crash_guard_edi], edi
    mov [crash_guard_esp], esp
    mov dword [crash_guard_eip], .resume
    mov byte [crash_guard_active], 1
    mov eax, [esp + 20]             ; the paint function
    call eax
    mov byte [crash_guard_active], 0
    jmp .done

.resume:
    ; The paint faulted and the handler came back here. The registers and
    ; stack pointer are already restored, so this is a normal return - only
    ; EAX has to say that nothing was painted.
    mov byte [crash_guard_active], 0
    mov eax, -1
.done:
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret

; Called from the exception handler, never returns to it: resumes the
; crash_guard_call frame that armed the guard.
crash_guard_restore:
    mov ebp, [crash_guard_ebp]
    mov ebx, [crash_guard_ebx]
    mov esi, [crash_guard_esi]
    mov edi, [crash_guard_edi]
    mov esp, [crash_guard_esp]
    jmp [crash_guard_eip]

isr_common_stub:
    mov [crash_old_esp], esp
    cmp byte [crash_stack_active], 0
    jne .stack_ready
    mov esp, crash_stack_top
    mov byte [crash_stack_active], 1
.stack_ready:
    ; Reserve room for the copied CPU frame above the register block: the
    ; struct starts at gs and grows upward, so eflags must end up at the
    ; highest address.  The reservation happens before pusha so pusha still
    ; captures the interrupted EAX/ECX/EDX unmodified.
    sub esp, 20
    pusha
    ; pusha saved the crash-stack ESP; report the interrupted one instead
    mov eax, [crash_old_esp]
    mov [esp + 12], eax
    push ds
    push es
    push fs
    push gs
    ; ESP now points at gs, the first field of CpuExceptionFrame; fill the
    ; reserved words from the frame the CPU left on the interrupted stack.
    mov edx, [crash_old_esp]
    mov eax, [edx + 0]              ; vector
    mov [esp + 48], eax
    mov eax, [edx + 4]              ; error code
    mov [esp + 52], eax
    mov eax, [edx + 8]              ; eip
    mov [esp + 56], eax
    mov eax, [edx + 12]             ; cs
    mov [esp + 60], eax
    mov eax, [edx + 16]             ; eflags
    mov [esp + 64], eax
    push esp
    call cpu_exception_handler
    add esp, 4
    test eax, eax
    jz .hang
    ; Interrupts are off already. Resume: the original CPU frame is still
    ; on the interrupted stack; iretd returns to the instruction after the
    ; trap. Only the architectural traps (#BP / #OF) take this path.
    mov esp, [crash_old_esp]
    add esp, 8                      ; skip vector + error code
    mov byte [crash_stack_active], 0
    iretd

.hang:
    cli
    hlt
    jmp .hang

cpu_halt_once:
    sti
    hlt
    ret

; Mark the stack non-executable for the ELF linker.
section .note.GNU-stack noalloc noexec nowrite progbits

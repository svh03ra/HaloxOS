; Copyright Svh03ra (C) 2026, All rights reserved.
[BITS 32]

global idt_load
global irq0_stub
global irq_default_stub
global isr_default_stub
global cpu_halt_once

extern timer_tick_from_isr

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

irq_default_stub:
    pusha
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iretd

isr_default_stub:
    iretd

cpu_halt_once:
    sti
    hlt
    ret

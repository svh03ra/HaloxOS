// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: interrupts.c, interrupts handling.

// This repository is licensed under the GNU General Public License.

static void set_idt_gate(uint8_t index, void (*handler)(void), uint8_t type_attr) {
    uintptr_t address = (uintptr_t)handler;
    idt[index].offset_low = (uint16_t)(address & 0xFFFFu);
    idt[index].selector = 0x10;
    idt[index].zero = 0;
    idt[index].type_attr = type_attr;
    idt[index].offset_high = (uint16_t)((address >> 16) & 0xFFFFu);
}

static void init_pic(void) {
    uint8_t master_mask = 0xF8;
    uint8_t slave_mask = 0xEF;

    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();
    outb(0x21, master_mask);
    outb(0xA1, slave_mask);
}

static void init_pit(void) {
    uint16_t divisor = (uint16_t)(1193182u / TIMER_HZ);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));
}

static void init_interrupts(void) {
    IdtPointer pointer;
    void (*const exception_stubs[32])(void) = {
        isr0_stub, isr1_stub, isr2_stub, isr3_stub,
        isr4_stub, isr5_stub, isr6_stub, isr7_stub,
        isr8_stub, isr9_stub, isr10_stub, isr11_stub,
        isr12_stub, isr13_stub, isr14_stub, isr15_stub,
        isr16_stub, isr17_stub, isr18_stub, isr19_stub,
        isr20_stub, isr21_stub, isr22_stub, isr23_stub,
        isr24_stub, isr25_stub, isr26_stub, isr27_stub,
        isr28_stub, isr29_stub, isr30_stub, isr31_stub
    };

    for (int i = 0; i < 256; ++i) {
        set_idt_gate((uint8_t)i, isr_default_stub, 0x8E);
    }
    for (int i = 0; i < 32; ++i) {
        set_idt_gate((uint8_t)i, exception_stubs[i], 0x8E);
    }
    set_idt_gate(32, irq0_stub, 0x8E);
    for (int i = 33; i <= 47; ++i) {
        set_idt_gate((uint8_t)i, irq_default_stub, 0x8E);
    }

    pointer.limit = (uint16_t)(sizeof(idt) - 1);
    pointer.base = (uint32_t)(uintptr_t)&idt[0];

    idt_load(&pointer);
    init_pic();
    init_pit();
}

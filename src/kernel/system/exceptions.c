// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: exceptions.c, crash exception and etc.

// This repository is licensed under the GNU General Public License.

static const char *system_state_name(void) {
    switch (system_state) {
        case STATE_BOOT_MENU: return "Boot Menu";
        case STATE_BOOT_TERMINAL: return "Boot Terminal";
        case STATE_LOGIN: return "Login";
        case STATE_DESKTOP: return "Desktop";
        case STATE_SHUTDOWN: return "Shutdown";
        default: return "Unknown";
    }
}

static const char *cpu_exception_name(uint32_t vector) {
    static const char *names[32] = {
        "#DE Division Error",
        "#DB Debug",
        "NMI Interrupt",
        "#BP Breakpoint",
        "#OF Overflow",
        "#BR Bound Range Exceeded",
        "#UD Invalid Opcode",
        "#NM Device Not Available",
        "#DF Double Fault",
        "Coprocessor Segment Overrun",
        "#TS Invalid TSS",
        "#NP Segment Not Present",
        "#SS Stack-Segment Fault",
        "#GP General Protection Fault",
        "#PF Page Fault",
        "Reserved",
        "#MF x87 Floating-Point Exception",
        "#AC Alignment Check",
        "#MC Machine Check",
        "#XM SIMD Floating-Point Exception",
        "#VE Virtualization Exception",
        "#CP Control Protection Exception",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "#HV Hypervisor Injection Exception",
        "#VC VMM Communication Exception",
        "#SX Security Exception",
        "Reserved"
    };

    if (vector < 32) {
        return names[vector];
    }
    return "Unknown Exception";
}

static void serial_dump_cpu_exception(const char *name, const char *reason, const CpuExceptionFrame *frame) {
    if (!debug) {
        return;
    }

    serial_write_string("***** MACHINE CRASH!!! *****\n");
    serial_trace_begin("ERROR");
    serial_write_string(reason != NULL ? reason : "CPU exception");
    serial_write_string("\n");

    serial_trace_begin("ERROR");
    serial_write_string("Exception: ");
    serial_write_string(name);
    if (frame != NULL) {
        serial_write_string(" vector=");
        serial_write_uint(frame->vector);
        serial_write_string(" error=");
        serial_write_hex32(frame->error_code);
    }
    serial_write_string("\n");

    serial_trace_begin("INFO");
    serial_write_string("System State: ");
    serial_write_string(system_state_name());
    serial_write_string("\n");

    serial_trace_video_mode("Crash video state");

    if (frame == NULL) {
        return;
    }

    serial_write_string("[INFO]: Registers A: EAX=");
    serial_write_hex32(frame->eax);
    serial_write_string(" EBX=");
    serial_write_hex32(frame->ebx);
    serial_write_string(" ECX=");
    serial_write_hex32(frame->ecx);
    serial_write_string(" EDX=");
    serial_write_hex32(frame->edx);
    serial_write_string("\n");

    serial_write_string("[INFO]: Registers B: ESI=");
    serial_write_hex32(frame->esi);
    serial_write_string(" EDI=");
    serial_write_hex32(frame->edi);
    serial_write_string(" EBP=");
    serial_write_hex32(frame->ebp);
    serial_write_string(" ESP=");
    serial_write_hex32(frame->esp);
    serial_write_string("\n");

    serial_write_string("[INFO]: Control: EIP=");
    serial_write_hex32(frame->eip);
    serial_write_string(" CS=");
    serial_write_hex32(frame->cs);
    serial_write_string(" EFLAGS=");
    serial_write_hex32(frame->eflags);
    serial_write_string("\n");

    serial_write_string("[INFO]: Segments: DS=");
    serial_write_hex32(frame->ds);
    serial_write_string(" ES=");
    serial_write_hex32(frame->es);
    serial_write_string(" FS=");
    serial_write_hex32(frame->fs);
    serial_write_string(" GS=");
    serial_write_hex32(frame->gs);
    serial_write_string("\n");
}

void cpu_exception_handler(const CpuExceptionFrame *frame) {
    const char *reason = debug_forced_fault_reason;
    const char *name = frame != NULL ? cpu_exception_name(frame->vector) : "CPU Exception";

    serial_dump_cpu_exception(name, reason, frame);
    debug_forced_fault_reason = NULL;
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

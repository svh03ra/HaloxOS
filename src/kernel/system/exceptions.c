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

/* Fixed 8-digit hex (0xXXXXXXXX) so 32-bit values keep full width. */
static void bsod_append_hex32(char *buffer, size_t *len, size_t max_len, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";

    if (*len + 8 >= max_len) {
        return;
    }
    for (int shift = 28; shift >= 0; shift -= 4) {
        buffer[(*len)++] = digits[(value >> shift) & 0x0Fu];
    }
    buffer[*len] = '\0';
}

/*
 * Classic BSOD screen: solid blue background, white text. Only drawn when
 * the system crashed while a graphical framebuffer is active; the full
 * register dump always goes to the serial debugger trace regardless.
 */
static void render_bsod(const char *name, const CpuExceptionFrame *frame) {
    char line[TERM_LINE_LEN];
    size_t len;
    /* Crash screen text is anchored to the logical canvas top-left.
     * Keep a small, consistent margin so the first glyph is never vertically
     * centered or horizontally shifted by the normal desktop layout. */
    int y = 16;
    int text_x = 16;

    if (vga_native_text_mode_active() || fb.address == NULL || fb.width < OS_WIDTH) {
        serial_trace("ERROR", "RENDER crash screen unavailable: native VGA text mode or no framebuffer");
        serial_trace_hex_value("ERROR", "RENDER native text mode", vga_native_text_mode_active() ? 1 : 0);
        serial_trace_hex_value("ERROR", "RENDER framebuffer address", (uint32_t)(uintptr_t)fb.address);
        serial_trace_hex_value("ERROR", "RENDER framebuffer width", fb.width);
        return;
    }

    serial_trace("RENDER", "crash screen render started");
    serial_trace_concat("RENDER", "exception: ", name);
    serial_trace_hex_value("RENDER", "framebuffer", (uint32_t)(uintptr_t)fb.address);
    serial_trace_hex_value("RENDER", "framebuffer size", ((uint32_t)fb.width << 16) | fb.height);
    serial_trace_hex_value("RENDER", "framebuffer bpp", fb.bpp);

    fill_rect(0, 0, OS_WIDTH, OS_HEIGHT, color_blue);

    draw_text(text_x, y, "A problem has been detected!!! HaloxOS has been shut down to prevent damage", color_white, color_blue, false);
    y += 10;
    draw_text(text_x, y, "to your machine.", color_white, color_blue, false);
    y += 20;

    draw_text(text_x, y, name, color_white, color_blue, false);
    y += 30;

    /* Keep long diagnostics inside the 640x480 logical canvas instead of
     * allowing draw_text() to run past the right edge. */
    draw_text(text_x, y, "Looks like you've got something wrong with the system, please make sure to", color_white, color_blue, false);
    y += 10;
    draw_text(text_x, y, "report this issue:", color_white, color_blue, false);
    y += 10;
    draw_text(text_x, y, "https://github.com/svh03ra/HaloxOS", color_white, color_blue, false);
    y += 20;

    draw_text(text_x, y, "Please try again after restarting your device if you're tired...", color_white, color_blue, false);
    y += 40;

    line[0] = '\0';
    len = 0;
    copy_string(line, "*** STATUS: Error=", sizeof(line));
    len = strlen_local(line);
    bsod_append_hex32(line, &len, sizeof(line), frame != NULL ? frame->error_code : 0);
    copy_string(line + len, " Vector=", sizeof(line) - len);
    len = strlen_local(line);
    bsod_append_hex32(line, &len, sizeof(line), frame != NULL ? frame->vector : 0);
    draw_text(text_x, y, line, color_white, color_blue, false);
    y += 20;

    if (debug) {
        draw_text(text_x, y, "Machine Info:", color_white, color_blue, false);
        y += 10;

        if (frame != NULL) {
            line[0] = '\0';
            copy_string(line, "EAX=", sizeof(line));
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->eax);
            copy_string(line + len, " EBX=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->ebx);
            copy_string(line + len, " ECX=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->ecx);
            copy_string(line + len, " EDX=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->edx);
            draw_text(text_x, y, line, color_white, color_blue, false);
            y += 10;

            line[0] = '\0';
            copy_string(line, "ESI=", sizeof(line));
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->esi);
            copy_string(line + len, " EDI=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->edi);
            copy_string(line + len, " EBP=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->ebp);
            copy_string(line + len, " ESP=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->esp);
            draw_text(text_x, y, line, color_white, color_blue, false);
            y += 10;

            line[0] = '\0';
            copy_string(line, "EIP=", sizeof(line));
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->eip);
            copy_string(line + len, " CS=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->cs);
            copy_string(line + len, " DS=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->ds);
            copy_string(line + len, " ES=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->es);
            draw_text(text_x, y, line, color_white, color_blue, false);
            y += 10;

            line[0] = '\0';
            copy_string(line, "FS=", sizeof(line));
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->fs);
            copy_string(line + len, " GS=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->gs);
            copy_string(line + len, " EFLAGS=", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), frame->eflags);
            draw_text(text_x, y, line, color_white, color_blue, false);
            y += 20;
        }

        draw_text(text_x, y, "SYSTEM STOP.", color_white, color_blue, false);
        y += 20;
    }

    /*
     * Memory Map: raw E820 entry base addresses (low 32 bits) saved from
     * the multiboot header at boot, four hex values per row.
     */
    if (crash_mmap_addr != 0 && crash_mmap_length != 0) {
        uintptr_t cursor = crash_mmap_addr;
        uintptr_t end = crash_mmap_addr + crash_mmap_length;
        int column = 0;
        int rows_drawn = 0;
        int entry_count = 0;
        int truncated = 0;
        const int max_rows = (OS_HEIGHT - 40 - y) / 10;

        serial_trace("RENDER", "memory map render started");
        draw_text(text_x, y, "Memory Map:", color_white, color_blue, false);
        y += 10;

        line[0] = '\0';
        len = 0;
        while (cursor + sizeof(uint32_t) <= end) {
            const MultibootMmapEntry *entry = (const MultibootMmapEntry *)cursor;

            if (cursor + entry->size + sizeof(uint32_t) > end) {
                break;
            }

            if (rows_drawn >= max_rows) {
                truncated = 1;
                break;
            }

            if (column > 0) {
                copy_string(line + len, " ", sizeof(line) - len);
                ++len;
            }
            copy_string(line + len, "0x", sizeof(line) - len);
            len = strlen_local(line);
            bsod_append_hex32(line, &len, sizeof(line), (uint32_t)entry->base_addr);
            ++column;
            ++entry_count;

            if (column == 4) {
                draw_text(text_x, y, line, color_white, color_blue, false);
                y += 10;
                ++rows_drawn;
                column = 0;
                line[0] = '\0';
                len = 0;
            }

            cursor += entry->size + sizeof(uint32_t);
        }

        if (column > 0 && rows_drawn < max_rows) {
            draw_text(text_x, y, line, color_white, color_blue, false);
            ++rows_drawn;
        }

        serial_trace_uint_value("RENDER", "memory map entries rendered", (uint32_t)entry_count);
        if (truncated) {
            serial_trace("RENDER", "memory map truncated: not enough screen rows");
        } else {
            serial_trace("RENDER", "memory map render complete");
        }
    } else {
        serial_trace("ERROR", "RENDER memory map unavailable: no multiboot E820 data");
        serial_trace_hex_value("ERROR", "RENDER mmap address", crash_mmap_addr);
        serial_trace_hex_value("ERROR", "RENDER mmap length", crash_mmap_length);
    }

    present();
    serial_trace("RENDER", "crash screen presented to framebuffer: SUCCESS");
}

void cpu_exception_handler(const CpuExceptionFrame *frame) {
    const char *reason = debug_forced_fault_reason;
    const char *name = frame != NULL ? cpu_exception_name(frame->vector) : "CPU Exception";

    serial_dump_cpu_exception(name, reason, frame);
    debug_forced_fault_reason = NULL;
    render_bsod(name, frame);
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

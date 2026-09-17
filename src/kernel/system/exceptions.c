// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: exceptions.c, crash exception and etc.

// This repository is licensed under the GNU General Public License.

/* Debug-build backtrace/calltrace support. The build generates
 * build/generated/crash_symbols.h/.c (one nm + one addr2line pass over
 * the final kernel) with a FIXED-SIZE table, so filling in the real
 * symbols never shifts a kernel address and the kernel itself is built
 * in a single pass. Release builds get a zero-filled table and only raw
 * addresses render. */
#include "crash_symbols.h"

#if defined(HALOXOS_CONFIG_DEBUG) && HALOXOS_CONFIG_DEBUG
#define CRASH_TRACE_ENABLED 1
#else
#define CRASH_TRACE_ENABLED 0
#endif

#if CRASH_TRACE_ENABLED
#ifndef CRASH_SYMBOL_MAX
#define CRASH_SYMBOL_MAX 0
#endif

#define CRASH_TRACE_MAX     32   /* frames captured per walk */
#endif

/* Defined later in this file / graphics.c; forward declared because the
 * trace view is compiled above them in the amalgamation order. */
#if CRASH_TRACE_ENABLED
static void bsod_append_hex32(char *buffer, size_t *len, size_t max_len, uint32_t value);
static void draw_text_clipped(int x, int y, int max_w, const char *text, uint8_t fg, uint8_t bg, bool transparent);
#endif
static void draw_pixel(int x, int y, uint8_t color);

/*
 * Animated wireframe cube badge (top-right of the crash screen, clear
 * of the crash text): white edges over a black offset copy for a
 * shadow illusion. The animation clock is the PIT channel-0 counter
 * (port 0x40) read with a proper LATCH command, because the crash
 * handler runs with interrupts disabled and timer_ticks is frozen.
 *
 * Deep-debug notes (why the first version never animated):
 *  - the PIT is programmed mode 3 (square wave): the count reloads at
 *    the DIVISOR (~19886 at 60Hz), not at 65536, so "0x10000 - count"
 *    phase math was meaningless;
 *  - the counter counts DOWN: a reload shows up as count JUMPING UP,
 *    detected as now > last -> one 60Hz frame per jump;
 *  - reading 0x40 without latching can mix lo/hi bytes of two
 *    different counts: outb(0x43, 0x00) freezes the value first.
 */
static uint16_t crash_pit_count(void) {
    uint16_t lo;
    uint16_t hi;

    outb(0x43, 0x00);   /* latch channel-0 count for a stable read */
    lo = (uint16_t)inb(0x40);
    hi = (uint16_t)inb(0x40);
    return (uint16_t)((hi << 8) | lo);
}

static uint32_t crash_cube_frames = 0;      /* 60Hz frames since crash */
static uint16_t crash_cube_last_count = 0;

/* Advance the animation clock: call as often as possible; each PIT
 * reload wrap = one 60Hz frame. Returns true when the frame changed. */
static bool crash_cube_tick(void) {
    uint16_t now = crash_pit_count();
    bool advanced = false;

    if (now > crash_cube_last_count) {
        /* counted DOWN past the reload point: new 60Hz frame */
        ++crash_cube_frames;
        advanced = true;
    }
    crash_cube_last_count = now;
    return advanced;
}

static void crash_cube_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    {
        int err = dx - dy;

        for (;;) {
            draw_pixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            {
                int e2 = err * 2;

                if (e2 > -dy) {
                    err -= dy;
                    x0 += sx;
                }
                if (e2 < dx) {
                    err += dx;
                    y0 += sy;
                }
            }
        }
    }
}

static int crash_cube_sin(int deg) {
    /* exact round(256*sin(deg)) quadrant table, same as the 3D Box app */
    static const int table[91] = {
        0, 4, 9, 13,  18, 22, 27, 31,  36, 40, 44, 49,  53, 58, 62, 66,
        71, 75, 79, 83,  88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132,
        136, 139, 143, 147, 150, 154, 158, 161, 165, 168, 171, 175, 178, 181, 184, 187,
        190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219, 222, 224, 226, 228,
        230, 232, 234, 236, 237, 239, 241, 242, 243, 245, 246, 247, 248, 249, 250, 251,
        252, 253, 254, 254, 255, 255, 255, 256, 256, 256, 256
    };
    int a = deg % 360;

    if (a < 0) {
        a += 360;
    }
    if (a <= 90) return table[a];
    if (a < 180) return table[180 - a];
    if (a < 270) return -table[a - 180];
    return -table[359 - a];
}

static void crash_draw_cube(int cx, int cy, int angle) {
    static const int vx[8] = { -40,  40,  40, -40, -40,  40,  40, -40 };
    static const int vy[8] = { -40, -40,  40,  40, -40, -40,  40,  40 };
    static const int vz[8] = { -40, -40, -40, -40,  40,  40,  40,  40 };
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    int px[8];
    int py[8];
    int sa = crash_cube_sin(angle);
    int ca = crash_cube_sin(angle + 90);
    int sb = crash_cube_sin(angle * 2 + 30);   /* tumble: pitch twice as fast */
    int cb = crash_cube_sin(angle * 2 + 120);

    for (int i = 0; i < 8; ++i) {
        int rx = (vx[i] * ca - vz[i] * sa) / 256;
        int rz = (vx[i] * sa + vz[i] * ca) / 256;
        int ry = (vy[i] * cb - rz * sb) / 256;
        int rz2 = (vy[i] * sb + rz * cb) / 256;
        int depth = 300 + rz2;

        if (depth < 60) {
            depth = 60;
        }
        px[i] = cx + rx * 200 / depth;
        py[i] = cy - ry * 200 / depth;
    }

    /* black shadow copy offset a few px, then the white wire on top */
    for (int pass = 0; pass < 2; ++pass) {
        int off = pass == 0 ? 3 : 0;
        uint8_t col = pass == 0 ? color_black : color_white;

        for (int e = 0; e < 12; ++e) {
            int a = edges[e][0];
            int b = edges[e][1];

            crash_cube_line(px[a] + off, py[a] + off, px[b] + off, py[b] + off, col);
        }
    }
}

static void crash_render_cube_badge(void) {
    /* Far-right middle, level with the *** STATUS line and below: the
     * register dump lines only reach ~200px wide, so the right edge is
     * free between the STATUS row and the memory map. */
    int cx = OS_WIDTH - 84;
    int cy = 196;

    crash_draw_cube(cx, cy, (int)(crash_cube_frames % 360u));
}

/*
 * Transparent badge redraw: the region's background pixels are saved
 * ONCE (before the first draw) and restored per animation frame, so
 * the cube floats over the crash report with no opaque erase block -
 * no blue rectangle, no clipped text.
 */
#define CRASH_CUBE_SAVE_W 128
#define CRASH_CUBE_SAVE_H 120
#define CRASH_CUBE_SAVE_X (OS_WIDTH - 148)
#define CRASH_CUBE_SAVE_Y 140
/* Snapshot in RGB565, not palette index: only one shadow plane is live at
 * a time (see present_index_plane_live in vga.c), so reading the indexed
 * plane directly would snapshot stale bytes on every RGB output. */
static uint16_t crash_cube_saved_bg[CRASH_CUBE_SAVE_W * CRASH_CUBE_SAVE_H];
static bool crash_cube_bg_saved = false;

static void crash_cube_badge_redraw(void) {
    if (!crash_cube_bg_saved) {
        /* first call: snapshot whatever is under the badge area */
        for (int yy = 0; yy < CRASH_CUBE_SAVE_H; ++yy) {
            for (int xx = 0; xx < CRASH_CUBE_SAVE_W; ++xx) {
                int sx = CRASH_CUBE_SAVE_X + xx;
                int sy = CRASH_CUBE_SAVE_Y + yy;

                if (sx >= 0 && sy >= 0 && sx < OS_WIDTH && sy < OS_HEIGHT) {
                    crash_cube_saved_bg[yy * CRASH_CUBE_SAVE_W + xx] = plane_get_pixel_rgb(sx, sy);
                }
            }
        }
        crash_cube_bg_saved = true;
    } else {
        /* restore the snapshot, then draw the new frame on top */
        for (int yy = 0; yy < CRASH_CUBE_SAVE_H; ++yy) {
            for (int xx = 0; xx < CRASH_CUBE_SAVE_W; ++xx) {
                int sx = CRASH_CUBE_SAVE_X + xx;
                int sy = CRASH_CUBE_SAVE_Y + yy;

                if (sx >= 0 && sy >= 0 && sx < OS_WIDTH && sy < OS_HEIGHT) {
                    plane_set_pixel_rgb((uint32_t)sy * OS_WIDTH + (uint32_t)sx,
                                        crash_cube_saved_bg[yy * CRASH_CUBE_SAVE_W + xx]);
                }
            }
        }
    }
    crash_render_cube_badge();
}

/* Symbol lookup is kept OUTSIDE the debug-only guard so the debugger
 * terminal breakpoint trace can use it in every build; release builds
 * embed an empty table and simply get "unknown". */
static int crash_symbol_lookup(uint32_t addr, const char **name,
                                const char **file, int *line, uint32_t *offset);

/* Binary search over the addr-ordered nm table in crash_symbols.c. The
 * table is always CRASH_SYMBOL_MAX entries long (unused slots zeroed) so
 * the symbol data never moves; crash_symbol_count says how many slots
 * hold real symbols. */
static int crash_symbol_lookup(uint32_t addr, const char **name,
                               const char **file, int *line, uint32_t *offset) {
    if (crash_symbol_count > 0) {
        int lo = 0;
        int hi = crash_symbol_count - 1;
        int best = -1;

        while (lo <= hi) {
            int mid = (lo + hi) / 2;

            if (crash_symbol_table_data[mid].addr <= addr) {
                best = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        if (best >= 0) {
            *name = crash_symbol_table_data[best].name;
            *file = crash_symbol_table_data[best].file;
            *line = crash_symbol_table_data[best].line;
            *offset = addr - (uint32_t)crash_symbol_table_data[best].addr;
            return 1;
        }
    }

    (void)addr;
    *name = "unknown";
    *file = "?";
    *line = 0;
    *offset = 0;
    return 0;
}

/*
 * EBP chain walk. Requires -fno-omit-frame-pointer (debug builds add it
 * in the Makefile). Every frame is: [ebp] = saved ebp, [ebp+4] = return
 * eip. Stops on obviously invalid frame pointers instead of faulting
 * inside the crash handler.
 */
static int crash_capture_trace(uint32_t start_ebp, uint32_t start_eip,
                               uint32_t *frames) {
    uint32_t ebp = start_ebp;
    int count = 0;

    frames[count++] = start_eip;
    for (int i = 0; i < CRASH_TRACE_MAX - 1; ++i) {
        uint32_t next_ebp;
        uint32_t ret;

        if (ebp < 0x10000u || ebp > 0x7F000000u || (ebp & 3u) != 0) {
            break;
        }
        next_ebp = *(volatile uint32_t *)ebp;
        ret = *(volatile uint32_t *)(ebp + 4u);
        if (next_ebp <= ebp || next_ebp > ebp + 0x4000u) {
            break;   /* chain must grow upward and stay plausible */
        }
        if (ret < 0x200000u || ret > 0x5D0000u) {
            break;   /* return address must lie inside the kernel image */
        }
        frames[count++] = ret;
        ebp = next_ebp;
    }
    return count;
}

static void crash_render_trace_lines(int x, int y, const uint32_t *frames,
                                     int count, int skip, int max_rows,
                                     int calltrace_mode, int hscroll,
                                     int *out_max_len) {
    /* Full source paths are wider than the 640px window can show, so
     * lines are built with the complete path and the view scrolls them
     * horizontally (Ctrl+Left/Right). Yellow << / >> horns mark hidden
     * text on the left/right side of the window, per row. */
    char line[160];
    const int max_w = OS_WIDTH - 32 - x;
    const int visible_chars = max_w / 8;
    int row = 0;
    int max_len = 0;

    for (int i = skip; i < count && row < max_rows; ++i, ++row) {
        const char *name;
        const char *file;
        int line_no;
        uint32_t offset;
        size_t len = 0;

        line[0] = '\0';
        copy_string(line, "[0x", sizeof(line));
        len = strlen_local(line);
        bsod_append_hex32(line, &len, sizeof(line), frames[i]);
        copy_string(line + len, "] ", sizeof(line) - len);
        len = strlen_local(line);

        if (crash_symbol_lookup(frames[i], &name, &file, &line_no, &offset)) {
            if (calltrace_mode) {
                copy_string(line + len, name, sizeof(line) - len);
                len = strlen_local(line);
                copy_string(line + len, "+0x", sizeof(line) - len);
                len = strlen_local(line);
                bsod_append_hex32(line, &len, sizeof(line), offset);
            } else {
                /* full extended source path: what does not fit the
                 * 640px screen is reached with Ctrl+Left/Right */
                copy_string(line + len, name, sizeof(line) - len);
                len = strlen_local(line);
                copy_string(line + len, " at ", sizeof(line) - len);
                len = strlen_local(line);
                copy_string(line + len, file, sizeof(line) - len);
                len = strlen_local(line);
                copy_string(line + len, "; line ", sizeof(line) - len);
                len = strlen_local(line);
                /* decimal line number */
                {
                    char digits[12];
                    int dn = 0;
                    uint32_t v = (uint32_t)line_no;

                    if (v == 0) {
                        digits[dn++] = '?';
                    }
                    while (v > 0 && dn < 11) {
                        digits[dn++] = (char)('0' + (v % 10u));
                        v /= 10u;
                    }
                    while (dn > 0 && len < sizeof(line) - 2) {
                        line[len++] = digits[--dn];
                    }
                    line[len] = '\0';
                }
            }
        } else {
            copy_string(line + len, "unknown", sizeof(line) - len);
        }

        /* horizontal window into the row text (Ctrl+Left/Right) */
        if ((int)len > max_len) {
            max_len = (int)len;
        }
        if (hscroll < (int)len) {
            draw_text_clipped(x, y + row * 10, max_w,
                              &line[hscroll], color_white, color_crash_blue, false);
        }
        if (hscroll > 0 && hscroll < (int)len) {
            /* hidden text on the left edge */
            draw_text(x, y + row * 10, "<<", color_yellow, color_crash_blue, false);
        }
        if (hscroll < (int)len - visible_chars) {
            /* hidden text on the right edge */
            draw_text(OS_WIDTH - 32, y + row * 10, ">>", color_yellow, color_crash_blue, false);
        }
    }
    if (out_max_len != NULL) {
        *out_max_len = max_len;
    }
}

/*
 * Full trace dump to the serial log: every captured frame with address,
 * symbol, offset, and the complete source file/line (no clipping). Gives
 * the terminal log more detail than the 640px screen can show.
 */
static void crash_serial_dump_trace(const CpuExceptionFrame *frame) {
    uint32_t frames[CRASH_TRACE_MAX];
    int count;

    if (frame == NULL) {
        return;
    }
    count = crash_capture_trace(frame->ebp, frame->eip, frames);

    serial_write_string("[INFO]: ===== Call/Backtrace =====\n");
    for (int i = 0; i < count; ++i) {
        const char *name;
        const char *file;
        int line_no;
        uint32_t offset;

        serial_write_string("[INFO]: [");
        serial_write_uint((uint32_t)i);
        serial_write_string("] ");
        serial_write_hex32(frames[i]);
        if (crash_symbol_lookup(frames[i], &name, &file, &line_no, &offset)) {
            serial_write_string(" ");
            serial_write_string(name);
            serial_write_string("+");
            serial_write_hex32(offset);
            serial_write_string(" at ");
            serial_write_string(file);
            serial_write_string(":");
            serial_write_uint((uint32_t)line_no);
        } else {
            serial_write_string(" unknown");
        }
        serial_write_string("\n");
    }
    serial_write_string("[INFO]: ===== end trace =====\n");
}

/*
 * Interactive Backtrace/Calltrace viewer (debug builds only). Drawn at
 * the crash screen after the memory map; runs its own keyboard loop so
 * it stays alive while the rest of the system is halted:
 *   Left/Right = switch Backtrace <-> Calltrace page
 *   Up/Down    = scroll the frame list
 */
static void crash_render_trace_view(int x, int y_start, const CpuExceptionFrame *frame) {
    uint32_t frames[CRASH_TRACE_MAX];
    int count;
    int page_calltrace = 0;    /* default: Backtrace */
    int scroll = 0;
    int hscroll = 0;           /* horizontal window into each row */
    int ctrl_down = 0;
    int extended_next = 0;
    const int header_rows = 2;
    const int max_rows = (OS_HEIGHT - 16 - y_start) / 10 - header_rows;
    int max_line_len = 0;
    const int visible_chars = (OS_WIDTH - 32 - x) / 8;

    if (frame == NULL) {
        return;
    }

    count = crash_capture_trace(frame->ebp, frame->eip, frames);
    serial_trace_uint_value("RENDER", "trace frames captured", (uint32_t)count);

    /* Mask the keyboard IRQ so the normal IRQ1 handler (still installed
     * and enabled at crash time) cannot steal the scancrokes away from
     * this polling loop. Timer IRQ stays alive so hlt wakes and the
     * cursor-less screen keeps scanning for input. */
    outb(0x21, inb(0x21) | 0x02u);

    for (;;) {
        /* Clear the whole trace view region first so switching pages or
         * scrolling never leaves stale pixels from the previous frame
         * (old header tails, removed rows). The clear starts exactly at
         * the header row - never above it - so the memory map's last
         * line above the view is never sliced into. */
        fill_rect(x - 4, y_start, OS_WIDTH - x - 8, OS_HEIGHT - y_start, color_crash_blue);

        /* header + list */
        draw_text(x, y_start, page_calltrace ? "Calltrace:" : "Backtrace:",
                  color_white, color_crash_blue, false);
        draw_text(x, y_start + 10, "----------------------------------------------------------------------------",
                  color_white, color_crash_blue, false);
        crash_render_trace_lines(x, y_start + header_rows * 10, frames,
                                 count, scroll, max_rows, page_calltrace,
                                 hscroll, &max_line_len);
        if (hscroll > max_line_len - visible_chars) {
            hscroll = max_line_len - visible_chars;
            if (hscroll < 0) {
                hscroll = 0;
            }
        }
        {
            /* key legend: horizontal scroll hint for extended paths */
            char hint[64];
            size_t hlen = 0;

            hint[0] = '\0';
            copy_string(hint, "< > page  ^ v rows  Ctrl+< > scroll", sizeof(hint));
            hlen = strlen_local(hint);
            if (hscroll > 0) {
                copy_string(hint + hlen, " <<", sizeof(hint) - hlen);
                hlen = strlen_local(hint);
            }
            if (hscroll < max_line_len - visible_chars) {
                copy_string(hint + hlen, " >>", sizeof(hint) - hlen);
            }
            draw_text(x, OS_HEIGHT - 26, hint, color_gray_light, color_crash_blue, false);
        }

        /* scroll indicator */
        if (count > max_rows) {
            char info[48];
            size_t len = 0;

            info[0] = '\0';
            copy_string(info, "rows ", sizeof(info));
            len = strlen_local(info);
            {
                char digits[8];
                int dn = 0;
                uint32_t shown = (uint32_t)(count - scroll < max_rows ? count - scroll : max_rows);

                while (shown > 0 && dn < 7) {
                    digits[dn++] = (char)('0' + (shown % 10u));
                    shown /= 10u;
                }
                while (dn > 0 && len < sizeof(info) - 2) {
                    info[len++] = digits[--dn];
                }
                info[len] = '\0';
            }
            copy_string(info + len, " of ", sizeof(info) - len);
            len = strlen_local(info);
            {
                char digits[8];
                int dn = 0;
                uint32_t total = (uint32_t)count;

                while (total > 0 && dn < 7) {
                    digits[dn++] = (char)('0' + (total % 10u));
                    total /= 10u;
                }
                while (dn > 0 && len < sizeof(info) - 2) {
                    info[len++] = digits[--dn];
                }
                info[len] = '\0';
            }
            draw_text(x, OS_HEIGHT - 14, info, color_gray_light, color_crash_blue, false);
        }

        present();

        /* Animated cube badge (right side, after the STATUS line):
         * redrawn fresh on every trace view pass so scrolling or page
         * switching never smears it; keeps spinning in the key poll. */
        crash_cube_badge_redraw();
        present();

        /* interactive keyboard loop: poll the PS/2 controller directly
         * because the normal IRQ-driven input path is dead at crash time.
         * Busy-poll without hlt: the exception entered with interrupts
         * disabled, so hlt would sleep forever and never see keys. */
        for (;;) {
            uint8_t status;
            uint8_t scancode;
            int was_extended;

            /* cube heartbeat: on every PIT frame wrap, redraw the badge
             * so the animation never stops while polling for keys */            if (crash_cube_tick()) {
                crash_cube_badge_redraw();
                present();
            }

            status = inb(0x64);
            if ((status & 0x01u) == 0) {
                continue;
            }
            scancode = inb(0x60);
            if (scancode == 0xE0) {
                extended_next = 1;
                continue;
            }
            if (scancode == 0x1D) {          /* Ctrl press */
                ctrl_down = 1;
                continue;
            }
            if (scancode == 0x9D) {          /* Ctrl release */
                ctrl_down = 0;
                continue;
            }
            if (scancode & 0x80u) {
                extended_next = 0;
                continue;   /* ignore other key releases */
            }
            was_extended = extended_next;
            extended_next = 0;
            if (was_extended && ctrl_down && scancode == 0x4Bu) {
                /* Ctrl+LEFT: scroll row text toward the start */
                if (hscroll > 0) {
                    --hscroll;
                }
            } else if (was_extended && ctrl_down && scancode == 0x4Du) {
                /* Ctrl+RIGHT: scroll row text toward the end */
                if (hscroll < max_line_len - visible_chars) {
                    ++hscroll;
                }
            } else if (scancode == 0x4Bu) {          /* LEFT */
                page_calltrace = 0;
                scroll = 0;
                hscroll = 0;
            } else if (scancode == 0x4Du) {   /* RIGHT */
                page_calltrace = 1;
                scroll = 0;
                hscroll = 0;
            } else if (scancode == 0x48u) {    /* UP */
                if (scroll > 0) {
                    --scroll;
                }
            } else if (scancode == 0x50u) {   /* DOWN */
                if (scroll < count - max_rows) {
                    ++scroll;
                }
            }
            break;   /* redraw with the new state */
        }
    }
}

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

    fill_rect(0, 0, OS_WIDTH, OS_HEIGHT, color_crash_blue);

    draw_text(text_x, y, "A problem has been detected!!! HaloxOS has been shut down to prevent damage", color_white, color_crash_blue, false);
    y += 10;
    draw_text(text_x, y, "to your machine.", color_white, color_crash_blue, false);
    y += 20;

    line[0] = '\0';
    len = 0;
    copy_string(line, "CODE: ", sizeof(line));
    len = strlen_local(line);
    copy_string(line + len, name, sizeof(line) - len);
    draw_text(text_x, y, line, color_white, color_crash_blue, false);
    y += 30;

    /* Keep long diagnostics inside the 640x480 logical canvas instead of
     * allowing draw_text() to run past the right edge. */
    draw_text(text_x, y, "Looks like you've got something wrong with the system, please make sure to", color_white, color_crash_blue, false);
    y += 10;
    draw_text(text_x, y, "report this issue:", color_white, color_crash_blue, false);
    y += 10;
    draw_text(text_x, y, "https://github.com/svh03ra/HaloxOS", color_white, color_crash_blue, false);
    y += 20;

    draw_text(text_x, y, "Please try again after restarting your device if you're tired...", color_white, color_crash_blue, false);
    y += 40;

    line[0] = '\0';
    len = 0;
    copy_string(line, "*** STATUS: Error=", sizeof(line));
    len = strlen_local(line);
    bsod_append_hex32(line, &len, sizeof(line), frame != NULL ? frame->error_code : 0);
    copy_string(line + len, " Vector=", sizeof(line) - len);
    len = strlen_local(line);
    bsod_append_hex32(line, &len, sizeof(line), frame != NULL ? frame->vector : 0);
    draw_text(text_x, y, line, color_white, color_crash_blue, false);
    y += 20;

    if (debug) {
        draw_text(text_x, y, "Machine Info:", color_white, color_crash_blue, false);
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
            draw_text(text_x, y, line, color_white, color_crash_blue, false);
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
            draw_text(text_x, y, line, color_white, color_crash_blue, false);
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
            draw_text(text_x, y, line, color_white, color_crash_blue, false);
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
            draw_text(text_x, y, line, color_white, color_crash_blue, false);
            y += 20;
        }

        draw_text(text_x, y, "SYSTEM STOP.", color_white, color_crash_blue, false);
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
        draw_text(text_x, y, "Memory Map:", color_white, color_crash_blue, false);
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
                draw_text(text_x, y, line, color_white, color_crash_blue, false);
                y += 10;
                ++rows_drawn;
                column = 0;
                line[0] = '\0';
                len = 0;
            }

            cursor += entry->size + sizeof(uint32_t);
        }

        if (column > 0 && rows_drawn < max_rows) {
            draw_text(text_x, y, line, color_white, color_crash_blue, false);
            y += 10;
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

#if CRASH_TRACE_ENABLED
    /* Debug builds: full trace detail to the serial log first (more
     * detail than the screen can show), then the interactive
     * Backtrace/Calltrace view below the memory map. Never returns -
     * it owns the crash screen until the machine is reset. y + 10
     * keeps one blank row between the last map row and the trace
     * header. The animated cube badge (right side, after the STATUS
     * line) keeps spinning inside the trace view's poll loop. */
    crash_cube_last_count = crash_pit_count();
    crash_cube_badge_redraw();
    present();
    crash_serial_dump_trace(frame);
    crash_render_trace_view(text_x, y + 10, frame);
#else
    /* Release builds: no trace view - animate the cube badge forever
     * on the frozen screen (PIT clock), redrawing only the badge area
     * so the crash report itself is never disturbed. */
    crash_cube_last_count = crash_pit_count();
    for (;;) {
        if (crash_cube_tick()) {
            crash_cube_badge_redraw();
            present();
        }
    }
#endif

    present();
    serial_trace("RENDER", "crash screen presented to framebuffer: SUCCESS");
}

void cpu_exception_handler(const CpuExceptionFrame *frame) {
    static bool crash_handler_active = false;
    const char *reason = debug_forced_fault_reason;
    const char *name = frame != NULL ? cpu_exception_name(frame->vector) : "CPU Exception";

    /* Nested crash (the system crashed INSIDE the crash handler):
     * never try to re-render the full BSOD - that risks faulting
     * again and triple-faulting the machine. Keep what is already on
     * screen and drop into the crash-safe cube animation loop so the
     * crash screen stays alive forever instead of resetting. */
    if (crash_handler_active) {
        serial_write_string("[ERROR]: Nested crash inside the crash handler - keeping screen alive\n");
        if (frame != NULL) {
            serial_write_string("[ERROR]: Nested exception vector=");
            serial_write_uint(frame->vector);
            serial_write_string(" EIP=");
            serial_write_hex32(frame->eip);
            serial_write_string("\n");
        }
        __asm__ volatile ("cli");
        /* reset the animation clock base so the badge keeps ticking */
        crash_cube_last_count = crash_pit_count();
        for (;;) {
            if (crash_cube_tick()) {
                crash_cube_badge_redraw();
                present();
            }
        }
    }

    crash_handler_active = true;
    serial_dump_cpu_exception(name, reason, frame);
    debug_forced_fault_reason = NULL;
    render_bsod(name, frame);
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

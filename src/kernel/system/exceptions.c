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

#ifndef CRASH_SYMBOL_MAX
#define CRASH_SYMBOL_MAX 0
#endif

/* Frames captured per stack walk. Both profiles walk it: a release build has
 * no symbol table, but the raw return addresses still say where the fault
 * came from. */
#define CRASH_TRACE_MAX     32

#if CRASH_TRACE_ENABLED

/* Horizontal backtrace scrolling.  A tap moves a couple of columns; a
 * held Ctrl+Left/Right repeats on its own, one column per frame (two once
 * held longer), instead of waiting for the keyboard's typematic rate -
 * which is far too slow on real hardware for these long source paths. */
#define CRASH_SCROLL_TAP_STEP      2
#define CRASH_SCROLL_REPEAT_DELAY  12   /* frames before a hold repeats */
#define CRASH_SCROLL_FAST_FRAMES   48   /* frames before it doubles up */
#endif

/* Defined later in this file / graphics.c; forward declared because the
 * trace view is compiled above them in the amalgamation order. */
#if CRASH_TRACE_ENABLED
static void bsod_append_hex32(char *buffer, size_t *len, size_t max_len, uint32_t value);
static void draw_text_clipped(int x, int y, int max_w, const char *text, uint8_t fg, uint8_t bg, bool transparent);
/* Native USB keyboard source, defined in xhci_hid.c later in the
 * amalgamation.  The crash handler polls it beside the 8042 so a
 * USB-only machine is not left with an unresponsive trace viewer. */
static void xhci_hid_crash_begin(void);
static bool xhci_hid_crash_poll(uint8_t *scancode);
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
 * The call trace: which functions led to the fault.
 *
 * The walk itself is compiled into both profiles (release builds keep frame
 * pointers for it and print raw addresses); the symbolised rendering, the
 * serial dump and the interactive viewer below are debug-build only.
 */

/*
 * EBP chain walk. Requires -fno-omit-frame-pointer (both profiles add it
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

#if CRASH_TRACE_ENABLED

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
        append_string(line, &len, sizeof(line), "[0x");
        bsod_append_hex32(line, &len, sizeof(line), frames[i]);
        append_string(line, &len, sizeof(line), "] ");

        if (crash_symbol_lookup(frames[i], &name, &file, &line_no, &offset)) {
            if (calltrace_mode) {
                append_string(line, &len, sizeof(line), name);
                append_string(line, &len, sizeof(line), "+0x");
                bsod_append_hex32(line, &len, sizeof(line), offset);
            } else {
                /* full extended source path: what does not fit the
                 * 640px screen is reached with Ctrl+Left/Right */
                append_string(line, &len, sizeof(line), name);
                append_string(line, &len, sizeof(line), " at ");
                append_string(line, &len, sizeof(line), file);
                append_string(line, &len, sizeof(line), "; line ");
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
            append_string(line, &len, sizeof(line), "unknown");
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
    int hold_dir = 0;          /* -1 left, +1 right while the key is held */
    int hold_frames = 0;       /* PIT frames the key has been held */
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

    /* Drop any scancodes the xHCI decoder queued during normal operation;
     * from here on every poll feeds the trace viewer directly. */
    xhci_hid_crash_begin();

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

            append_string(hint, &hlen, sizeof(hint), "< > page  ^ v rows  Ctrl+< > scroll");
            if (hscroll > 0) {
                append_string(hint, &hlen, sizeof(hint), " <<");
            }
            if (hscroll < max_line_len - visible_chars) {
                append_string(hint, &hlen, sizeof(hint), " >>");
            }
            draw_text(x, OS_HEIGHT - 26, hint, color_gray_light, color_crash_blue, false);
        }

        /* scroll indicator */
        if (count > max_rows) {
            char info[48];
            size_t len = 0;

            append_string(info, &len, sizeof(info), "rows ");
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
            append_string(info, &len, sizeof(info), " of ");
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

        /* interactive keyboard loop: poll both native sources directly
         * because the normal IRQ-driven input path is dead at crash time.
         * The xHCI poll runs first: on a USB-only board port 0x64 floats
         * at 0xFF and would otherwise keep this loop reading a phantom
         * byte forever without ever reaching the real keyboard. */
        for (;;) {
            uint8_t status;
            uint8_t scancode;
            int was_extended;

            /* cube heartbeat: on every PIT frame wrap, redraw the badge
             * so the animation never stops while polling for keys */
            if (crash_cube_tick()) {
                crash_cube_badge_redraw();
                present();

                /* Held Ctrl+Left/Right keeps scrolling without waiting
                 * for the next keyboard repeat: one character per frame
                 * after a short delay, two once the hold is long enough.
                 * The outer loop then repaints the text at the new
                 * offset.  A tap still moves CRASH_SCROLL_TAP_STEP only. */
                if (hold_dir != 0 && ctrl_down) {
                    int limit = max_line_len - visible_chars;

                    if (limit < 0) {
                        limit = 0;
                    }
                    if (++hold_frames >= CRASH_SCROLL_REPEAT_DELAY) {
                        int before = hscroll;
                        int step = hold_frames >= CRASH_SCROLL_FAST_FRAMES ? 2 : 1;

                        hscroll += hold_dir * step;
                        if (hscroll < 0) {
                            hscroll = 0;
                        }
                        if (hscroll > limit) {
                            hscroll = limit;
                        }
                        if (hscroll != before) {
                            break;
                        }
                    }
                }
            }

            if (!xhci_hid_crash_poll(&scancode)) {
                status = inb(0x64);
                if ((status & 0x01u) == 0 || status == 0xFFu) {
                    continue;
                }
                scancode = inb(0x60);
                if ((status & 0xC0u) != 0) {
                    continue; /* parity/timeout: no valid byte behind it */
                }
            }
            if (scancode == 0xE0) {
                extended_next = 1;
                continue;
            }
            if (scancode == 0x1D) {          /* Ctrl press */
                ctrl_down = 1;
                extended_next = 0;
                continue;
            }
            if (scancode == 0x9D) {          /* Ctrl release */
                ctrl_down = 0;
                hold_dir = 0;
                extended_next = 0;
                continue;
            }
            if (scancode & 0x80u) {
                uint8_t released = (uint8_t)(scancode & 0x7Fu);

                /* An arrow release ends the hold; without it a keyboard
                 * that lacks break codes would keep scrolling forever. */
                if (released == 0x4Bu || released == 0x4Du) {
                    hold_dir = 0;
                }
                extended_next = 0;
                continue;   /* ignore other key releases */
            }
            was_extended = extended_next;
            extended_next = 0;
            if (was_extended && ctrl_down && scancode == 0x4Bu) {
                /* Ctrl+LEFT: scroll row text toward the start.  A repeat
                 * make code from the keyboard for the key already held
                 * must not add another tap step or restart the hold. */
                if (hold_dir != -1) {
                    hold_dir = -1;
                    hold_frames = 0;
                    hscroll -= CRASH_SCROLL_TAP_STEP;
                    if (hscroll < 0) {
                        hscroll = 0;
                    }
                }
            } else if (was_extended && ctrl_down && scancode == 0x4Du) {
                /* Ctrl+RIGHT: scroll row text toward the end */
                if (hold_dir != 1) {
                    int limit = max_line_len - visible_chars;

                    if (limit < 0) {
                        limit = 0;
                    }
                    hold_dir = 1;
                    hold_frames = 0;
                    hscroll += CRASH_SCROLL_TAP_STEP;
                    if (hscroll > limit) {
                        hscroll = limit;
                    }
                }
            } else if (scancode == 0x4Bu) {          /* LEFT */
                page_calltrace = 0;
                scroll = 0;
                hscroll = 0;
                hold_dir = 0;
            } else if (scancode == 0x4Du) {   /* RIGHT */
                page_calltrace = 1;
                scroll = 0;
                hscroll = 0;
                hold_dir = 0;
            } else if (scancode == 0x48u) {    /* UP */
                hold_dir = 0;
                if (scroll > 0) {
                    --scroll;
                }
            } else if (scancode == 0x50u) {   /* DOWN */
                hold_dir = 0;
                if (scroll < count - max_rows) {
                    ++scroll;
                }
            } else {
                hold_dir = 0;
            }
            break;   /* redraw with the new state */
        }
    }
}

#endif /* CRASH_TRACE_ENABLED */

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
 * One "LABEL=XXXXXXXX" field on a diagnostic line.
 *
 * The register dump is a dozen of these in a row, so writing the pair of
 * calls out at every site is what made that block long enough to hide a
 * mistake in it. The label carries its own leading separator where the
 * field is not first, which is also what the calls did by hand.
 */
static void bsod_append_field(char *buffer, size_t *len, size_t max_len,
                              const char *label, uint32_t value) {
    append_string(buffer, len, max_len, label);
    bsod_append_hex32(buffer, len, max_len, value);
}

/*
 * Word-wrap one paragraph of the crash report.
 *
 * The report was originally hand-broken for the 640x480 canvas: the first
 * warning always put "to your machine." on its own row, and the "Looks
 * like..." block always broke after "make sure to". On a wider canvas
 * those breaks leave most of the row empty even though the whole sentence
 * fits, and on a narrower one a long row runs off the right edge. Wrap by
 * whole 8px characters instead, using the live canvas width, and return
 * the y of the row after the last one. Callers add the blank rows between
 * paragraphs, so a paragraph that grows or shrinks still keeps its gaps.
 */
#define BSOD_CHAR_WIDTH  8
#define BSOD_LINE_HEIGHT 10

static int bsod_draw_wrapped(int x, int y, int right, const char *text,
                             uint8_t fg, uint8_t bg, bool transparent) {
    char line[TERM_LINE_LEN];
    int max_chars = (right - x) / BSOD_CHAR_WIDTH;
    size_t len = 0;

    if (max_chars < 1) {
        max_chars = 1;
    }
    if (max_chars > (int)sizeof(line) - 1) {
        max_chars = (int)sizeof(line) - 1;
    }
    line[0] = '\0';

    while (*text != '\0') {
        const char *word;
        size_t word_len;

        while (*text == ' ') {
            ++text;
        }
        if (*text == '\0') {
            break;
        }

        word = text;
        while (*text != '\0' && *text != ' ') {
            ++text;
        }
        word_len = (size_t)(text - word);

        /* Flush the current row before starting a word that cannot fit
         * after it. The +1 is the separating space. */
        if (len > 0 && len + 1 + word_len > (size_t)max_chars) {
            draw_text(x, y, line, fg, bg, transparent);
            y += BSOD_LINE_HEIGHT;
            len = 0;
            line[0] = '\0';
        }

        /* A single word wider than the whole row (a URL on a tiny canvas)
         * is split at the row edge rather than drawn past the screen. */
        while (word_len > (size_t)max_chars - len) {
            size_t take = (size_t)max_chars - len;

            if (take > 0) {
                for (size_t i = 0; i < take; ++i) {
                    line[len++] = word[i];
                }
                line[len] = '\0';
                word += take;
                word_len -= take;
            }
            draw_text(x, y, line, fg, bg, transparent);
            y += BSOD_LINE_HEIGHT;
            len = 0;
            line[0] = '\0';
        }

        if (word_len > 0) {
            if (len > 0) {
                line[len++] = ' ';
            }
            for (size_t i = 0; i < word_len; ++i) {
                line[len++] = word[i];
            }
            line[len] = '\0';
        }
    }

    if (len > 0) {
        draw_text(x, y, line, fg, bg, transparent);
        y += BSOD_LINE_HEIGHT;
    }

    return y;
}

/*
 * Classic BSOD screen: solid blue background, white text. Only drawn when
 * the system crashed while a graphical framebuffer is active; the full
 * register dump always goes to the serial debugger trace regardless.
 */
#if !CRASH_TRACE_ENABLED
/*
 * Release-build backtrace: no symbol table and no interactive viewer, but
 * the return-address chain still says where the fault came from. Drawn once
 * under the memory map with raw addresses; the cube keeps animating over it.
 */
static int crash_render_flat_trace(int x, int y, const CpuExceptionFrame *frame) {
    uint32_t frames[CRASH_TRACE_MAX];
    char line[TERM_LINE_LEN];
    char index[8];
    int count;
    int rows;

    if (frame == NULL) {
        return y;
    }
    count = crash_capture_trace(frame->ebp, frame->eip, frames);
    if (count <= 0) {
        return y;
    }

    y += 20;
    draw_text(x, y, "Backtrace:", color_white, color_crash_blue, false);
    y += 10;

    rows = (OS_HEIGHT - 40 - y) / 10;
    if (rows > count) {
        rows = count;
    }

    for (int i = 0; i < rows; ++i) {
        size_t len = 0;
        int n = i;
        int digits = 0;

        do {
            index[digits++] = (char)('0' + n % 10);
            n /= 10;
        } while (n > 0 && digits < (int)sizeof(index));

        line[len++] = '[';
        while (digits > 0) {
            line[len++] = index[--digits];
        }
        line[len++] = ']';
        line[len++] = ' ';
        line[len] = '\0';
        bsod_append_field(line, &len, sizeof(line), "0x", frames[i]);
        draw_text(x, y, line, color_white, color_crash_blue, false);
        y += 10;
    }

    serial_trace_uint_value("RENDER", "release backtrace frames", (uint32_t)count);
    return y;
}
#endif /* !CRASH_TRACE_ENABLED */

/*
 * Draw the crash report onto the canvas. Returns the y below the last row so
 * the caller can place the interactive trace view under it.
 *
 * `include_mmap` is false for the compact retry. The E820 walk is the one
 * part of the report that dereferences a boot-time pointer, so it is the
 * first suspect when the full report faulted; skipping it is what lets the
 * retry finish and still show the status and register dump.
 */
static int bsod_paint(const char *name, const CpuExceptionFrame *frame, bool include_mmap) {
    char line[TERM_LINE_LEN];
    size_t len;
    /* Crash screen text is anchored to the logical canvas top-left.
     * Keep a small, consistent margin so the first glyph is never vertically
     * centered or horizontally shifted by the normal desktop layout. */
    int y = 16;
    int text_x = 16;

    fill_rect(0, 0, OS_WIDTH, OS_HEIGHT, color_crash_blue);

    y = bsod_draw_wrapped(text_x, y, OS_WIDTH - 16,
                          "A problem has been detected!!! HaloxOS has been shut down to prevent damage to your machine.",
                          color_white, color_crash_blue, false);
    y += 10;   /* one blank row before the CODE line */

    line[0] = '\0';
    len = 0;
    append_string(line, &len, sizeof(line), "CODE: ");
    append_string(line, &len, sizeof(line), name);
    draw_text(text_x, y, line, color_white, color_crash_blue, false);
    y += 30;

    /* Wrap the diagnostics to the live canvas instead of hard-breaking
     * them at the 640x480 column: wider screens keep the whole sentence
     * on one row, narrower ones wrap rather than run off the right edge. */
    y = bsod_draw_wrapped(text_x, y, OS_WIDTH - 16,
                          "Looks like you've got something wrong with the system, please make sure to report this issue:",
                          color_white, color_crash_blue, false);
    y = bsod_draw_wrapped(text_x, y, OS_WIDTH - 16,
                          "https://github.com/svh03ra/HaloxOS",
                          color_white, color_crash_blue, false);
    y += 10;   /* one blank row before the retry hint */

    y = bsod_draw_wrapped(text_x, y, OS_WIDTH - 16,
                          "Please try again after restarting your device if you're tired...",
                          color_white, color_crash_blue, false);
    y += 30;   /* three blank rows before the status line */

    line[0] = '\0';
    len = 0;
    bsod_append_field(line, &len, sizeof(line), "*** STATUS: Error=",
                      frame != NULL ? frame->error_code : 0);
    bsod_append_field(line, &len, sizeof(line), " Vector=",
                      frame != NULL ? frame->vector : 0);
    draw_text(text_x, y, line, color_white, color_crash_blue, false);
    y += 20;

    /* The register dump is part of the crash report, not a debug-build
     * nicety: a release build is exactly what a user reports a crash from. */
    {
        draw_text(text_x, y, "Machine Info:", color_white, color_crash_blue, false);
        y += 10;

        if (frame != NULL) {
            line[0] = '\0';
            len = 0;
            bsod_append_field(line, &len, sizeof(line), "EAX=", frame->eax);
            bsod_append_field(line, &len, sizeof(line), " EBX=", frame->ebx);
            bsod_append_field(line, &len, sizeof(line), " ECX=", frame->ecx);
            bsod_append_field(line, &len, sizeof(line), " EDX=", frame->edx);
            draw_text(text_x, y, line, color_white, color_crash_blue, false);
            y += 10;

            line[0] = '\0';
            len = 0;
            bsod_append_field(line, &len, sizeof(line), "ESI=", frame->esi);
            bsod_append_field(line, &len, sizeof(line), " EDI=", frame->edi);
            bsod_append_field(line, &len, sizeof(line), " EBP=", frame->ebp);
            bsod_append_field(line, &len, sizeof(line), " ESP=", frame->esp);
            draw_text(text_x, y, line, color_white, color_crash_blue, false);
            y += 10;

            line[0] = '\0';
            len = 0;
            bsod_append_field(line, &len, sizeof(line), "EIP=", frame->eip);
            bsod_append_field(line, &len, sizeof(line), " CS=", frame->cs);
            bsod_append_field(line, &len, sizeof(line), " DS=", frame->ds);
            bsod_append_field(line, &len, sizeof(line), " ES=", frame->es);
            draw_text(text_x, y, line, color_white, color_crash_blue, false);
            y += 10;

            line[0] = '\0';
            len = 0;
            bsod_append_field(line, &len, sizeof(line), "FS=", frame->fs);
            bsod_append_field(line, &len, sizeof(line), " GS=", frame->gs);
            bsod_append_field(line, &len, sizeof(line), " EFLAGS=", frame->eflags);
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
    if (include_mmap && crash_mmap_addr != 0 && crash_mmap_length != 0) {
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
                append_string(line, &len, sizeof(line), " ");
            }
            bsod_append_field(line, &len, sizeof(line), "0x", (uint32_t)entry->base_addr);
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

#if !CRASH_TRACE_ENABLED
    y = crash_render_flat_trace(text_x, y, frame);
#endif

    (void)text_x;
    return y;
}

/*
 * Last-resort crash report in 80x25 VGA text memory.
 *
 * The BSOD depends on a working linear framebuffer. In native text mode, or
 * when painting the graphical report faults, the machine would otherwise sit
 * on the last frame - the frozen screen this handler exists to avoid.
 * 0xB8000 is plain memory, so these writes cannot fault no matter how badly
 * the video state was left by the crashing code.
 */
static void crash_text_put(int row, int col, const char *text, uint8_t attr) {
    volatile uint16_t *vga = (volatile uint16_t *)(uintptr_t)0xB8000;

    if (row < 0 || row >= VGA_TEXT_ROWS) {
        return;
    }
    for (int i = 0; text[i] != '\0' && col + i < VGA_TEXT_COLS; ++i) {
        vga[row * VGA_TEXT_COLS + col + i] =
            (uint16_t)(((uint16_t)attr << 8) | (uint8_t)text[i]);
    }
}

static void crash_text_report(const char *name, const CpuExceptionFrame *frame) {
    char line[80];
    size_t len;
    const uint8_t attr = 0x1Fu; /* white on blue */
    volatile uint16_t *vga = (volatile uint16_t *)(uintptr_t)0xB8000;

    for (int i = 0; i < VGA_TEXT_COLS * VGA_TEXT_ROWS; ++i) {
        vga[i] = (uint16_t)(((uint16_t)attr << 8) | ' ');
    }

    crash_text_put(1, 2, "***** HALOXOS CRASH!!! *****", attr);
    crash_text_put(3, 2, "A fatal CPU exception stopped the system:", attr);
    crash_text_put(4, 4, name, attr);

    line[0] = '\0';
    len = 0;
    bsod_append_field(line, &len, sizeof(line), "STATUS: Error=",
                      frame != NULL ? frame->error_code : 0);
    bsod_append_field(line, &len, sizeof(line), " Vector=",
                      frame != NULL ? frame->vector : 0);
    crash_text_put(6, 2, line, attr);

    if (frame != NULL) {
        line[0] = '\0';
        len = 0;
        bsod_append_field(line, &len, sizeof(line), "EIP=", frame->eip);
        bsod_append_field(line, &len, sizeof(line), " CS=", frame->cs);
        bsod_append_field(line, &len, sizeof(line), " EFLAGS=", frame->eflags);
        crash_text_put(7, 2, line, attr);

        line[0] = '\0';
        len = 0;
        bsod_append_field(line, &len, sizeof(line), "EAX=", frame->eax);
        bsod_append_field(line, &len, sizeof(line), " EBX=", frame->ebx);
        bsod_append_field(line, &len, sizeof(line), " ECX=", frame->ecx);
        bsod_append_field(line, &len, sizeof(line), " EDX=", frame->edx);
        crash_text_put(8, 2, line, attr);

        line[0] = '\0';
        len = 0;
        bsod_append_field(line, &len, sizeof(line), "ESI=", frame->esi);
        bsod_append_field(line, &len, sizeof(line), " EDI=", frame->edi);
        bsod_append_field(line, &len, sizeof(line), " EBP=", frame->ebp);
        bsod_append_field(line, &len, sizeof(line), " ESP=", frame->esp);
        crash_text_put(9, 2, line, attr);
    }

    crash_text_put(11, 2, "Looks like you've got something wrong with the system, please make sure to", attr);
    crash_text_put(12, 2, "report this issue:", attr);
    crash_text_put(13, 2, "https://github.com/svh03ra/HaloxOS", attr);

    crash_text_put(15, 2, "The graphical crash screen could not be drawn; this is the text fallback.", attr);
    crash_text_put(16, 2, "The full register and backtrace dump was written to the serial log (COM1)", attr);
    crash_text_put(17, 2, "and to the boot medium when one is writable.", attr);
    crash_text_put(19, 2, "Please try again after restarting your device...", attr);
    crash_text_put(21, 2, "SYSTEM STOP.", attr);

    /* keep the hardware cursor out of the report */
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

/*
 * Crash-screen paint guard (interrupts.asm).
 *
 * crash_guard_call() runs one paint and returns the row it ended at, or -1
 * when the paint faulted and the exception handler recovered to the guard.
 * Moves the "did the paint finish?" branch into assembly: a C-level second
 * return is at the mercy of the optimiser, and getting it wrong is what turns
 * a fallback screen into an endless fault loop.
 */
extern volatile uint8_t crash_guard_active;
extern int crash_guard_call(int (*paint)(void));
extern void crash_guard_restore(void) __attribute__((noreturn));

static volatile uint32_t crash_guard_trips = 0;
static const char *volatile crash_paint_name;
static const CpuExceptionFrame *volatile crash_paint_frame;
#if CRASH_TRACE_ENABLED
static volatile int crash_trace_y = 0;
#endif

static int crash_paint_full(void) {
    int y = bsod_paint(crash_paint_name, crash_paint_frame, true);

    present();
    return y;
}

static int crash_paint_compact(void) {
    int y = bsod_paint(crash_paint_name, crash_paint_frame, false);

    present();
    return y;
}

#if CRASH_TRACE_ENABLED
static int crash_paint_trace_view(void) {
    crash_cube_last_count = crash_pit_count();
    crash_cube_badge_redraw();
    present();
    crash_serial_dump_trace(crash_paint_frame);
    crash_render_trace_view(16, crash_trace_y, crash_paint_frame);
    return 0;
}
#endif

/* Release builds and the fault fallback: keep the cube badge spinning on the
 * finished report. Only the badge rectangle is touched, so the report around
 * it is never disturbed. */
static void crash_cube_animate_forever(void) {
    crash_cube_last_count = crash_pit_count();
    crash_cube_badge_redraw();
    present();
    for (;;) {
        if (crash_cube_tick()) {
            crash_cube_badge_redraw();
            present();
        }
    }
}

/*
 * Paint the full crash screen, then animate it.
 *
 * Everything that touches the screen runs under the crash guard: a fault in
 * the paint (bad framebuffer, corrupted graphics state, stale boot pointer)
 * returns here and the compact report is drawn instead, and a fault in the
 * interactive trace view falls back to the cube animation. Only when even the
 * compact report cannot be painted does the VGA text fallback take over. The
 * boot-log flush runs after the report is on screen, so a slow or dead disk
 * can no longer hide the crash.
 */
static void render_bsod(const char *name, const CpuExceptionFrame *frame) {
    volatile bool framebuffer_ok =
        !vga_native_text_mode_active() && fb.address != NULL &&
        fb.width >= (uint32_t)OS_WIDTH;
    int y = 16;

    serial_trace("RENDER", "crash screen render started");
    serial_trace_concat("RENDER", "exception: ", name);
    serial_trace_hex_value("RENDER", "framebuffer", (uint32_t)(uintptr_t)fb.address);
    serial_trace_hex_value("RENDER", "framebuffer size", ((uint32_t)fb.width << 16) | fb.height);
    serial_trace_hex_value("RENDER", "framebuffer bpp", fb.bpp);
    if (!framebuffer_ok) {
        serial_trace("ERROR", "RENDER no usable framebuffer: using the VGA text fallback");
    }

    /* A previous crash's badge background must not survive into this one. */
    crash_cube_bg_saved = false;

    crash_paint_name = name;
    crash_paint_frame = frame;

    /* A crash inside an app render leaves the window clip stuck on that app's
     * client rectangle (app_dispatch.c sets it around each app draw and only
     * clears it after the call returns, which never happens on a fault). Reset
     * to the full canvas so the BSOD covers the whole screen. */
    if (framebuffer_ok) {
        graphics_reset_window_clip();
        y = crash_guard_call(crash_paint_full);
        if (y >= 0) {
            serial_trace("RENDER", "crash screen presented to framebuffer: SUCCESS");
        } else {
            /* The full report faulted while painting. Reset the graphics state
             * and retry without the memory map: the status and register dump
             * are what has to come out, and they walk no boot-time pointer. */
            ++crash_guard_trips;
            serial_trace("ERROR", "RENDER full crash screen faulted: retrying the compact report");
            graphics_reset_window_clip();
            y = crash_guard_call(crash_paint_compact);
            if (y >= 0) {
                serial_trace("RENDER", "compact crash screen presented to framebuffer: SUCCESS");
            } else {
                ++crash_guard_trips;
                serial_trace("ERROR", "RENDER compact crash screen faulted: using the VGA text fallback");
                framebuffer_ok = false;
            }
        }
    }

    if (!framebuffer_ok) {
        crash_text_report(name, frame);
        y = OS_HEIGHT;
    }

    /* The report is visible before any medium I/O: the polled ATA flush can
     * block behind a missing or dead disk, and that must never be the reason
     * a crash shows no screen at all. */
    serial_bootlog_flush(name);

#if CRASH_TRACE_ENABLED
    if (framebuffer_ok) {
        /* The interactive view owns the screen. Drawing it (and its badge) is
         * guarded too: a fault returns -1 and the cube keeps the completed
         * report alive instead of freezing on a partial redraw. */
        crash_trace_y = y + 10;
        if (crash_guard_call(crash_paint_trace_view) < 0) {
            serial_trace("ERROR", "RENDER trace view faulted: continuing with the cube badge");
        }
    }
#else
    (void)y;
#endif

    if (framebuffer_ok) {
        crash_cube_animate_forever();
    }

    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

int cpu_exception_handler(const CpuExceptionFrame *frame) {
    static bool crash_handler_active = false;
    const char *reason = debug_forced_fault_reason;
    const char *name = frame != NULL ? cpu_exception_name(frame->vector) : "CPU Exception";

    /* The crash screen's paint pass faulted. Abandon the half-painted frame
     * instead of retrying it blindly: returning through the guard is what
     * turns a renderer that cannot draw into a fallback screen rather than a
     * machine stuck on an old or blank frame. */
    if (crash_guard_active) {
        crash_guard_active = 0;
        ++crash_guard_trips;
        serial_write_string("[ERROR]: Crash screen paint faulted - switching to the fallback renderer\n");
        if (frame != NULL) {
            serial_write_string("[ERROR]: paint exception vector=");
            serial_write_uint(frame->vector);
            serial_write_string(" EIP=");
            serial_write_hex32(frame->eip);
            serial_write_string("\n");
        }
        crash_guard_restore();
    }

    /* #BP and #OF are architectural traps, not fatal machine faults: log the
     * event and return to the instruction after the trap. isr_common_stub
     * iretds with the original CPU frame when this returns non-zero. */
    if (frame != NULL && (frame->vector == 3 || frame->vector == 4)) {
        serial_write_string("[WARN]: resumable trap vector=");
        serial_write_uint(frame->vector);
        serial_write_string(" EIP=");
        serial_write_hex32(frame->eip);
        serial_write_string(" - continuing\n");
        debug_forced_fault_reason = NULL;
        return 1;
    }

    /* Nested crash outside a guarded paint. Never redraw here: redrawing on a
     * broken framebuffer is exactly what turns one fault into an endless
     * exception loop. Keep the last complete frame on screen and stop. */
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
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    crash_handler_active = true;
    serial_dump_cpu_exception(name, reason, frame);
    debug_forced_fault_reason = NULL;
    /* render_bsod() paints the report first and flushes the boot log after it:
     * the polled ATA flush can block behind a dead disk and must never be able
     * to keep the crash screen from appearing. */
    render_bsod(name, frame);
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
    return 0;
}

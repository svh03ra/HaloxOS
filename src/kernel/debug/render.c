// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, debugger graphical of render core.

// This repository is licensed under the GNU General Public License.

static uint8_t debug_terminal_line_color(int index, const char *line) {
    if (index == 0) {
        return color_blue_dark;
    }
    if (index == 1) {
        return color_gray_dark;
    }
    if (starts_with(line, "error")) {
        return color_red;
    }
    if (starts_with(line, "warning")) {
        return color_orange;
    }
    if (contains_text(line, "edited")) {
        return color_red;
    }
    return color_black;
}

static void render_debug_memory_hex(int x, int y, int w, int h) {
    uint32_t limit = debug_memory_limit();
    int rows = debug_memory_hex_rows();
    int row_y = y + 42;
    (void)h;

    draw_text(x + 10, y + 28, "Address      00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F", color_gray_dark, color_white, true);
    for (int row = 0; row < rows; ++row) {
        uint32_t base = debug_memory_base + (uint32_t)row * DEBUG_MEMORY_BYTES_PER_ROW;
        char address_text[16] = {0};
        size_t len = 0;

        if (base >= limit) {
            break;
        }

        append_hex32(address_text, &len, sizeof(address_text), base);
        append_char(address_text, &len, sizeof(address_text), ':');
        draw_text(x + 10, row_y, address_text, color_blue_dark, color_white, true);

        for (int col = 0; col < DEBUG_MEMORY_BYTES_PER_ROW; ++col) {
            uint32_t address = base + (uint32_t)col;
            int byte_x = x + 106 + col * 24;
            char byte_text[3] = {0};
            size_t blen = 0;
            uint8_t text_color = color_black;
            bool selected = address == debug_memory_cursor;
            bool edited = debug_memory_address_edited(address);

            if (address >= limit) {
                draw_text(byte_x, row_y, "--", color_gray_dark, color_white, true);
                continue;
            }

            append_hex8(byte_text, &blen, sizeof(byte_text), debug_memory_read_byte(address));
            if (selected) {
                fill_rect(byte_x - 2, row_y - 1, 20, 10, color_blue);
                text_color = edited ? color_red : color_white;
            } else if (edited) {
                text_color = color_red;
            }
            draw_text(byte_x, row_y, byte_text, text_color, selected ? color_blue : color_white, true);
        }
        row_y += 10;
    }

    {
        char selected[TERM_LINE_LEN] = {0};
        size_t len = 0;
        append_char(selected, &len, sizeof(selected), 'S');
        append_char(selected, &len, sizeof(selected), 'e');
        append_char(selected, &len, sizeof(selected), 'l');
        append_char(selected, &len, sizeof(selected), ':');
        append_char(selected, &len, sizeof(selected), ' ');
        append_hex32(selected, &len, sizeof(selected), debug_memory_cursor);
        append_char(selected, &len, sizeof(selected), ' ');
        append_char(selected, &len, sizeof(selected), '=');
        append_char(selected, &len, sizeof(selected), ' ');
        append_char(selected, &len, sizeof(selected), '0');
        append_char(selected, &len, sizeof(selected), 'x');
        append_hex8(selected, &len, sizeof(selected), debug_memory_read_byte(debug_memory_cursor));
        draw_text_clipped(x + 10, y + h - 38, w - 20, selected,
                          debug_memory_address_edited(debug_memory_cursor) ? color_red : color_blue_dark,
                          color_white,
                          true);
    }
}

static void render_debug_memory_scrollbar(void) {
    int scroll_x = debug_memory_scroll_x();
    int bitmap_y = debug_memory_visual_y();
    int track_y = bitmap_y + 16;
    int track_h = debug_memory_visual_h() - 32;
    int thumb_h = 20;
    int thumb_track = track_h - thumb_h;
    uint32_t max_base = debug_memory_max_base();
    int thumb_y = track_y + 1;
    bool thumb_hover;
    bool track_hover;

    if (debug_memory_base > max_base) {
        debug_memory_base = max_base;
    }
    thumb_y += debug_memory_thumb_offset(debug_memory_base, max_base, thumb_track);

    draw_button(scroll_x, bitmap_y - 2, 16, 16, "^",
                debug_memory_base > 0 ? color_gray_light : color_gray,
                color_black, color_black);
    fill_rect(scroll_x, track_y, 16, track_h, color_gray_light);
    draw_rect(scroll_x, track_y, 16, track_h, color_gray_dark);

    thumb_hover = point_in_rect(mouse.x, mouse.y, scroll_x + 2, thumb_y, 12, thumb_h);
    track_hover = point_in_rect(mouse.x, mouse.y, scroll_x, track_y, 16, track_h);
    if (thumb_hover || track_hover) {
        cursor_hand_hint = true;
    }
    hover_widget_add(scroll_x + 2, thumb_y, 12, thumb_h);
    hover_widget_add(scroll_x, track_y, 16, track_h);

    if (max_base != 0) {
        fill_rect(scroll_x + 2, thumb_y, 12, thumb_h, thumb_hover ? color_blue_dark : color_blue);
        draw_rect(scroll_x + 2, thumb_y, 12, thumb_h, color_black);
    } else {
        fill_rect(scroll_x + 2, track_y + 2, 12, thumb_h, color_gray);
        draw_rect(scroll_x + 2, track_y + 2, 12, thumb_h, color_black);
    }

    draw_button(scroll_x, bitmap_y + debug_memory_visual_h() - 16, 16, 16, "v",
                debug_memory_base < max_base ? color_gray_light : color_gray,
                color_black, color_black);
}

static void render_debug_memory_visual(int x, int y, int w, int h) {
    uint32_t limit = debug_memory_limit();
    int vx = debug_memory_visual_x();
    int vy = debug_memory_visual_y();
    int vw = debug_memory_visual_w();
    int vh = debug_memory_visual_h();
    uint32_t visible = (uint32_t)vw * (uint32_t)vh;
    (void)x;
    (void)y;
    (void)w;
    (void)h;

    fill_rect(vx - 1, vy - 1, vw + 2, vh + 2, color_black);

    /*
     * Bulk bitmap blit.
     *
     * The old loop called draw_pixel() for every visible byte: bounds
     * test, clip test, plane election and a function call per pixel, then
     * one byte read from physical memory.  At the default 430x240 that is
     * 103,200 calls per repaint, and scrolling repaints the whole bitmap
     * every key repeat.  Resolve the plane election and the 256-entry
     * palette mapping once per frame, then walk rows straight into the
     * live shadow plane.  The indexed plane is a raw byte copy; the RGB565
     * plane expands four bytes per iteration through the LUT.
     */
    {
        bool want_index = present_need_index_plane();
        bool want_rgb = present_need_rgb_plane();
        uint16_t lut[256];
        uint32_t base = debug_memory_base;

        if (want_rgb) {
            for (int i = 0; i < 256; ++i) {
                lut[i] = pixel_rgb565_fast((uint8_t)i);
            }
        }

        for (int py = 0; py < vh; ++py) {
            uint32_t address = base + (uint32_t)py * (uint32_t)vw;
            uint32_t count = 0;
            uint8_t *dst_index = &backbuffer[(size_t)(vy + py) * OS_WIDTH + vx];
            uint16_t *dst_rgb = &backbuffer_rgb565[(size_t)(vy + py) * OS_WIDTH + vx];

            if (address < limit) {
                count = limit - address;
                if (count > (uint32_t)vw) {
                    count = (uint32_t)vw;
                }
            }

            if (want_index && count > 0) {
                memcpy_local(dst_index, (const void *)(uintptr_t)address, (size_t)count);
            } else if (want_rgb && count > 0) {
                const uint8_t *src = (const uint8_t *)(uintptr_t)address;
                uint32_t px = 0;

                for (; px + 4u <= count; px += 4u) {
                    uint32_t word;

                    __builtin_memcpy(&word, src + px, sizeof(word));
                    dst_rgb[px + 0] = lut[word & 0xFFu];
                    dst_rgb[px + 1] = lut[(word >> 8) & 0xFFu];
                    dst_rgb[px + 2] = lut[(word >> 16) & 0xFFu];
                    dst_rgb[px + 3] = lut[(word >> 24) & 0xFFu];
                }
                for (; px < count; ++px) {
                    dst_rgb[px] = lut[src[px]];
                }
            }

            if (count < (uint32_t)vw) {
                uint32_t tail = (uint32_t)vw - count;

                if (want_index) {
                    memset_local(dst_index + count, color_black, (size_t)tail);
                }
                if (want_rgb) {
                    memset16_local(dst_rgb + count, lut[color_black], (size_t)tail);
                }
            }
        }
    }

    if (debug_memory_cursor >= debug_memory_base && debug_memory_cursor < debug_memory_base + visible) {
        uint32_t offset = debug_memory_cursor - debug_memory_base;
        int cx = vx + (int)(offset % (uint32_t)vw);
        int cy = vy + (int)(offset / (uint32_t)vw);
        draw_rect(cx - 2, cy - 2, 5, 5, color_blue);
        draw_pixel(cx, cy, debug_memory_address_edited(debug_memory_cursor) ? color_red : color_white);
    }

    render_debug_memory_scrollbar();
}

/*
 * Width/Height boxes for the bitmap resolution, on their own row between
 * the bitmap and the Memory Bytes footer.  The row is placed from the
 * bottom of the view, so it stays clear of the header and of a bitmap
 * grown to its maximum height.  "max" names the largest size this screen
 * can show, because anything above it is clamped to that.
 */
static void render_debug_memory_size_fields(void) {
    int row_y = debug_memory_size_row_y();
    int x = debug_memory_view_x();
    char hint[40] = {0};
    size_t len = 0;

    draw_text(x + 10, row_y + 3, "Width:", color_black, color_white, true);
    draw_text(x + 120, row_y + 3, "Height:", color_black, color_white, true);

    for (int which = 0; which < 2; ++which) {
        int bx = debug_memory_size_box_x(which);
        bool focused = debug_memory_size_focus == which;

        fill_rect(bx, row_y, DEBUG_MEMORY_SIZE_BOX_W, DEBUG_MEMORY_SIZE_BOX_H, color_white);
        draw_rect(bx, row_y, DEBUG_MEMORY_SIZE_BOX_W, DEBUG_MEMORY_SIZE_BOX_H,
                  focused ? color_blue_dark : color_gray_dark);
        draw_text(bx + 4, row_y + 3, debug_memory_size_text[which],
                  color_black, color_white, true);
        if (focused && ((timer_ticks / TERMINAL_CURSOR_BLINK_TICKS) & 1u) == 0) {
            fill_rect(bx + 4 + debug_memory_size_len[which] * 8, row_y + 3, 1, 8, color_blue_dark);
        }
    }

    append_string(hint, &len, sizeof(hint), "max ");
    append_uint(hint, &len, sizeof(hint), (uint32_t)debug_memory_visual_max_w());
    append_char(hint, &len, sizeof(hint), 'x');
    append_uint(hint, &len, sizeof(hint), (uint32_t)debug_memory_visual_max_h());
    draw_text(x + 244, row_y + 3, hint, color_gray_dark, color_white, true);
}

static void render_debug_memory_view(void) {
    int x = debug_memory_view_x();
    int y = debug_memory_view_y();
    int w = debug_memory_view_w();
    int h = debug_memory_view_h();
    char memory_text[64] = {0};
    char footer[TERM_LINE_LEN] = {0};
    size_t len = 0;

    fill_rect(x, y, w, h, color_white);
    draw_rect(x, y, w, h, color_black);
    draw_text(x + 10, y + 10, "Modes |", color_black, color_white, true);
    draw_text(x + 74, y + 10, debug_memory_mode == DEBUG_MEMORY_MODE_HEX ? "[Hex]" : "Hex",
              debug_memory_mode == DEBUG_MEMORY_MODE_HEX ? color_blue_dark : color_gray_dark,
              color_white,
              true);
    draw_text(x + 130, y + 10, debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL ? "[Visual]" : "Visual",
              debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL ? color_blue_dark : color_gray_dark,
              color_white,
              true);
    draw_text(x + 220, y + 10, "F1=Hex F2=Visual", color_gray_dark, color_white, true);

    if (debug_memory_mode == DEBUG_MEMORY_MODE_HEX) {
        render_debug_memory_hex(x, y, w, h);
    } else {
        render_debug_memory_visual(x, y, w, h);
        render_debug_memory_size_fields();
    }

    format_single_memory_amount(memory_text, sizeof(memory_text), debug_memory_limit());
    append_char(footer, &len, sizeof(footer), 'M');
    append_char(footer, &len, sizeof(footer), 'e');
    append_char(footer, &len, sizeof(footer), 'm');
    append_char(footer, &len, sizeof(footer), 'o');
    append_char(footer, &len, sizeof(footer), 'r');
    append_char(footer, &len, sizeof(footer), 'y');
    append_char(footer, &len, sizeof(footer), ' ');
    append_char(footer, &len, sizeof(footer), 'B');
    append_char(footer, &len, sizeof(footer), 'y');
    append_char(footer, &len, sizeof(footer), 't');
    append_char(footer, &len, sizeof(footer), 'e');
    append_char(footer, &len, sizeof(footer), 's');
    append_char(footer, &len, sizeof(footer), ':');
    append_char(footer, &len, sizeof(footer), ' ');
    for (int i = 0; memory_text[i] != '\0'; ++i) {
        append_char(footer, &len, sizeof(footer), memory_text[i]);
    }
    append_char(footer, &len, sizeof(footer), ' ');
    append_char(footer, &len, sizeof(footer), 'P');
    append_char(footer, &len, sizeof(footer), 'r');
    append_char(footer, &len, sizeof(footer), 'e');
    append_char(footer, &len, sizeof(footer), 's');
    append_char(footer, &len, sizeof(footer), 's');
    append_char(footer, &len, sizeof(footer), ' ');
    append_char(footer, &len, sizeof(footer), 'E');
    append_char(footer, &len, sizeof(footer), 'S');
    append_char(footer, &len, sizeof(footer), 'C');
    append_char(footer, &len, sizeof(footer), ' ');
    append_char(footer, &len, sizeof(footer), 't');
    append_char(footer, &len, sizeof(footer), 'o');
    append_char(footer, &len, sizeof(footer), ' ');
    append_char(footer, &len, sizeof(footer), 'e');
    append_char(footer, &len, sizeof(footer), 'x');
    append_char(footer, &len, sizeof(footer), 'i');
    append_char(footer, &len, sizeof(footer), 't');
    append_char(footer, &len, sizeof(footer), '.');
    draw_text_clipped(x + 10, y + h - 18, w - 20, footer, color_gray_dark, color_white, true);
}

/*
 * FPS overlay: one fixed-size box in the top-right corner.  A fixed box
 * matters for more than looks: the box is the only rectangle the partial
 * scan-out has to refresh, so changing the width with the digits would
 * leave stale pixels beside a shrinking number and force a wider present.
 * The widest reading, "FPS 100.0" at 9 characters, still fits.
 */
#define DEBUG_FPS_BOX_X (OS_WIDTH - 102)
#define DEBUG_FPS_BOX_Y 6
#define DEBUG_FPS_BOX_W 84
#define DEBUG_FPS_BOX_H 14

static void debug_fps_overlay_rect(int *x, int *y, int *w, int *h) {
    *x = DEBUG_FPS_BOX_X - 2;
    *y = DEBUG_FPS_BOX_Y - 2;
    *w = DEBUG_FPS_BOX_W + 4;
    *h = DEBUG_FPS_BOX_H + 4;
}

static void render_fps_overlay(void) {
    char text[32] = {0};
    size_t len = 0;
    uint32_t whole = fps_current / 10u;
    uint32_t frac = fps_current % 10u;
    bool blink_red = fps_lagging && ((timer_ticks / 6u) & 1u) != 0u;

    if (!fps_overlay_on) {
        return;
    }

    append_string(text, &len, sizeof(text), "FPS ");
    append_uint(text, &len, sizeof(text), whole);
    append_char(text, &len, sizeof(text), '.');
    append_uint(text, &len, sizeof(text), frac);

    fill_rect(DEBUG_FPS_BOX_X, DEBUG_FPS_BOX_Y, DEBUG_FPS_BOX_W, DEBUG_FPS_BOX_H, color_black);
    fill_rect(DEBUG_FPS_BOX_X + 1, DEBUG_FPS_BOX_Y + 1, DEBUG_FPS_BOX_W - 2, DEBUG_FPS_BOX_H - 2,
              blink_red ? color_red : color_white);
    draw_text(DEBUG_FPS_BOX_X + 6, DEBUG_FPS_BOX_Y + 4, text, color_black,
              blink_red ? color_red : color_white, true);
}

static void render_debug_overlay(void) {
    int x = 42;
    int y = 46;
    int w = OS_WIDTH - 84;
    int h = OS_HEIGHT / 2;
    int max_lines = (h - 58) / 10;
    int start = debug_term.line_count > max_lines ? debug_term.line_count - max_lines : 0;
    int line_y = y + 32;

    render_fps_overlay();

    if (!debug_overlay_open) {
        return;
    }

    if (debug_memory_view_open) {
        render_debug_memory_view();
        return;
    }

    fill_rect(x, y, w, h, color_white);
    draw_rect(x, y, w, h, color_black);
    draw_text(x + 10, y + 10, "HaloxOS Debugger", color_blue_dark, color_white, true);

    /* Extended horizontal log: Ctrl+Left/Right pans the log so the
     * very long breakpoint trace lines can be read past the window
     * edge - the same way the crash handler's backtrace view scrolls.
     * The pan is clamped to what the longest visible line still hides;
     * a <NNN> hint in the title bar shows the current column. */
    {
        int max_len = 0;
        int visible_chars = (w - 20) / 8;

        for (int i = start; i < debug_term.line_count; ++i) {
            int len = (int)strlen_local(debug_term.lines[i]);
            if (len > max_len) {
                max_len = len;
            }
        }
        if (debug_log_scroll_x < 0) {
            debug_log_scroll_x = 0;
        }
        if (max_len <= visible_chars) {
            debug_log_scroll_x = 0;
        } else if (debug_log_scroll_x > (max_len - visible_chars) * 8) {
            debug_log_scroll_x = (max_len - visible_chars) * 8;
        }

        if (debug_log_scroll_x > 0 || max_len > visible_chars) {
            char pos[32] = {0};
            size_t plen = 0;

            append_char(pos, &plen, sizeof(pos), '<');
            append_uint(pos, &plen, sizeof(pos), (uint32_t)(debug_log_scroll_x / 8));
            append_char(pos, &plen, sizeof(pos), '>');
            draw_text(x + w - 60, y + 10, pos, color_gray_dark, color_white, true);
        }
    }

    line_y = y + 32;
    {
        int visible_chars = (w - 20) / 8;
        int skip_chars = debug_log_scroll_x / 8;

        for (int i = start; i < debug_term.line_count; ++i) {
            uint8_t text_color = debug_terminal_line_color(i, debug_term.lines[i]);
            const char *line_text = debug_term.lines[i];
            int full_len = (int)strlen_local(line_text);
            int shown_len = full_len - skip_chars;

            if (skip_chars > 0) {
                if (skip_chars >= full_len) {
                    line_text = "";
                    shown_len = 0;
                } else {
                    line_text += skip_chars;
                }
            }
            draw_text_clipped(x + 10, line_y, w - 20, line_text, text_color, color_white, true);
            /* edge horns like the crash handler backtrace: << marks
             * hidden text on the left, >> hidden text on the right */
            if (skip_chars > 0 && shown_len > 0) {
                draw_text(x + 10, line_y, "<<", color_yellow, color_white, true);
            }
            if (shown_len > visible_chars) {
                draw_text(x + w - 26, line_y, ">>", color_yellow, color_white, true);
            }
            line_y += 10;
        }
    }
    draw_text(x + 10, y + h - 18, "DBG:", color_black, color_white, true);
    {
        int prompt_w = 48;
        int input_w = w - prompt_w - 12;
        int max_chars = input_w / 8;
        int input_start = debug_term.input_len > max_chars ? debug_term.input_len - max_chars : 0;
        draw_text_clipped(x + prompt_w, y + h - 18, input_w, &debug_term.input[input_start], color_black, color_white, true);
        if (((timer_ticks / TERMINAL_CURSOR_BLINK_TICKS) & 1u) == 0) {
            int cursor_chars = debug_term.input_len - input_start;
            if (cursor_chars > max_chars) {
                cursor_chars = max_chars;
            }
            draw_text(x + prompt_w + cursor_chars * 8, y + h - 18, "_", color_black, color_white, true);
        }
    }
}

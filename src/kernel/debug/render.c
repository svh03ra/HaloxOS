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

static void render_debug_memory_visual(int x, int y, int w, int h) {
    uint32_t limit = debug_memory_limit();
    int vx = x + (w - DEBUG_MEMORY_VISUAL_W) / 2;
    int vy = y + 54;
    uint32_t visible = (uint32_t)DEBUG_MEMORY_VISUAL_W * DEBUG_MEMORY_VISUAL_H;
    (void)h;

    if (debug_memory_cursor < debug_memory_base) {
        debug_memory_base = debug_memory_cursor;
    } else if (debug_memory_cursor >= debug_range_end(debug_memory_base, visible)) {
        debug_memory_base = debug_memory_cursor - visible + 1;
    }

    fill_rect(vx - 1, vy - 1, DEBUG_MEMORY_VISUAL_W + 2, DEBUG_MEMORY_VISUAL_H + 2, color_black);
    for (int py = 0; py < DEBUG_MEMORY_VISUAL_H; ++py) {
        for (int px = 0; px < DEBUG_MEMORY_VISUAL_W; ++px) {
            uint32_t address = debug_memory_base + (uint32_t)py * DEBUG_MEMORY_VISUAL_W + (uint32_t)px;
            draw_pixel(vx + px, vy + py, address < limit ? debug_memory_read_byte(address) : color_black);
        }
    }

    if (debug_memory_cursor >= debug_memory_base && debug_memory_cursor < debug_memory_base + visible) {
        uint32_t offset = debug_memory_cursor - debug_memory_base;
        int cx = vx + (int)(offset % DEBUG_MEMORY_VISUAL_W);
        int cy = vy + (int)(offset / DEBUG_MEMORY_VISUAL_W);
        draw_rect(cx - 2, cy - 2, 5, 5, color_blue);
        draw_pixel(cx, cy, debug_memory_address_edited(debug_memory_cursor) ? color_red : color_white);
    }
}

static void render_debug_memory_view(void) {
    int x = 16;
    int y = 26;
    int w = OS_WIDTH - 32;
    int h = OS_HEIGHT - 54;
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

static void render_debug_overlay(void) {
    int x = 42;
    int y = 46;
    int w = OS_WIDTH - 84;
    int h = OS_HEIGHT / 2;
    int max_lines = (h - 58) / 10;
    int start = debug_term.line_count > max_lines ? debug_term.line_count - max_lines : 0;
    int line_y = y + 32;

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
    for (int i = start; i < debug_term.line_count; ++i) {
        uint8_t text_color = debug_terminal_line_color(i, debug_term.lines[i]);
        draw_text_clipped(x + 10, line_y, w - 20, debug_term.lines[i], text_color, color_white, true);
        line_y += 10;
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

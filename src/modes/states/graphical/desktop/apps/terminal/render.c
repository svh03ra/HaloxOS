// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, terminal rendering.

// This repository is licensed under the GNU General Public License.

static void render_terminal(const Terminal *term, int x, int y, int w, int h) {
    fill_rect(x, y, w, h, color_black);
    draw_rect(x, y, w, h, color_gray);
    int max_lines = (h - 20) / 10;
    int start = term->line_count > max_lines ? term->line_count - max_lines : 0;
    int line_y = y + 4;
    int text_w = w - 8;
    for (int i = start; i < term->line_count; ++i) {
        draw_text_clipped(x + 4, line_y, text_w, term->lines[i], color_green, color_black, true);
        line_y += 10;
    }
    draw_text(x + 4, y + h - 14, "A:\\>", color_green, color_black, true);
    {
        int prompt_w = 36;
        int input_w = w - prompt_w - 8;
        int max_chars = input_w / 8;
        int input_start = term->input_len > max_chars ? term->input_len - max_chars : 0;
        draw_text_clipped(x + prompt_w, y + h - 14, input_w, &term->input[input_start], color_green, color_black, true);
        if (((timer_ticks / TERMINAL_CURSOR_BLINK_TICKS) & 1u) == 0) {
            int cursor_chars = term->input_len - input_start;
            if (cursor_chars > max_chars) {
                cursor_chars = max_chars;
            }
            draw_text(x + prompt_w + cursor_chars * 8, y + h - 14, "_", color_green, color_black, true);
        }
    }
}

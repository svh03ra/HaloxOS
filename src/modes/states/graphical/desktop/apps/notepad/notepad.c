// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: notepad.c, notepad app.

// This repository is licensed under the GNU General Public License.

static void render_notepad(const Window *window) {
    int tx = window->x + 6;
    int ty = window->y + 24;
    int max_h = window->h - 30;
    fill_rect(tx, ty, window->w - 12, max_h, color_white);
    draw_rect(tx, ty, window->w - 12, max_h, color_gray_dark);
    draw_text_block(tx + 4, ty + 4, window->w - 20, max_h - 8, notepad_text[0] ? notepad_text : "Type here...", color_black, color_white, true);
}

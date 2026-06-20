// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, guess number rendering.

// This repository is licensed under the GNU General Public License.

static void render_guess(const Window *window) {
    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_white);
    draw_text(window->x + 14, window->y + 36, guess_message, color_black, color_white, true);
    draw_text(window->x + 14, window->y + 62, "Your guess:", color_black, color_white, true);
    fill_rect(window->x + 14, window->y + 76, 80, 18, color_gray_light);
    draw_rect(window->x + 14, window->y + 76, 80, 18, color_black);
    draw_text(window->x + 18, window->y + 81, guess_input, color_black, color_gray_light, true);
    draw_text(window->x + 14, window->y + 104, "Press ENTER to submit.", color_black, color_white, true);
}

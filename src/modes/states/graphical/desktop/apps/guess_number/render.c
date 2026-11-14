// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, guess number rendering.

// This repository is licensed under the GNU General Public License.

static void render_guess(const Window *window) {
    bool focused = guess_input_focused && active_window == APP_GUESS;
    uint8_t field_bg = focused ? color_white : color_gray_light;
    uint8_t field_border = focused ? color_blue : color_black;

    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_white);
    draw_text(window->x + 14, window->y + 36, guess_message, color_black, color_white, true);
    draw_text(window->x + 14, window->y + 62, "Your guess:", color_black, color_white, true);
    fill_rect(window->x + 14, window->y + 76, 80, 18, field_bg);
    draw_rect(window->x + 14, window->y + 76, 80, 18, field_border);
    if (focused) {
        /* Two-pixel blue frame + caret: the old field was always grey and
         * gave no feedback that a click had put it in text-entry mode. */
        draw_rect(window->x + 15, window->y + 77, 78, 16, color_blue);
    }
    draw_text(window->x + 18, window->y + 81, guess_input, color_black, field_bg, true);
    if (focused) {
        int caret_x = window->x + 18 + (int)strlen_local(guess_input) * 8;

        if (caret_x < window->x + 92) {
            fill_rect(caret_x, window->y + 80, 1, 10, color_blue);
        }
    }
    draw_text(window->x + 14, window->y + 104, "Press ENTER to submit.", color_black, color_white, true);
}

// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, guess number input handling.

// This repository is licensed under the GNU General Public License.

static void handle_guess_submit(void) {
    int value = 0;
    for (int i = 0; i < guess_input_len; ++i) {
        value = value * 10 + (guess_input[i] - '0');
    }
    if (guess_input_len == 0) {
        copy_string(guess_message, "Enter a number first.", sizeof(guess_message));
        return;
    }

    if (value == guess_target) {
        copy_string(guess_message, "Correct! New number ready.", sizeof(guess_message));
        guess_target = rand_range(100) + 1;
    } else if (value < guess_target) {
        copy_string(guess_message, "Too low. Try again.", sizeof(guess_message));
    } else {
        copy_string(guess_message, "Too high. Try again.", sizeof(guess_message));
    }

    guess_input_len = 0;
    guess_input[0] = '\0';
}

/*
 * Clicking the guess field gives it the focused look (blue frame + caret).
 * Keyboard input still goes to the app whenever its window is active, so
 * this is a visual focus state, not a second keyboard routing path.
 */
static void guess_handle_mouse(void) {
    Window *window = &windows[APP_GUESS];

    if (!window->open || active_window != APP_GUESS ||
        !mouse.left || mouse.prev_left) {
        return;
    }
    guess_input_focused = point_in_rect(mouse.x, mouse.y,
                                        window->x + 14, window->y + 76, 80, 18);
}

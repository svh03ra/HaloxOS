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

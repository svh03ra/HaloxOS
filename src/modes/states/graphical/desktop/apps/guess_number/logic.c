// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: logic.c, guess number game logic.

// This repository is licensed under the GNU General Public License.

static void reset_guess(void) {
    guess_input_len = 0;
    guess_input[0] = '\0';
    copy_string(guess_message, "Guess a number from 1 to 100.", sizeof(guess_message));
    guess_target = rand_range(100) + 1;
}

// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, Title Run! input handling.

// This repository is licensed under the GNU General Public License.

static void run_handle_key(KeyEvent event) {
    if (active_window != APP_RUN_GAME || !windows[APP_RUN_GAME].open) {
        return;
    }

    /* smooth ease jump during play: spacebar or arrow up */
    if (run_state == 1 && (event.ch == ' ' || event.code == KEY_UP) &&
        run_player_grounded && run_jump_anim_t >= RUN_JUMP_FRAMES) {
        run_jump_anim_t = 0;               /* start the eased jump arc */
        run_jump_start_y16 = run_py16;
        run_player_grounded = false;
    }
    /* restart after game over / win */
    if (event.code == KEY_ENTER && (run_state == 3 || run_state == 4)) {
        reset_run();
    }
}

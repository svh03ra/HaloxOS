// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: player.c, Coin Run! player movement and squeeze mode.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

/* Carry-style test: the proposed player square overlaps a 16x16 brick. */
static bool coin_run_hit_brick(int next_x, int next_y) {
    int box = coin_run_player_box;

    for (int i = 0; i < coin_run_brick_count; ++i) {
        int bx = coin_run_brick_x[i];
        int by = coin_run_brick_y[i];

        if (next_x + box > bx && bx + 16 > next_x &&
            next_y + box > by && by + 16 > next_y) {
            return true;
        }
    }
    return false;
}

static void coin_run_try_player_position(void) {
    /* Invincibility also ghosts the player through brick walls. */
    if (!coin_run_debug_invincible() &&
        coin_run_hit_brick(coin_run_next_x, coin_run_next_y)) {
        return;
    }

    {
        int max_x = COIN_RUN_FIELD_W - coin_run_player_box;
        int max_y = COIN_RUN_FIELD_H - coin_run_player_box;

        if (coin_run_next_x > max_x) coin_run_next_x = max_x;
        if (coin_run_next_x < 0) coin_run_next_x = 0;
        if (coin_run_next_y > max_y) coin_run_next_y = max_y;
        if (coin_run_next_y < 28) coin_run_next_y = 28;
        coin_run_player_x = coin_run_next_x;
        coin_run_player_y = coin_run_next_y;
    }
}

/*
 * Held arrows move every game tick. Up/down are tried first, then
 * left/right, so a diagonal can advance both axes in one tick.
 */
static void coin_run_update_player_motion(void) {
    if (coin_run_state != COIN_RUN_STATE_PLAY) {
        return;
    }

    if (coin_run_key_up()) {
        coin_run_next_x = coin_run_player_x;
        coin_run_next_y = coin_run_player_y - coin_run_player_step;
        if (coin_run_next_y < 28) coin_run_next_y = 28;
        coin_run_try_player_position();
    }
    if (coin_run_key_down()) {
        coin_run_next_x = coin_run_player_x;
        coin_run_next_y = coin_run_player_y + coin_run_player_step;
        if (coin_run_next_y > COIN_RUN_FIELD_H - coin_run_player_box) {
            coin_run_next_y = COIN_RUN_FIELD_H - coin_run_player_box;
        }
        coin_run_try_player_position();
    }
    if (coin_run_key_left()) {
        coin_run_facing = 1;
        coin_run_next_x = coin_run_player_x >= 2 ? coin_run_player_x - coin_run_player_step : 0;
        coin_run_next_y = coin_run_player_y;
        coin_run_try_player_position();
    } else if (coin_run_key_right()) {
        coin_run_facing = 0;
        coin_run_next_x = coin_run_player_x + coin_run_player_step;
        coin_run_next_y = coin_run_player_y;
        coin_run_try_player_position();
    }
}

static void coin_run_update_squeeze(void) {
    if (coin_run_squeeze_active) {
        if (--coin_run_squeeze_ticks > 0) {
            return;
        }
        coin_run_squeeze_active = false;
        coin_run_player_box = 32;
        coin_run_player_step = 4;
        if (coin_run_player_x > COIN_RUN_FIELD_W - 32) {
            coin_run_player_x = COIN_RUN_FIELD_W - 32;
        }
        coin_run_squeeze_cooldown = COIN_RUN_SQUEEZE_COOLDOWN_TICKS;
        return;
    }
    if (coin_run_squeeze_cooldown > 0) {
        --coin_run_squeeze_cooldown;
    }
}

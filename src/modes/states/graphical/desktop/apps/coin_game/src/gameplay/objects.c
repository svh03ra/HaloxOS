// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: objects.c, Coin Run! coins, skulls, timer, 1UP and lives.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

/* Carry-style test for a skull's 24x24 body box (48x48 sprite). */
static bool coin_run_skull_hits_player(int index) {
    int sx = coin_run_skull_x[index] + 12;
    int sy = coin_run_skull_y[index] + 16;

    return coin_run_player_x + coin_run_player_box > sx && sx + 24 > coin_run_player_x &&
           coin_run_player_y + coin_run_player_box > sy && sy + 24 > coin_run_player_y;
}

/* Returns true once no active coin is left in the round. */
static bool coin_run_collect_coins(void) {
    coin_run_coins_left = 0;
    for (int i = 0; i < coin_run_coin_count; ++i) {
        if (!coin_run_coin_active[i]) {
            continue;
        }
        if (coin_run_player_x + 31 > coin_run_coin_x[i] &&
            coin_run_coin_x[i] + 16 > coin_run_player_x &&
            coin_run_player_y + 31 > coin_run_coin_y[i] &&
            coin_run_coin_y[i] + 16 > coin_run_player_y) {
            coin_run_coin_active[i] = false;
            coin_run_play_coin_sound();
        }
        if (coin_run_coin_active[i]) {
            ++coin_run_coins_left;
        }
    }
    return coin_run_coins_left == 0;
}

static void coin_run_animate_coins(void) {
    if (++coin_run_coin_tick < 3) {
        return;
    }
    coin_run_coin_tick = 0;
    if (++coin_run_coin_frame >= 12) {
        coin_run_coin_frame = 0;
    }
}

static void coin_run_update_timer(void) {
    /* Debug invincibility freezes the countdown until toggled back off. */
    if (coin_run_debug_invincible()) {
        return;
    }
    if (++coin_run_timer_tick < 18) {
        return;
    }
    coin_run_timer_tick = 0;
    if (--coin_run_seconds_left <= 0) {
        coin_run_seconds_left = 0;
        coin_run_lose_life();
    }
}

/* Skulls step 2px on one axis every second tick and pass through bricks. */
static bool coin_run_skull_try_x(int index) {
    if (coin_run_skull_x[index] == coin_run_player_x) {
        return true;
    }
    coin_run_skull_x[index] += coin_run_skull_x[index] < coin_run_player_x ? 2 : -2;
    return false;
}

static bool coin_run_skull_try_y(int index) {
    if (coin_run_skull_y[index] == coin_run_player_y) {
        return true;
    }
    coin_run_skull_y[index] += coin_run_skull_y[index] < coin_run_player_y ? 2 : -2;
    return false;
}

static void coin_run_update_skulls(void) {
    if (coin_run_hurt_cooldown > 0) {
        --coin_run_hurt_cooldown;
        return;
    }
    if (++coin_run_skull_move_tick < 2) {
        return;
    }
    coin_run_skull_move_tick = 0;

    for (int i = 0; i < coin_run_skull_count; ++i) {
        int dx = coin_run_abs(coin_run_player_x - coin_run_skull_x[i]);
        int dy = coin_run_abs(coin_run_player_y - coin_run_skull_y[i]);

        if (dx >= dy) {
            if (coin_run_skull_try_x(i)) {
                coin_run_skull_try_y(i);
            }
        } else {
            if (coin_run_skull_try_y(i)) {
                coin_run_skull_try_x(i);
            }
        }
        /* Invincibility ignores skull collisions only. */
        if (coin_run_skull_hits_player(i) && !coin_run_debug_invincible()) {
            coin_run_lose_life();
            return;
        }
    }
}

static void coin_run_update_oneup(void) {
    if (coin_run_level < 3) {
        coin_run_oneup_active = false;
        coin_run_oneup_respawn_ticks = 0;
        return;
    }
    if (coin_run_oneup_active) {
        if (coin_run_player_x + 31 > coin_run_oneup_x &&
            coin_run_oneup_x + 16 > coin_run_player_x &&
            coin_run_player_y + 31 > coin_run_oneup_y &&
            coin_run_oneup_y + 16 > coin_run_player_y) {
            if (coin_run_lives < 9) {
                ++coin_run_lives;
            }
            coin_run_oneup_active = false;
            coin_run_reset_oneup_timer();
        }
        return;
    }
    if (coin_run_oneup_respawn_ticks > 0) {
        if (--coin_run_oneup_respawn_ticks > 0) {
            return;
        }
        coin_run_spawn_oneup();
        return;
    }
    coin_run_spawn_oneup();
}

static void coin_run_lose_life(void) {
    coin_run_play_hurt_sound();
    --coin_run_lives;
    if (coin_run_lives <= 0) {
        coin_run_lives = 0;
        coin_run_state = COIN_RUN_STATE_LOSE;
        return;
    }
    coin_run_player_x = 16;
    coin_run_player_y = 96;
    coin_run_hurt_cooldown = 24;
    coin_run_reset_level_timer();
    coin_run_state = COIN_RUN_STATE_BEGIN;
    coin_run_begin_ticks = COIN_RUN_BEGIN_TICKS;
}

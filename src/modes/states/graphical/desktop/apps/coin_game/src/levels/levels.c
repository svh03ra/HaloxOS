// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: levels.c, Coin Run! level tables and object placement.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

/*
 * Bricks stay out of the player's spawn corridor and leave a two-brick edge
 * channel. Cells are aligned to the 16px grid and may not repeat.
 */
static void coin_run_place_bricks(void) {
    for (int i = 0; i < coin_run_brick_count; ++i) {
        for (int tries = 48; tries > 0; --tries) {
            int x = coin_run_random_x();
            int y = coin_run_random_y();
            bool clash = false;

            if (x < 96) x += 96;
            if (x > 272) x = 272;
            x &= ~0xF;
            if (y < 32) y = 32;
            if (y > 152) y = 152;
            y &= ~0xF;

            for (int j = 0; j < i; ++j) {
                if (coin_run_brick_x[j] == x && coin_run_brick_y[j] == y) {
                    clash = true;
                    break;
                }
            }
            if (!clash || tries == 1) {
                coin_run_brick_x[i] = x;
                coin_run_brick_y[i] = y;
                break;
            }
        }
    }
}

/* Coins keep clear of the screen edge and of every accepted brick cell. */
static void coin_run_place_coins(void) {
    for (int i = 0; i < coin_run_coin_count; ++i) {
        for (int tries = 48; tries > 0; --tries) {
            bool clash = false;
            int x = coin_run_random_x();
            int y = coin_run_random_y();

            if (x < 32) x = 32;
            if (x > 256) x = 256;
            x &= ~0xF;
            if (y < 32) y = 32;
            if (y > 152) y = 152;
            y &= ~0xF;

            for (int j = 0; j < coin_run_brick_count; ++j) {
                if (x == coin_run_brick_x[j] && y == coin_run_brick_y[j]) {
                    clash = true;
                    break;
                }
            }
            if (!clash || tries == 1) {
                coin_run_coin_x[i] = x;
                coin_run_coin_y[i] = y;
                coin_run_coin_active[i] = true;
                break;
            }
        }
    }
}

/* The player starts on the left, so skulls start away from the spawn. */
static void coin_run_place_skulls(void) {
    for (int i = 0; i < coin_run_skull_count; ++i) {
        int y = coin_run_random_y();

        if (i == 0) {
            coin_run_skull_x[i] = 256;
        } else {
            coin_run_skull_x[i] = 64;
            if (coin_run_abs(y - coin_run_skull_y[0]) < 56) {
                y += 72;
                if (y > 168) y -= 136;
            }
        }
        coin_run_skull_y[i] = y;
    }
}

static void coin_run_reset_level_timer(void) {
    coin_run_timer_tick = 0;
    if (coin_run_level == 1) {
        coin_run_seconds_left = 7;
    } else if (coin_run_level == 2) {
        coin_run_seconds_left = 10;
    } else {
        coin_run_seconds_left = 15;
    }
}

static void coin_run_reset_oneup_timer(void) {
    coin_run_oneup_respawn_ticks =
        (int)(coin_run_random_word() % COIN_RUN_ONEUP_RESPAWN_RANGE) + COIN_RUN_ONEUP_MIN_TICKS;
}

static void coin_run_reset_oneup_session(void) {
    coin_run_oneup_active = false;
    coin_run_oneup_respawn_ticks = 0;
}

/*
 * The 1UP starts its first 2-6 minute countdown only when level 3 is
 * reached, then survives every attempt and levels 4-5.
 */
static void coin_run_reset_oneup(void) {
    if (coin_run_level < 3) {
        coin_run_oneup_active = false;
        coin_run_oneup_respawn_ticks = 0;
        return;
    }
    if (coin_run_oneup_active || coin_run_oneup_respawn_ticks != 0) {
        return;
    }
    coin_run_reset_oneup_timer();
}

/* Place the 1UP clear of bricks, active coins and skull hitboxes. */
static void coin_run_spawn_oneup(void) {
    int x = 32;
    int y = 32;

    for (int tries = 48; tries > 0; --tries) {
        bool blocked = false;

        x = coin_run_random_x();
        if (x < 32) x = 32;
        if (x > 256) x = 256;
        x &= ~0xF;
        y = coin_run_random_y();
        if (y < 32) y = 32;
        if (y > 152) y = 152;
        y &= ~0xF;

        for (int i = 0; i < coin_run_brick_count && !blocked; ++i) {
            if (x + 16 > coin_run_brick_x[i] && coin_run_brick_x[i] + 16 > x &&
                y + 16 > coin_run_brick_y[i] && coin_run_brick_y[i] + 16 > y) {
                blocked = true;
            }
        }
        for (int i = 0; i < coin_run_coin_count && !blocked; ++i) {
            if (!coin_run_coin_active[i]) continue;
            if (x + 16 > coin_run_coin_x[i] && coin_run_coin_x[i] + 16 > x &&
                y + 16 > coin_run_coin_y[i] && coin_run_coin_y[i] + 16 > y) {
                blocked = true;
            }
        }
        for (int i = 0; i < coin_run_skull_count && !blocked; ++i) {
            int sx = coin_run_skull_x[i] + 12;
            int sy = coin_run_skull_y[i] + 16;
            if (x + 16 > sx && sx + 24 > x && y + 16 > sy && sy + 24 > y) {
                blocked = true;
            }
        }
        if (!blocked) break;
    }

    coin_run_oneup_x = x;
    coin_run_oneup_y = y;
    coin_run_oneup_active = true;
}

static void coin_run_setup_round(void) {
    coin_run_player_x = 16;
    coin_run_player_y = 96;
    coin_run_facing = 0;
    coin_run_coin_frame = 0;
    coin_run_coin_tick = 0;
    coin_run_timer_tick = 0;
    coin_run_skull_move_tick = 0;
    coin_run_hurt_cooldown = 0;
    coin_run_squeeze_active = false;
    coin_run_squeeze_ticks = 0;
    coin_run_squeeze_cooldown = 0;
    coin_run_player_box = 32;
    coin_run_player_step = 4;
    coin_run_begin_ticks = COIN_RUN_BEGIN_TICKS;
    coin_run_reset_level_timer();
    coin_run_reset_oneup();
    coin_run_debug_on_round_start();
    coin_run_coin_count =
        (int)(coin_run_random_word() % (uint16_t)coin_run_coin_span) + coin_run_coin_min;
    coin_run_place_bricks();
    coin_run_place_coins();
    coin_run_place_skulls();

    /* A visible 1UP is re-placed so new geometry cannot bury it. */
    if (coin_run_oneup_active) {
        coin_run_spawn_oneup();
    }
}

/* Per-level configuration from the original setup_level table. */
static void coin_run_setup_level(void) {
    coin_run_attempt_current = 1;
    if (coin_run_level == 1) {
        coin_run_attempt_total = 3;
        coin_run_coin_min = 5;
        coin_run_coin_span = 4;
        coin_run_brick_count = 0;
        coin_run_skull_count = 0;
        coin_run_seconds_left = 7;
    } else if (coin_run_level == 2) {
        coin_run_attempt_total = 5;
        coin_run_coin_min = 5;
        coin_run_coin_span = 4;
        coin_run_brick_count = 6;
        coin_run_skull_count = 0;
        coin_run_seconds_left = 10;
    } else if (coin_run_level == 3) {
        coin_run_attempt_total = 2;
        coin_run_coin_min = 8;
        coin_run_coin_span = 7;
        coin_run_brick_count = 6;
        coin_run_skull_count = 1;
        coin_run_seconds_left = 15;
    } else if (coin_run_level == 4) {
        coin_run_attempt_total = 3;
        coin_run_coin_min = 10;
        coin_run_coin_span = 7;
        coin_run_brick_count = 8;
        coin_run_skull_count = 2;
        coin_run_seconds_left = 15;
    } else {
        coin_run_attempt_total = 1;
        coin_run_coin_min = 10;
        coin_run_coin_span = 7;
        coin_run_brick_count = 12;
        coin_run_skull_count = 2;
        coin_run_seconds_left = 15;
    }
    coin_run_setup_round();
}

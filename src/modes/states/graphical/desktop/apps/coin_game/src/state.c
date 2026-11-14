// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: state.c, Coin Run! shared state, RNG and reset.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "coin_run.h"

static int coin_run_level;
static int coin_run_lives;
static int coin_run_state;
static int coin_run_attempt_current;
static int coin_run_attempt_total;
static int coin_run_coin_min;
static int coin_run_coin_span;
static int coin_run_seconds_left;
static int coin_run_timer_tick;
static int coin_run_begin_ticks;
static int coin_run_coin_count;
static int coin_run_brick_count;
static int coin_run_skull_count;
static int coin_run_coins_left;
static int coin_run_coin_frame;
static int coin_run_coin_tick;
static int coin_run_hurt_cooldown;
static int coin_run_skull_move_tick;
static int coin_run_player_x;
static int coin_run_player_y;
static int coin_run_next_x;
static int coin_run_next_y;
static int coin_run_player_box = 32;
static int coin_run_player_step = 4;
static int coin_run_facing;
static bool coin_run_squeeze_active;
static int coin_run_squeeze_ticks;
static int coin_run_squeeze_cooldown;
static int coin_run_coin_x[COIN_RUN_MAX_COINS];
static int coin_run_coin_y[COIN_RUN_MAX_COINS];
static bool coin_run_coin_active[COIN_RUN_MAX_COINS];
static int coin_run_brick_x[COIN_RUN_MAX_BRICKS];
static int coin_run_brick_y[COIN_RUN_MAX_BRICKS];
static int coin_run_skull_x[COIN_RUN_MAX_SKULLS];
static int coin_run_skull_y[COIN_RUN_MAX_SKULLS];
static int coin_run_oneup_x;
static int coin_run_oneup_y;
static bool coin_run_oneup_active;
static int coin_run_oneup_respawn_ticks;
static uint16_t coin_run_rng_state = 1;
/* Raw seed is kept separately from the live LCG so the debug overlay can
 * show the value the current round was generated from. */
static uint16_t coin_run_seed = 1;

static int coin_run_abs(int value) {
    return value < 0 ? -value : value;
}

/* Same 16-bit LCG the assembly game uses (mul 25173, add 13849). */
static uint16_t coin_run_random_word(void) {
    coin_run_rng_state = (uint16_t)((uint32_t)coin_run_rng_state * 25173u + 13849u);
    return coin_run_rng_state;
}

static int coin_run_random_x(void) {
    return (int)(coin_run_random_word() % 288u) + 16;
}

static int coin_run_random_y(void) {
    return (int)(coin_run_random_word() % 136u) + 40;
}

/* Fresh game: called when the app window is opened. */
static void coin_run_reset(void) {
    coin_run_rng_state = (uint16_t)((uint32_t)timer_ticks * 25173u + 13849u +
                                    (uint32_t)rand_range(65536));
    coin_run_seed = coin_run_rng_state;
    coin_run_level = 1;
    coin_run_lives = 3;
    coin_run_reset_oneup_session();
    coin_run_debug_reset();
    coin_run_speaker_silence();
    coin_run_setup_level();
    coin_run_state = COIN_RUN_STATE_BEGIN;
}

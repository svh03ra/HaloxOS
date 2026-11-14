// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: input.c, Coin Run! input, tick pump and state machine.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

/* The original runs on the 18.2 Hz BIOS tick. The desktop timer runs at
 * 60 Hz, so accumulate 18.2065/60 = 0.30344 game ticks per OS tick in
 * fixed point and step the game exactly as often as the PC would. */
#define COIN_RUN_TICK_SCALE 100000u
#define COIN_RUN_TICK_STEP 30344u

static uint32_t coin_run_tick_accum;
static uint32_t coin_run_last_os_tick;
static bool coin_run_prev_space;
static bool coin_run_prev_enter;

/* The OS queue only carries presses; movement needs the physical
 * key-state table, exactly like the DOOM port. */
static bool coin_run_key_up(void) {
    return keyboard_key_held(0x48, true);
}

static bool coin_run_key_down(void) {
    return keyboard_key_held(0x50, true);
}

static bool coin_run_key_left(void) {
    return keyboard_key_held(0x4B, true);
}

static bool coin_run_key_right(void) {
    return keyboard_key_held(0x4D, true);
}

static bool coin_run_key_space(void) {
    return keyboard_key_held(0x39, false);
}

static bool coin_run_key_enter(void) {
    return keyboard_key_held(0x1C, false);
}

/* One BIOS-tick worth of game logic. */
static void coin_run_step(void) {
    if (coin_run_debug_seed_active()) {
        return;
    }

    coin_run_debug_tick();
    coin_run_update_player_motion();

    if (coin_run_state == COIN_RUN_STATE_BEGIN) {
        if (coin_run_begin_ticks > 0) {
            --coin_run_begin_ticks;
        }
        if (coin_run_begin_ticks == 0) {
            coin_run_state = COIN_RUN_STATE_PLAY;
        }
        return;
    }
    if (coin_run_state == COIN_RUN_STATE_PLAY) {
        coin_run_update_squeeze();
        coin_run_animate_coins();
        coin_run_update_timer();
        if (coin_run_state != COIN_RUN_STATE_PLAY) return;
        coin_run_update_skulls();
        if (coin_run_state != COIN_RUN_STATE_PLAY) return;
        coin_run_update_oneup();
        if (coin_run_state != COIN_RUN_STATE_PLAY) return;

        if (coin_run_collect_coins()) {
            if (coin_run_attempt_current < coin_run_attempt_total) {
                ++coin_run_attempt_current;
                coin_run_setup_round();
                coin_run_state = COIN_RUN_STATE_BEGIN;
            } else {
                ++coin_run_level;
                if (coin_run_level >= 6) {
                    coin_run_state = COIN_RUN_STATE_WIN;
                } else {
                    coin_run_setup_level();
                    coin_run_state = COIN_RUN_STATE_BEGIN;
                }
            }
        }
    }
}

static void coin_run_handle_space_edge(void) {
    if (coin_run_state != COIN_RUN_STATE_PLAY) {
        return;
    }
    if (coin_run_squeeze_active || coin_run_squeeze_cooldown != 0) {
        return;
    }
    coin_run_squeeze_active = true;
    coin_run_player_box = 16;
    coin_run_player_step = 6;
    coin_run_squeeze_ticks = COIN_RUN_SQUEEZE_ACTIVE_TICKS;
    coin_run_play_powerup_sound();
}

static void coin_run_handle_enter_edge(void) {
    if (coin_run_state != COIN_RUN_STATE_WIN && coin_run_state != COIN_RUN_STATE_LOSE) {
        return;
    }
    coin_run_level = 1;
    coin_run_lives = 3;
    coin_run_reset_oneup_session();
    coin_run_setup_level();
    coin_run_state = COIN_RUN_STATE_BEGIN;
}

/*
 * Called once per desktop frame. The game pauses while another window owns
 * the keyboard, mirroring the other desktop games; elapsed OS ticks are
 * discarded on the way back in so a pause cannot fast-forward the timer.
 */
static void update_coin_run(void) {
    if (!windows[APP_COIN_RUN].open) {
        coin_run_speaker_silence();
        coin_run_last_os_tick = timer_ticks;
        coin_run_tick_accum = 0;
        return;
    }
    if (active_window != APP_COIN_RUN || debug_overlay_open || cpu_halted_overlay ||
        coin_run_debug_seed_active()) {
        coin_run_speaker_silence();
        coin_run_last_os_tick = timer_ticks;
        coin_run_tick_accum = 0;
        coin_run_prev_space = coin_run_key_space();
        coin_run_prev_enter = coin_run_key_enter();
        return;
    }

    {
        uint32_t now = timer_ticks;
        uint32_t delta = now - coin_run_last_os_tick;
        bool space;
        bool enter;

        coin_run_last_os_tick = now;
        /* Tone changes are scheduled on the 60 Hz system tick, not on the
         * 18.2 Hz game tick, so notes have a steady wall-clock length even
         * when one frame runs several game steps in a row. */
        coin_run_update_sound();
        if (delta > 18u) {
            delta = 18u;
        }
        coin_run_tick_accum += delta * COIN_RUN_TICK_STEP;
        while (coin_run_tick_accum >= COIN_RUN_TICK_SCALE) {
            coin_run_tick_accum -= COIN_RUN_TICK_SCALE;
            coin_run_step();
        }

        space = coin_run_key_space();
        enter = coin_run_key_enter();
        if (space && !coin_run_prev_space) {
            coin_run_handle_space_edge();
        }
        if (enter && !coin_run_prev_enter) {
            coin_run_handle_enter_edge();
        }
        coin_run_prev_space = space;
        coin_run_prev_enter = enter;
    }
}

/* Keyboard-routing entry point for the debug keys/seed dialog. In a release
 * build the debug module is a no-op, so the game ignores these events. */
static void coin_run_handle_key(KeyEvent event) {
    coin_run_debug_handle_key(event);
}

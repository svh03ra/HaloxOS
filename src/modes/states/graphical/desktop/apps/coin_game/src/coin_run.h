// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: coin_run.h, shared Coin Run! constants and module prototypes.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#ifndef COIN_RUN_H
#define COIN_RUN_H

/*
 * Debug mode follows the OS build: a HaloxOS debug build enables the same
 * extras the original assembly kept behind its `debug` byte (F1-F5, 1-5 level
 * skip, hitbox/seed overlay and the custom-seed dialog). A release build
 * compiles the implementation out and leaves no-ops in its place.
 */
#if defined(HALOXOS_CONFIG_DEBUG) && (HALOXOS_CONFIG_DEBUG != 0)
#define COIN_RUN_DEBUG 1
#else
#define COIN_RUN_DEBUG 0
#endif

#define COIN_RUN_FIELD_W 320
#define COIN_RUN_FIELD_H 200
#define COIN_RUN_HUD_H 24
#define COIN_RUN_MAX_COINS 16
#define COIN_RUN_MAX_BRICKS 12
#define COIN_RUN_MAX_SKULLS 2
#define COIN_RUN_SQUEEZE_ACTIVE_TICKS 91
#define COIN_RUN_SQUEEZE_COOLDOWN_TICKS 292
#define COIN_RUN_BEGIN_TICKS 55
#define COIN_RUN_ONEUP_MIN_TICKS 2184      /* 2 minutes at 18.2 ticks/s */
#define COIN_RUN_ONEUP_RESPAWN_RANGE 4369  /* random +0..4368 => 2-6 minutes */

#define COIN_RUN_STATE_BEGIN 0
#define COIN_RUN_STATE_PLAY 1
#define COIN_RUN_STATE_WIN 2
#define COIN_RUN_STATE_LOSE 3

/* state.c */
static void coin_run_reset(void);
static int coin_run_abs(int value);
static uint16_t coin_run_random_word(void);
static int coin_run_random_x(void);
static int coin_run_random_y(void);

/* levels/levels.c */
static void coin_run_setup_level(void);
static void coin_run_setup_round(void);
static void coin_run_reset_oneup_session(void);
static void coin_run_reset_oneup_timer(void);
static void coin_run_spawn_oneup(void);

/* gameplay/player.c */
static bool coin_run_hit_brick(int next_x, int next_y);
static void coin_run_try_player_position(void);
static void coin_run_update_player_motion(void);
static void coin_run_update_squeeze(void);

/* gameplay/objects.c */
static bool coin_run_collect_coins(void);
static bool coin_run_skull_hits_player(int index);
static void coin_run_animate_coins(void);
static void coin_run_update_timer(void);
static void coin_run_update_skulls(void);
static void coin_run_update_oneup(void);
static void coin_run_lose_life(void);

/* audio/sound.c */
static void coin_run_play_coin_sound(void);
static void coin_run_play_hurt_sound(void);
static void coin_run_play_powerup_sound(void);
static void coin_run_update_sound(void);
static void coin_run_speaker_silence(void);

/* debug/debug.c */
static void coin_run_debug_handle_key(KeyEvent event);
static bool coin_run_debug_seed_active(void);
static void coin_run_debug_tick(void);
static bool coin_run_debug_invincible(void);
static bool coin_run_debug_player_hidden(void);
static bool coin_run_debug_oneup_visible(void);
static void coin_run_debug_draw_overlay(int ox, int oy);
static void coin_run_debug_draw_seed_ui(int ox, int oy);
static void coin_run_debug_reset(void);
static void coin_run_debug_on_round_start(void);

/* input/input.c */
static bool coin_run_key_up(void);
static bool coin_run_key_down(void);
static bool coin_run_key_left(void);
static bool coin_run_key_right(void);
static bool coin_run_key_space(void);
static bool coin_run_key_enter(void);
static void update_coin_run(void);
static void coin_run_handle_key(KeyEvent event);

/* ui/palette.c */
static void coin_run_prepare_colors(void);
static uint8_t coin_run_color(int index);

/* ui/render.c */
static void render_coin_run(const Window *window);

#endif

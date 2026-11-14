// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, Coin Run! playfield rendering.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

/*
 * The playfield is the original 320x200 mode-13h screen, drawn 1:1 into an
 * app window sized to match it exactly, so no black letterbox margin is
 * painted around the game. Sprite order and the HUD/screen positions follow
 * the assembly game.
 */

static const uint8_t *const coin_run_coin_frames[12] = {
    _binary_build_coin_spr_coin1_bin_start,
    _binary_build_coin_spr_coin2_bin_start,
    _binary_build_coin_spr_coin3_bin_start,
    _binary_build_coin_spr_coin4_bin_start,
    _binary_build_coin_spr_coin3_flip_bin_start,
    _binary_build_coin_spr_coin2_flip_bin_start,
    _binary_build_coin_spr_coin1_flip_bin_start,
    _binary_build_coin_spr_coin2_flip_bin_start,
    _binary_build_coin_spr_coin3_flip_bin_start,
    _binary_build_coin_spr_coin4_bin_start,
    _binary_build_coin_spr_coin3_bin_start,
    _binary_build_coin_spr_coin2_bin_start
};

static void coin_run_draw_squeeze_bar(int ox, int oy) {
    int width;

    if (coin_run_squeeze_active) {
        fill_rect(ox + 8, oy + 188, 64, 6, coin_run_color(8));
        width = coin_run_squeeze_ticks * 64 / COIN_RUN_SQUEEZE_ACTIVE_TICKS;
        if (width > 0) {
            fill_rect(ox + 8, oy + 188, width, 6, coin_run_color(10));
        }
        return;
    }
    if (coin_run_squeeze_cooldown > 0) {
        fill_rect(ox + 8, oy + 188, 64, 6, coin_run_color(11));
        width = coin_run_squeeze_cooldown * 64 / COIN_RUN_SQUEEZE_COOLDOWN_TICKS;
        if (width > 0) {
            fill_rect(ox + 8 + 64 - width, oy + 188, width, 6, coin_run_color(8));
        }
    }
}

static void coin_run_draw_world(int ox, int oy) {
    for (int i = 0; i < coin_run_brick_count; ++i) {
        draw_image_at(_binary_build_coin_spr_brick_bin_start,
                      ox + coin_run_brick_x[i], oy + coin_run_brick_y[i], true);
    }
    for (int i = 0; i < coin_run_coin_count; ++i) {
        if (!coin_run_coin_active[i]) {
            continue;
        }
        draw_image_at(coin_run_coin_frames[coin_run_coin_frame],
                      ox + coin_run_coin_x[i], oy + coin_run_coin_y[i], true);
    }
    for (int i = 0; i < coin_run_skull_count; ++i) {
        draw_image_at(_binary_build_coin_spr_skull_bin_start,
                      ox + coin_run_skull_x[i], oy + coin_run_skull_y[i], true);
    }
    /* The original only paints the 1UP on level 3 (levels 4-5 keep the
     * invisible pickup logic). The debug spawn makes it visible anywhere. */
    if (coin_run_level == 3 || coin_run_debug_oneup_visible()) {
        if (coin_run_oneup_active) {
            draw_image_at(_binary_build_coin_spr_1up_bin_start,
                          ox + coin_run_oneup_x, oy + coin_run_oneup_y, true);
        }
    }

    if (!coin_run_debug_player_hidden()) {
        if (coin_run_squeeze_active) {
            draw_image_at(_binary_build_coin_spr_hud_lives_bin_start,
                          ox + coin_run_player_x, oy + coin_run_player_y, true);
        } else {
            draw_image_at(coin_run_facing ? _binary_build_coin_spr_player_flip_bin_start
                                          : _binary_build_coin_spr_player_bin_start,
                          ox + coin_run_player_x, oy + coin_run_player_y, true);
        }
    }

    /* Debug hitboxes/info sit under the squeeze bar, as in the original. */
    coin_run_debug_draw_overlay(ox, oy);
}

static void render_coin_run(const Window *window) {
    int ox = window->x + 1;
    int oy = window->y + 19;

    coin_run_prepare_colors();
    /* The window client is exactly the 320x200 playfield, so one clear
     * paints the whole game surface with no border showing around it. */
    fill_rect(ox, oy, COIN_RUN_FIELD_W, COIN_RUN_FIELD_H, coin_run_color(0));

    if (coin_run_state == COIN_RUN_STATE_WIN) {
        coin_run_draw_win(ox, oy);
        coin_run_debug_draw_seed_ui(ox, oy);
        return;
    }
    if (coin_run_state == COIN_RUN_STATE_LOSE) {
        coin_run_draw_lose(ox, oy);
        coin_run_debug_draw_seed_ui(ox, oy);
        return;
    }

    coin_run_draw_hud(ox, oy);
    if (coin_run_state == COIN_RUN_STATE_BEGIN) {
        coin_run_draw_begin(ox, oy);
    } else {
        coin_run_draw_world(ox, oy);
        coin_run_draw_squeeze_bar(ox, oy);
    }
    coin_run_debug_draw_seed_ui(ox, oy);
}

// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: hud.c, Coin Run! HUD rendering.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

/* Original HUD: lives icon/value, LEVEL n, attempt (n-m), clock + timer. */
static void coin_run_draw_hud(int ox, int oy) {
    char text[10];

    fill_rect(ox, oy, COIN_RUN_FIELD_W, COIN_RUN_HUD_H, coin_run_color(4));

    draw_image_at(_binary_build_coin_spr_hud_lives_bin_start, ox + 18, oy + 4, true);
    draw_text(ox + 38, oy + 8, "x", coin_run_color(15), coin_run_color(4), true);
    text[0] = (char)('0' + coin_run_lives);
    text[1] = '\0';
    draw_text(ox + 56, oy + 8, text, coin_run_color(15), coin_run_color(4), true);

    copy_string(text, "LEVEL ", sizeof(text));
    text[6] = (char)('0' + coin_run_level);
    text[7] = '\0';
    draw_text(ox + 108, oy + 8, text, coin_run_color(14), coin_run_color(4), true);

    /* Hidden on level 5 (one attempt) and on the first BEGIN card. */
    if (!(coin_run_level == 5 ||
          (coin_run_state == COIN_RUN_STATE_BEGIN && coin_run_attempt_current == 1))) {
        text[0] = '(';
        text[1] = (char)('0' + coin_run_attempt_current);
        text[2] = '-';
        text[3] = (char)('0' + coin_run_attempt_total);
        text[4] = ')';
        text[5] = '\0';
        draw_text(ox + 166, oy + 8, text, coin_run_color(14), coin_run_color(4), true);
    }

    /* The limit is a surprise once the BEGIN card is gone. */
    if (coin_run_state != COIN_RUN_STATE_BEGIN) {
        char time[6] = {'0', '0', ':', '0', '0', '\0'};

        draw_image_at(_binary_build_coin_spr_hud_clock_bin_start, ox + 238, oy + 4, true);
        time[3] = (char)('0' + (coin_run_seconds_left / 10) % 10);
        time[4] = (char)('0' + coin_run_seconds_left % 10);
        draw_text(ox + 260, oy + 8, time, coin_run_color(15), coin_run_color(4), true);
    }
}

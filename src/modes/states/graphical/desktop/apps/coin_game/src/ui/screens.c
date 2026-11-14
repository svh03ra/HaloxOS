// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: screens.c, Coin Run! begin/win/lose screens.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

static const int coin_run_sine[16] = {
    0, 1, 3, 4, 5, 4, 3, 1, 0, -1, -3, -4, -5, -4, -3, -1
};
static const uint8_t coin_run_rainbow[8] = {12, 14, 10, 11, 9, 13, 15, 14};

/*
 * The win banner used to advance its phase from the renderer itself, so a
 * modern 60 Hz desktop cycled the rainbow many times faster than the
 * original 18.2 Hz screen. Both phases now come from the system timer:
 * the animation is identical on a fast CPU and a slow one, and a dropped
 * frame can never make it jump ahead.
 */
#define COIN_RUN_WAVE_STEPS_PER_SECOND    12
#define COIN_RUN_RAINBOW_STEPS_PER_SECOND  3
#define COIN_RUN_WAVE_STEP_TICKS \
    (TIMER_HZ / COIN_RUN_WAVE_STEPS_PER_SECOND)
#define COIN_RUN_RAINBOW_STEP_TICKS \
    (TIMER_HZ / COIN_RUN_RAINBOW_STEPS_PER_SECOND)

static void coin_run_draw_begin(int ox, int oy) {
    char text[20];

    copy_string(text, "LEVEL ", sizeof(text));
    text[6] = (char)('0' + coin_run_level);
    copy_string(&text[7], " - BEGIN!", sizeof(text) - 7);
    draw_text_scaled(ox + 32, oy + 82, text, coin_run_color(15), coin_run_color(0), true, 2);
}

static void coin_run_draw_win(int ox, int oy) {
    static const char *won = "YOU WON!";
    uint32_t wave_step = timer_ticks / COIN_RUN_WAVE_STEP_TICKS;
    uint32_t rainbow_step = timer_ticks / COIN_RUN_RAINBOW_STEP_TICKS;

    for (int i = 0; won[i] != '\0'; ++i) {
        uint32_t phase = wave_step + (uint32_t)i;
        int wave_y = oy + 80 + coin_run_sine[phase & 15];
        uint8_t wave_color =
            coin_run_color(coin_run_rainbow[(rainbow_step + (uint32_t)i) & 7]);

        draw_char_scaled(ox + 104 + i * 16, wave_y, won[i], wave_color,
                         coin_run_color(0), true, 2);
    }
    draw_text(ox + 76, oy + 121, "Press ENTER to restart",
              coin_run_color(15), coin_run_color(0), true);
    draw_text(ox + 40, oy + 145, "Congrats, Thanks for playing!",
              coin_run_color(10), coin_run_color(0), true);
}

static void coin_run_draw_lose(int ox, int oy) {
    draw_text_scaled(ox + 88, oy + 72, "GAME OVER!", coin_run_color(12),
                     coin_run_color(0), true, 2);
    draw_text(ox + 76, oy + 160, "Press ENTER to restart",
              coin_run_color(15), coin_run_color(0), true);
}

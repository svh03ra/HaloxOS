// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: debug.c, Coin Run! debug keys, overlay and seed dialog.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

/*
 * The original assembly kept these behind its `debug` byte: F1 lose, F2 win,
 * F3 spawn 1UP, 1-5 level skip, F4 invincibility, F5 custom seed, plus the
 * hitbox/seed overlay and the seed entry dialog. COIN_RUN_DEBUG follows the
 * HaloxOS build configuration so a release kernel contains none of them.
 */

#if COIN_RUN_DEBUG

static bool coin_run_debug_invincible_on;
static int coin_run_debug_blink;
static bool coin_run_debug_oneup_mode;
static bool coin_run_debug_seed_ui_active;
static int coin_run_debug_seed_cursor;
static char coin_run_debug_seed_digits[4];

static char coin_run_debug_hex_char(int value) {
    value &= 0x0F;
    return value < 10 ? (char)('0' + value) : (char)('A' + value - 10);
}

static int coin_run_debug_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static void coin_run_debug_open_seed_ui(void) {
    coin_run_debug_seed_ui_active = true;
    coin_run_debug_seed_cursor = 0;
    for (int i = 0; i < 4; ++i) {
        int shift = 12 - i * 4;
        coin_run_debug_seed_digits[i] =
            coin_run_debug_hex_char((coin_run_seed >> shift) & 0x0F);
    }
}

static void coin_run_debug_commit_seed(void) {
    uint16_t value = 0;

    for (int i = 0; i < 4; ++i) {
        int nibble = coin_run_debug_hex_value(coin_run_debug_seed_digits[i]);
        if (nibble < 0) nibble = 0;
        value = (uint16_t)((value << 4) | (uint16_t)nibble);
    }
    coin_run_seed = value;
    coin_run_rng_state = value;
    coin_run_debug_seed_ui_active = false;
    coin_run_setup_level();
    coin_run_state = COIN_RUN_STATE_BEGIN;
}

static void coin_run_debug_seed_key(KeyEvent event) {
    if (event.code == KEY_ENTER) {
        coin_run_debug_commit_seed();
        return;
    }
    if (event.code == KEY_BACKSPACE) {
        if (coin_run_debug_seed_cursor > 0) {
            --coin_run_debug_seed_cursor;
            coin_run_debug_seed_digits[coin_run_debug_seed_cursor] = '0';
        }
        return;
    }
    if (event.code == KEY_LEFT) {
        if (coin_run_debug_seed_cursor > 0) --coin_run_debug_seed_cursor;
        return;
    }
    if (event.code == KEY_RIGHT) {
        if (coin_run_debug_seed_cursor < 4) ++coin_run_debug_seed_cursor;
        return;
    }

    {
        int nibble = coin_run_debug_hex_value(event.ch);
        if (nibble >= 0 && coin_run_debug_seed_cursor < 4) {
            coin_run_debug_seed_digits[coin_run_debug_seed_cursor] =
                coin_run_debug_hex_char(nibble);
            ++coin_run_debug_seed_cursor;
        }
    }
}

static void coin_run_debug_handle_key(KeyEvent event) {
    /* While the seed dialog is open it owns the keyboard. */
    if (coin_run_debug_seed_ui_active) {
        coin_run_debug_seed_key(event);
        return;
    }

    switch (event.code) {
        case KEY_F1:
            coin_run_state = COIN_RUN_STATE_LOSE;
            return;
        case KEY_F2:
            coin_run_state = COIN_RUN_STATE_WIN;
            return;
        case KEY_F3:
            coin_run_debug_oneup_mode = true;
            coin_run_spawn_oneup();
            return;
        case KEY_F4:
            coin_run_debug_invincible_on = !coin_run_debug_invincible_on;
            coin_run_debug_blink = 0;
            return;
        case KEY_F5:
            coin_run_debug_open_seed_ui();
            return;
        default:
            break;
    }

    if (event.ch >= '1' && event.ch <= '5') {
        coin_run_level = event.ch - '0';
        coin_run_setup_level();
        coin_run_state = COIN_RUN_STATE_BEGIN;
    }
}

static bool coin_run_debug_seed_active(void) {
    return coin_run_debug_seed_ui_active;
}

static void coin_run_debug_tick(void) {
    if (!coin_run_debug_invincible_on) {
        return;
    }
    if (++coin_run_debug_blink >= 4) {
        coin_run_debug_blink = 0;
    }
}

static bool coin_run_debug_invincible(void) {
    return coin_run_debug_invincible_on;
}

static bool coin_run_debug_player_hidden(void) {
    return coin_run_debug_invincible_on && coin_run_debug_blink < 2;
}

static bool coin_run_debug_oneup_visible(void) {
    return coin_run_debug_oneup_mode;
}

static void coin_run_debug_on_round_start(void) {
    coin_run_debug_oneup_mode = false;
}

static void coin_run_debug_reset(void) {
    coin_run_debug_invincible_on = false;
    coin_run_debug_blink = 0;
    coin_run_debug_oneup_mode = false;
    coin_run_debug_seed_ui_active = false;
    coin_run_debug_seed_cursor = 0;
}

static void coin_run_debug_outline_rect(int x, int y, int w, int h, uint8_t colour) {
    if (w <= 0 || h <= 0) {
        return;
    }
    fill_rect(x, y, w, 1, colour);
    fill_rect(x, y + h - 1, w, 1, colour);
    fill_rect(x, y, 1, h, colour);
    fill_rect(x + w - 1, y, 1, h, colour);
}

/* Left-bottom details: raw generation seed and 1UP arrival countdown. */
static void coin_run_debug_draw_overlay(int ox, int oy) {
    char text[24];

    coin_run_debug_outline_rect(ox + coin_run_player_x, oy + coin_run_player_y,
                                coin_run_player_box, coin_run_player_box,
                                coin_run_color(10));
    for (int i = 0; i < coin_run_coin_count; ++i) {
        if (!coin_run_coin_active[i]) continue;
        coin_run_debug_outline_rect(ox + coin_run_coin_x[i], oy + coin_run_coin_y[i],
                                    16, 16, coin_run_color(14));
    }
    for (int i = 0; i < coin_run_skull_count; ++i) {
        coin_run_debug_outline_rect(ox + coin_run_skull_x[i] + 12,
                                    oy + coin_run_skull_y[i] + 16,
                                    24, 24, coin_run_color(12));
    }

    copy_string(text, "SEED: ", sizeof(text));
    for (int i = 0; i < 4; ++i) {
        int shift = 12 - i * 4;
        text[6 + i] = coin_run_debug_hex_char((coin_run_seed >> shift) & 0x0F);
    }
    text[10] = '\0';
    draw_text(ox + 4, oy + 184, text, coin_run_color(13), coin_run_color(0), true);

    /* The 1UP timer only appears on level 3+ (where it actually spawns). */
    if (coin_run_level >= 3) {
        if (coin_run_oneup_active) {
            copy_string(text, "1UP: ACTIVE", sizeof(text));
        } else {
            int seconds = (coin_run_oneup_respawn_ticks + 17) / 18;
            int minutes = seconds / 60;
            seconds %= 60;
            copy_string(text, "1UP: WAIT ", sizeof(text));
            text[10] = (char)('0' + (minutes / 10) % 10);
            text[11] = (char)('0' + minutes % 10);
            text[12] = ':';
            text[13] = (char)('0' + (seconds / 10) % 10);
            text[14] = (char)('0' + seconds % 10);
            text[15] = '\0';
        }
        draw_text(ox + 4, oy + 192, text, coin_run_color(13), coin_run_color(0), true);
    }
}

/* ----- custom seed dialog ------------------------------------------------- */
static void coin_run_debug_draw_seed_ui(int ox, int oy) {
    char line[13];
    int cursor;

    if (!coin_run_debug_seed_ui_active) {
        return;
    }

    fill_rect(ox + 24, oy + 68, 272, 72, coin_run_color(8));
    fill_rect(ox + 28, oy + 72, 264, 64, coin_run_color(1));
    draw_text(ox + 64, oy + 80, "Teleport to custom seed:",
              coin_run_color(15), coin_run_color(1), true);

    copy_string(line, "Seed: [0000]", sizeof(line));
    for (int i = 0; i < 4; ++i) {
        line[7 + i] = coin_run_debug_seed_digits[i];
    }
    draw_text(ox + 112, oy + 100, line, coin_run_color(14), coin_run_color(1), true);
    draw_text(ox + 64, oy + 120, "Press ENTER to teleport.",
              coin_run_color(15), coin_run_color(1), true);

    cursor = coin_run_debug_seed_cursor;
    if (cursor < 0) cursor = 0;
    if (cursor > 4) cursor = 4;
    fill_rect(ox + 168 + cursor * 8, oy + 112, 6, 2, coin_run_color(15));
}

#else /* !COIN_RUN_DEBUG */

static void coin_run_debug_handle_key(KeyEvent event) { (void)event; }
static bool coin_run_debug_seed_active(void) { return false; }
static void coin_run_debug_tick(void) {}
static bool coin_run_debug_invincible(void) { return false; }
static bool coin_run_debug_player_hidden(void) { return false; }
static bool coin_run_debug_oneup_visible(void) { return false; }
static void coin_run_debug_draw_overlay(int ox, int oy) { (void)ox; (void)oy; }
static void coin_run_debug_draw_seed_ui(int ox, int oy) { (void)ox; (void)oy; }
static void coin_run_debug_reset(void) {}
static void coin_run_debug_on_round_start(void) {}

#endif /* COIN_RUN_DEBUG */

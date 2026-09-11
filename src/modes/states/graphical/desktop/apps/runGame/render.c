// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, Title Run! game rendering.

// This repository is licensed under the GNU General Public License.

static void render_run(const Window *window) {
    int ox = window->x + 8;
    int oy = window->y + 24;
    int vw = window->w - 16;
    int vh = window->h - 32;

    /* sky */
    fill_rect(ox, oy, vw, vh, color_blue);
    /* water strip at the bottom */
    fill_rect(ox, oy + RUN_WATER_Y, vw, vh - RUN_WATER_Y, color_blue_dark);
    draw_text(ox + vw / 2 - 60, oy + RUN_WATER_Y + 8, "~ ~ ~ ~ ~ ~ ~ ~ ~ ~", color_white, color_blue_dark, true);

    /* platforms: grass top + dirt body, drawn from scroll offset */
    for (int i = 0; i < RUN_PLATFORMS; ++i) {
        int sx = run_platforms[i].x - run_scroll;
        int px = ox + sx;

        if (px + run_platforms[i].w < ox || px > ox + vw) {
            continue;
        }
        /* grass top */
        fill_rect(ox + sx, oy + run_platforms[i].y, run_platforms[i].w, 6, color_green);
        /* dirt body */
        fill_rect(ox + sx, oy + run_platforms[i].y + 6, run_platforms[i].w, 18, color_orange);
        for (int d = 0; d < run_platforms[i].w; d += 8) {
            draw_pixel(ox + sx + d + 3, oy + run_platforms[i].y + 10, color_black);
            draw_pixel(ox + sx + d, oy + run_platforms[i].y + 16, color_black);
        }
    }

    /* bricks */
    for (int i = 0; i < RUN_BRICKS; ++i) {
        int sx = run_bricks[i].x - run_scroll;
        int px = ox + sx;

        if (px + 16 < ox || px > ox + vw) {
            continue;
        }
        draw_image_at(_binary_build_run_brick_bin_start, ox + sx, oy + run_bricks[i].y, true);
    }

    /* coins: 10-frame spin, frames 5-9 are the mirrored half */
    for (int i = 0; i < RUN_COINS; ++i) {
        if (run_coins[i].taken) {
            continue;
        }
        int sx = run_coins[i].x - run_scroll;
        int px = ox + sx;

        if (px + 16 < ox || px > ox + vw) {
            continue;
        }
        {
            const uint8_t *sprite = _binary_build_run_coin1_bin_start;
            bool mirrored = false;

            switch (run_coins[i].frame) {
                case 0: sprite = _binary_build_run_coin1_bin_start; break;
                case 1: sprite = _binary_build_run_coin2_bin_start; break;
                case 2: sprite = _binary_build_run_coin3_bin_start; break;
                case 3: sprite = _binary_build_run_coin4_bin_start; break;
                case 4: mirrored = true; sprite = _binary_build_run_coin4_bin_start; break;
                case 5: mirrored = true; sprite = _binary_build_run_coin3_bin_start; break;
                case 6: mirrored = true; sprite = _binary_build_run_coin2_bin_start; break;
                case 7: mirrored = true; sprite = _binary_build_run_coin3_bin_start; break;
                case 8: mirrored = true; sprite = _binary_build_run_coin4_bin_start; break;
                default: sprite = _binary_build_run_coin1_bin_start; break;
            }
            if (mirrored) {
                /* horizontal flip draw for the return half of the spin */
                int w = image_width(sprite);
                int h = image_height(sprite);
                const uint8_t *alpha = image_has_alpha(sprite) ? image_alpha(sprite) : NULL;

                for (int yy = 0; yy < h; ++yy) {
                    for (int xx = 0; xx < w; ++xx) {
                        size_t idx = (size_t)yy * w + xx;

                        if (alpha == NULL || alpha[idx] >= 128) {
                            draw_pixel(ox + sx + (w - 1 - xx), oy + run_coins[i].y + yy,
                                       image_pixel_index(sprite, idx));
                        }
                    }
                }
            } else {
                draw_image_at(sprite, ox + sx, oy + run_coins[i].y, true);
            }
        }
    }

    /* player: died sprite during the game-over jump animation (state 2),
     * normal running sprite otherwise (hidden once fully dead, state 3) */
    if (run_state == 2) {
        draw_image_at(_binary_build_run_player_died_bin_start, ox + run_player_x, oy + run_player_y, true);
    } else if (run_state != 3) {
        draw_image_at(_binary_build_run_player_bin_start, ox + run_player_x, oy + run_player_y, true);
    }

    /* skull chaser: slides along the ground behind the player */
    if (run_skull_active && run_state == 1) {
        int skull_sx = run_skull_wx / RUN_SUB - run_scroll;

        if (skull_sx > -48 && skull_sx < vw) {
            /* ground-hugging y: bottom of skull at platform level ~156 */
            draw_image_at(_binary_build_run_skull_bin_start, ox + skull_sx, oy + 156 - 48, true);
        }
    }

    /* HUD: lives at top-left ([player head] x N), timer top-right.
     * lives icon = player.png 16x16 sprite. */
    draw_image_at(_binary_build_run_player16_bin_start, ox + 6, oy + 6, true);
    {
        char lives_text[16] = {0};
        size_t len = 0;

        copy_string(lives_text, " x ", sizeof(lives_text));
        len = strlen_local(lives_text);
        append_uint(lives_text, &len, sizeof(lives_text), (uint32_t)run_lives);
        draw_text(ox + 24, oy + 8, lives_text, color_white, color_blue, true);
    }
    {
        char time_text[24] = {0};
        size_t len = 0;
        uint32_t secs = run_time_left / TIMER_HZ;

        copy_string(time_text, "TIME ", sizeof(time_text));
        len = strlen_local(time_text);
        append_uint(time_text, &len, sizeof(time_text), secs);
        draw_text(ox + vw - 84, oy + 8, time_text, color_white, color_blue, true);
        /* timer bar countdown */
        int bar_max = 72;
        int bar_w = (int)(run_time_left * bar_max / (60u * TIMER_HZ));

        if (bar_w < 0) bar_w = 0;
        if (bar_w > bar_max) bar_w = bar_max;
        fill_rect(ox + vw - 84, oy + 18, bar_max, 4, color_gray_dark);
        fill_rect(ox + vw - 84, oy + 18, bar_w, 4, run_time_left < 10 * TIMER_HZ ? color_red : color_green);
    }
    /* coin counter next to lives */
    {
        char coin_text[24] = {0};
        size_t len = 0;

        copy_string(coin_text, "COINS ", sizeof(coin_text));
        len = strlen_local(coin_text);
        append_uint(coin_text, &len, sizeof(coin_text), (uint32_t)run_coins_collected);
        draw_text(ox + 70, oy + 8, coin_text, color_yellow, color_blue, true);
    }

    /* state overlays */
    if (run_state == 0) {
        /* 5 second countdown until GO */
        char count[8] = {0};
        size_t len = 0;
        uint32_t secs = (run_countdown + TIMER_HZ - 1) / TIMER_HZ;

        append_uint(count, &len, sizeof(count), secs);
        draw_text_center(ox + vw / 2, oy + vh / 2 - 10, count, color_white, color_blue, true);
        draw_text_center(ox + vw / 2, oy + vh / 2 + 8, "Get ready to RUN!", color_white, color_blue, true);
    } else if (run_state == 3) {
        draw_text_center(ox + vw / 2, oy + vh / 2 - 10, "GAME OVER!", color_red, color_blue, true);
        draw_text_center(ox + vw / 2, oy + vh / 2 + 8, "Press ENTER to play again", color_white, color_blue, true);
    } else if (run_state == 4) {
        draw_text_center(ox + vw / 2, oy + vh / 2 - 10, "YOU WIN!", color_yellow, color_blue, true);
        draw_text_center(ox + vw / 2, oy + vh / 2 + 8, "Press ENTER to play again", color_white, color_blue, true);
    }
}

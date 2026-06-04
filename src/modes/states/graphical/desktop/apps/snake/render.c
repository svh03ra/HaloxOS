// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, snake rendering.

// This repository is licensed under the GNU General Public License.

static void render_snake(const Window *window) {
    char score[24] = {0};
    int ox = window->x + 20;
    int oy = window->y + 34;
    fill_rect(ox, oy, 200, 140, color_black);
    draw_rect(ox, oy, 200, 140, color_gray);
    for (int y = 0; y < 14; ++y) {
        for (int x = 0; x < 20; ++x) {
            draw_rect(ox + x * 10, oy + y * 10, 10, 10, color_gray_dark);
        }
    }
    for (int i = 0; i < snake_length; ++i) {
        fill_rect(ox + snake_x[i] * 10 + 1, oy + snake_y[i] * 10 + 1, 8, 8, i == 0 ? color_green : color_green_dark);
    }
    fill_rect(ox + snake_food_x * 10 + 1, oy + snake_food_y * 10 + 1, 8, 8, color_red);
    draw_text(window->x + 223, window->y + 46, "Use arrow", color_black, color_gray_light, true);
    draw_text(window->x + 223, window->y + 55, "keys.", color_black, color_gray_light, true);
    format_snake_score(score, sizeof(score));
    draw_text(window->x + 20, window->y + 182, "SCORE:", color_black, color_gray_light, true);
    draw_text(window->x + 76, window->y + 182, score, color_blue_dark, color_gray_light, true);
    if (snake_dead) {
        draw_text(window->x + 221, window->y + 90, "GAME OVER!", color_red, color_gray_light, true);
        draw_text(window->x + 222, window->y + 105, "ENTER to", color_red, color_gray_light, true);
        draw_text(window->x + 223, window->y + 115, "restart.", color_red, color_gray_light, true);
    }
}

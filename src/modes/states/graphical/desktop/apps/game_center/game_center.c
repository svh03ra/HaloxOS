// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: game_center.c, game center app.

// This repository is licensed under the GNU General Public License.

static void render_game_center(const Window *window) {
    static const char *games[] = {"Minesweeper", "Snake", "Guess Number", "Run! Run", "DOOM"};
    static const AppId game_apps[] = {APP_MINES, APP_SNAKE, APP_GUESS, APP_RUN_GAME, APP_DOOM};
    int header_icon_w = image_width(_binary_build_game_icon_bin_start);
    int header_text_w = text_pixel_width("Game Center");
    int header_x = window->x + (window->w - (header_icon_w + 8 + header_text_w)) / 2;
    int list_y = window->y + 98;
    int row_x = window->x + 20;
    int row_w = window->w - 40;
    int hover_row = -1;

    /* Hover only when this window is the FRONT one: otherwise the
     * rows keep animating through an app that covers them. */
    if (active_window == APP_GAME_CENTER) {
        for (int i = 0; i < 5; ++i) {
            if (point_in_rect(mouse.x, mouse.y, row_x, list_y + i * 34, row_w, 36)) {
                hover_row = i;
                break;
            }
        }
    }
    if (hover_row != game_center_hover_row) {
        game_center_hover_row = hover_row;
        game_center_hover_tick = timer_ticks;
    }

    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_white);
    draw_image_at(_binary_build_game_icon_bin_start, header_x, window->y + 36, true);
    draw_text(header_x + header_icon_w + 8, window->y + 48, "Game Center", color_black, color_white, true);
    draw_text(window->x + 20, window->y + 80, "Welcome games! Choose one to play:", color_black, color_white, true);

    for (int i = 0; i < 5; ++i) {
        int row_y = list_y + i * 34;
        bool hover = game_center_hover_row == i;
        bool pressed = hover && mouse.left;
        int offset = pressed ? 1 : 0;
        int icon_h = image_height(app_icon_image(game_apps[i]));
        int text_y = row_y + (36 - 8) / 2;   /* vertically centered text */

        draw_interactive_row(row_x, row_y, row_w, 36, "", color_black, hover, pressed, game_center_hover_tick);
        draw_image_at(app_icon_image(game_apps[i]), window->x + 24 + offset, row_y + (36 - icon_h) / 2 + offset, true);
        draw_text(window->x + 64 + offset, text_y + offset, games[i], color_black, color_white, true);
    }
}

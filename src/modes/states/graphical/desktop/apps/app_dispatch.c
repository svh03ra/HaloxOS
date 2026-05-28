// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: app_dispatch.c, all of dispatches handling.

// This repository is licensed under the GNU General Public License.

static void render_app_window(AppId app) {
    Window *window = &windows[app];
    if (!window->open) {
        return;
    }

    draw_window_chrome(window);

    switch (app) {
        case APP_NOTEPAD: render_notepad(window); break;
        case APP_CMD: render_terminal(&cmd_term, window->x + 8, window->y + 26, window->w - 16, window->h - 34); break;
        case APP_PAINT: render_paint(window); break;
        case APP_EXPLORER: render_explorer(window); break;
        case APP_SNAKE: render_snake(window); break;
        case APP_GUESS: render_guess(window); break;
        case APP_MINES: render_mines(window); break;
        case APP_GAME_CENTER: render_game_center(window); break;
        case APP_POWER: render_power(window); break;
        case APP_SETTINGS: render_settings(window); break;
        case APP_TASK_MANAGER: render_task_manager(window); break;
    }
}

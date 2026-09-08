// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, game center input handling.

// This repository is licensed under the GNU General Public License.

static void game_center_handle_mouse(void) {
    if (!windows[APP_GAME_CENTER].open || active_window != APP_GAME_CENTER || !mouse.left || mouse.prev_left) {
        return;
    }

    Window *window = &windows[APP_GAME_CENTER];
    int row_x = window->x + 20;
    int row_y = window->y + 98;
    int row_w = window->w - 40;

    /* Only accept clicks that land inside this window's client area:
     * without this, a click that activates another overlapping window
     * (or the desktop) could still fall through to these rows because
     * this handler runs after the window-hit pass. */
    if (!point_in_rect(mouse.x, mouse.y, window->x, window->y, window->w, window->h - 18)) {
        return;
    }

    if (point_in_rect(mouse.x, mouse.y, row_x, row_y, row_w, 36)) {
        open_window(APP_MINES);
    } else if (point_in_rect(mouse.x, mouse.y, row_x, row_y + 34, row_w, 36)) {
        open_window(APP_SNAKE);
    } else if (point_in_rect(mouse.x, mouse.y, row_x, row_y + 68, row_w, 36)) {
        open_window(APP_GUESS);
    } else if (point_in_rect(mouse.x, mouse.y, row_x, row_y + 102, row_w, 36)) {
        open_window(APP_RUN_GAME);
    }
}

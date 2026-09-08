// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, demo center input handling.

// This repository is licensed under the GNU General Public License.

static void demo_center_handle_mouse(void) {
    if (!windows[APP_DEMO_CENTER].open || active_window != APP_DEMO_CENTER || !mouse.left || mouse.prev_left) {
        return;
    }

    Window *window = &windows[APP_DEMO_CENTER];
    int row_x = window->x + 20;
    int row_y = window->y + 108;
    int row_w = window->w - 40;

    /* Only accept clicks inside this window's client area so an
     * overlapping window never triggers these rows. */
    if (!point_in_rect(mouse.x, mouse.y, window->x, window->y, window->w, window->h - 18)) {
        return;
    }

    if (point_in_rect(mouse.x, mouse.y, row_x, row_y, row_w, 36)) {
        open_window(APP_3D_BOX);
    } else if (point_in_rect(mouse.x, mouse.y, row_x, row_y + 34, row_w, 36)) {
        open_window(APP_FIRECRACKER);
    }
}

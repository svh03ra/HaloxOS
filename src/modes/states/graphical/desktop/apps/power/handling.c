// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, power menu input handling.

// This repository is licensed under the GNU General Public License.

/*
 * The overlay's own hit test, built from the same geometry the renderer uses
 * (power.c): where the dialog is, where each row is inside it, and how big a
 * row is.  It used to repeat those numbers as literals - including a second
 * copy of the dialog rectangle for the outside-click test - so the buttons
 * and their hit boxes were only ever correct by coincidence.
 */
static bool power_handle_overlay_mouse(void) {
    int x;
    int y;
    int button_x;

    if (!power_menu_open || !mouse.left || mouse.prev_left) {
        return false;
    }

    power_overlay_origin(&x, &y);
    button_x = x + POWER_BUTTON_INSET;
    if (point_in_rect(mouse.x, mouse.y, button_x, y + POWER_ROW_SHUTDOWN, POWER_BUTTON_W, POWER_BUTTON_H)) {
        debug_bp_catch_power("shutdown (overlay menu)");
        shutdown_system();
    } else if (point_in_rect(mouse.x, mouse.y, button_x, y + POWER_ROW_RESTART, POWER_BUTTON_W, POWER_BUTTON_H)) {
        debug_bp_catch_power("restart (overlay menu)");
        restart_system();
    } else if (point_in_rect(mouse.x, mouse.y, button_x, y + POWER_ROW_HALT, POWER_BUTTON_W, POWER_BUTTON_H)) {
        serial_trace("INFO", "power menu halt requested");
        debug_bp_catch_power("halt (overlay menu)");
        cpu_halted_overlay = true;
    } else if (point_in_rect(mouse.x, mouse.y, button_x, y + POWER_ROW_CLOSE, POWER_BUTTON_W, POWER_ROW_CLOSE_H) ||
               !point_in_rect(mouse.x, mouse.y, x, y, POWER_OVERLAY_W, POWER_OVERLAY_H)) {
        power_menu_open = false;
    }
    return true;
}

static void power_handle_mouse(void) {
    if (!windows[APP_POWER].open || active_window != APP_POWER || !mouse.left || mouse.prev_left) {
        return;
    }

    Window *window = &windows[APP_POWER];
    /* Only clicks inside this window reach these buttons (see the window
     * hit pass); an overlap must never trigger shutdown/restart. */
    if (!point_in_rect(mouse.x, mouse.y, window->x, window->y, window->w, window->h)) {
        return;
    }
    int x = window->x + 20;
    int y = window->y + 36;
    if (point_in_rect(mouse.x, mouse.y, x, y, 160, 24)) {
        debug_bp_catch_power("shutdown (power app)");
        shutdown_system();
    } else if (point_in_rect(mouse.x, mouse.y, x, y + 32, 160, 24)) {
        debug_bp_catch_power("restart (power app)");
        restart_system();
    } else if (point_in_rect(mouse.x, mouse.y, x, y + 64, 160, 24)) {
        serial_trace("INFO", "power window halt requested");
        debug_bp_catch_power("halt (power app)");
        cpu_halted_overlay = true;
    } else if (point_in_rect(mouse.x, mouse.y, x, y + 96, 160, 24)) {
        close_window(APP_POWER);
    }
}

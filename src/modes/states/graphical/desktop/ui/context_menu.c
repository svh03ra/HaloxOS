// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: context_menu.c, desktop context menu logic.

// This repository is licensed under the GNU General Public License.

static void handle_taskbar_menu_click(void) {
    if (!taskbar_menu_open || !mouse.left || mouse.prev_left) {
        return;
    }
    if (!point_in_rect(mouse.x, mouse.y, taskbar_menu_x, taskbar_menu_y, 150, 16)) {
        taskbar_menu_open = false;
        return;
    }
    bool has_open = count_open_test_windows() > 0;
    for (int i = 0; i < APP_COUNT && !has_open; ++i) {
        if (windows[i].open) { has_open = true; }
    }
    if (!has_open) {
        taskbar_menu_open = false;
        return;
    }
    for (int i = 0; i < APP_COUNT; ++i) {
        if (windows[i].open) {
            close_window((AppId)i);
        }
    }
    for (int i = 0; i < test_window_count; ++i) {
        test_windows[i].open = false;
    }
    active_test_window = -1;
    taskbar_menu_open = false;
}

static void handle_context_menu_click(void) {
    if (!context_menu_open || !mouse.left || mouse.prev_left) {
        return;
    }

    if (!point_in_rect(mouse.x, mouse.y, context_menu_x, context_menu_y, 120, 64)) {
        context_menu_open = false;
        return;
    }

    int row = (mouse.y - context_menu_y) / 16;
    if (row == 0) {
        desktop_auto_grid = !desktop_auto_grid;
        if (desktop_auto_grid) {
            desktop_auto_grid_icons();
        }
        context_menu_open = false;
    } else if (row == 1) {
        load_desktop_icon_positions();
        context_menu_open = false;
    } else if (row == 2) {
        open_window(APP_EXPLORER);
        context_menu_open = false;
    } else if (row == 3) {
        open_window(APP_SETTINGS);
        context_menu_open = false;
    }
}

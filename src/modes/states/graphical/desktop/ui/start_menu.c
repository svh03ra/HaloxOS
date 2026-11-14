// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: start_menu.c, start menu logic.

// This repository is licensed under the GNU General Public License.

static bool start_menu_row_app(int row, AppId *app_out) {
    switch (row) {
#if HALOXOS_CONFIG_DEBUG
        case START_MENU_TEST_ROW: *app_out = APP_STRESS_TEST; return true;
#endif
        case 0: *app_out = APP_NOTEPAD; return true;
        case 1: *app_out = APP_CMD; return true;
        case 2: *app_out = APP_PAINT; return true;
        case 3: *app_out = APP_EXPLORER; return true;
        case 4: *app_out = APP_TASK_MANAGER; return true;
        case 6: *app_out = APP_GAME_CENTER; return true;
        case 7: *app_out = APP_DEMO_CENTER; return true;
        case 9: *app_out = APP_POWER; return true;
        case 10: *app_out = APP_SETTINGS; return true;
        default: return false;
    }
}

static void handle_start_menu_click(void) {
    int x = 0;
    int y = start_menu_y();

    if (!menu_open || ((!mouse.left || mouse.prev_left) && (!mouse.right || mouse.prev_right))) {
        return;
    }

    if (!point_in_rect(mouse.x, mouse.y, x, y, START_MENU_W, START_MENU_H)) {
        menu_open = false;
        return;
    }

    int row = start_menu_hit_row(x, y, mouse.x, mouse.y);
    AppId app;
    if (mouse.right && !mouse.prev_right && start_menu_row_app(row, &app)) {
        start_app_menu_open = true;
        start_app_menu_app = app;
        /* Spawn beside the start menu, not on top of the hovered row:
         * at the cursor the submenu covered the very item that opened it
         * (and its hover highlight), so it was easy to lose the row you
         * were on. */
        start_app_menu_x = clampi(START_MENU_W + 2, 0, OS_WIDTH - start_app_menu_width());
        start_app_menu_y = clampi(mouse.y - 6, 0, OS_HEIGHT - TASKBAR_H - START_APP_MENU_H);
        return;
    }
    if (mouse.left && !mouse.prev_left) {
        if (start_menu_row_app(row, &app)) {
            open_window(app);
        } else if (row == 11) {
            menu_open = false;
        }
    }
}

static bool start_menu_row_app(int row, AppId *app_out) {
    switch (row) {
        case 0: *app_out = APP_NOTEPAD; return true;
        case 1: *app_out = APP_CMD; return true;
        case 2: *app_out = APP_PAINT; return true;
        case 3: *app_out = APP_EXPLORER; return true;
        case 4: *app_out = APP_TASK_MANAGER; return true;
        case 6: *app_out = APP_GAME_CENTER; return true;
        case 9: *app_out = APP_POWER; return true;
        case 10: *app_out = APP_SETTINGS; return true;
        default: return false;
    }
}

static void handle_start_menu_click(void) {
    int x = 0;
    int y = OS_HEIGHT - TASKBAR_H - 214;

    if (!menu_open || ((!mouse.left || mouse.prev_left) && (!mouse.right || mouse.prev_right))) {
        return;
    }

    if (!point_in_rect(mouse.x, mouse.y, x, y, 180, 214)) {
        menu_open = false;
        return;
    }

    int row = start_menu_hit_row(x, y, mouse.x, mouse.y);
    AppId app;
    if (mouse.right && !mouse.prev_right && start_menu_row_app(row, &app)) {
        start_app_menu_open = true;
        start_app_menu_app = app;
        start_app_menu_x = clampi(mouse.x, 0, OS_WIDTH - 154);
        start_app_menu_y = clampi(mouse.y, 0, OS_HEIGHT - TASKBAR_H - 28);
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

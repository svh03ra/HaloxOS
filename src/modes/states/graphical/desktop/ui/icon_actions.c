static int desktop_find_slot_for_app(AppId app) {
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        if (!desktop_icon_visible[i] && desktop_icon_apps[i] == app) {
            return i;
        }
    }
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        if (!desktop_icon_visible[i]) {
            return i;
        }
    }
    return -1;
}

static void desktop_pin_app(AppId app, int x, int y) {
    int slot = desktop_find_slot_for_app(app);
    if (slot < 0) {
        return;
    }
    desktop_icon_apps[slot] = app;
    copy_string(desktop_icon_names[slot], app == APP_GAME_CENTER ? "Game Center" :
                                      app == APP_TASK_MANAGER ? "Task Manager" :
                                      app == APP_CMD ? "Terminal" : app_titles[app],
                sizeof(desktop_icon_names[slot]));
    desktop_icons[slot].x = clampi(x, 0, OS_WIDTH - 56);
    desktop_icons[slot].y = clampi(y, 0, OS_HEIGHT - TASKBAR_H - 56);
    desktop_icon_visible[slot] = true;
    selected_desktop_icon = slot;
}

static void desktop_begin_rename(int index) {
    if (index < 0 || index >= DESKTOP_ICON_COUNT || !desktop_icon_visible[index]) {
        return;
    }
    desktop_rename_active = true;
    desktop_rename_icon = index;
    copy_string(desktop_rename_buffer, desktop_icon_names[index], sizeof(desktop_rename_buffer));
    desktop_rename_len = (int)strlen_local(desktop_rename_buffer);
}

static void desktop_finish_rename(bool commit) {
    if (desktop_rename_active && commit && desktop_rename_icon >= 0 && desktop_rename_icon < DESKTOP_ICON_COUNT &&
        desktop_rename_buffer[0] != '\0') {
        copy_string(desktop_icon_names[desktop_rename_icon], desktop_rename_buffer, sizeof(desktop_icon_names[desktop_rename_icon]));
    }
    desktop_rename_active = false;
    desktop_rename_icon = -1;
    desktop_rename_len = 0;
}

static void handle_desktop_icon_menu_click(void) {
    if (!desktop_icon_menu_open || !mouse.left || mouse.prev_left) {
        return;
    }
    if (!point_in_rect(mouse.x, mouse.y, desktop_icon_menu_x, desktop_icon_menu_y, 120, 84)) {
        desktop_icon_menu_open = false;
        return;
    }
    int row = (mouse.y - desktop_icon_menu_y) / 15;
    int target = desktop_icon_menu_target;
    if (target < 0 || target >= DESKTOP_ICON_COUNT) {
        desktop_icon_menu_open = false;
        return;
    }
    if (row == 0 || row == 1) {
        desktop_clipboard_valid = true;
        desktop_clipboard_cut = row == 0;
        desktop_clipboard_app = desktop_icon_apps[target];
        copy_string(desktop_clipboard_name, desktop_icon_names[target], sizeof(desktop_clipboard_name));
        if (row == 0) {
            desktop_icon_visible[target] = false;
            selected_desktop_icon = -1;
        }
    } else if (row == 2 && desktop_clipboard_valid) {
        int slot = desktop_find_slot_for_app(desktop_clipboard_app);
        if (slot >= 0) {
            desktop_icon_apps[slot] = desktop_clipboard_app;
            copy_string(desktop_icon_names[slot], desktop_clipboard_name, sizeof(desktop_icon_names[slot]));
            desktop_icons[slot].x = clampi(desktop_icon_menu_x + 16, 0, OS_WIDTH - 56);
            desktop_icons[slot].y = clampi(desktop_icon_menu_y + 16, 0, OS_HEIGHT - TASKBAR_H - 56);
            desktop_icon_visible[slot] = true;
            selected_desktop_icon = slot;
            if (desktop_clipboard_cut) {
                desktop_clipboard_valid = false;
                desktop_clipboard_cut = false;
            }
        }
    } else if (row == 3) {
        desktop_begin_rename(target);
    } else if (row == 4) {
        desktop_icon_visible[target] = false;
        selected_desktop_icon = -1;
    }
    desktop_icon_menu_open = false;
}

static void handle_start_app_menu_click(void) {
    if (!start_app_menu_open || !mouse.left || mouse.prev_left) {
        return;
    }
    if (point_in_rect(mouse.x, mouse.y, start_app_menu_x, start_app_menu_y, 154, 28)) {
        desktop_pin_app(start_app_menu_app, 18, 24);
    }
    start_app_menu_open = false;
    menu_open = false;
}

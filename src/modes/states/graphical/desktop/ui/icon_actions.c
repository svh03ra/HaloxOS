// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: icon_actions.c, desktop icon actions (rename, copy, delete).

// This repository is licensed under the GNU General Public License.

static void desktop_finish_rename(bool commit);
static void desktop_copy_selected(int primary, bool cut);
static void desktop_delete_selected(int primary);

static void save_desktop_undo_state(void) {
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        desktop_undo_visible[i] = desktop_icon_visible[i];
        desktop_undo_x[i] = desktop_icons[i].x;
        desktop_undo_y[i] = desktop_icons[i].y;
        copy_string(desktop_undo_names[i], desktop_icon_names[i], DESKTOP_ICON_NAME_MAX + 1);
    }
    desktop_undo_valid = true;
}

static void desktop_undo(void) {
    if (!desktop_undo_valid) return;
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        desktop_icon_visible[i] = desktop_undo_visible[i];
        desktop_icons[i].x = desktop_undo_x[i];
        desktop_icons[i].y = desktop_undo_y[i];
        copy_string(desktop_icon_names[i], desktop_undo_names[i], DESKTOP_ICON_NAME_MAX + 1);
    }
    selected_desktop_icon = -1;
    memset_local(desktop_icon_multi_selected, 0, sizeof(desktop_icon_multi_selected));
    desktop_undo_valid = false;
}

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

static void desktop_paste_from_clipboard(int px, int py) {
    if (!desktop_clipboard_valid || desktop_clipboard_count <= 0) {
        return;
    }
    save_desktop_undo_state();
    int pasted = 0;
    for (int ci = 0; ci < desktop_clipboard_count; ++ci) {
        int slot = desktop_find_slot_for_app(desktop_clipboard_apps[ci]);
        if (slot >= 0) {
            desktop_icon_apps[slot] = desktop_clipboard_apps[ci];
            copy_string(desktop_icon_names[slot], desktop_clipboard_names[ci], sizeof(desktop_icon_names[slot]));
            desktop_icons[slot].x = clampi(px, 0, OS_WIDTH - 56);
            desktop_icons[slot].y = clampi(py + pasted * 28, 0, OS_HEIGHT - TASKBAR_H - 56);
            desktop_icon_visible[slot] = true;
            selected_desktop_icon = slot;
            ++pasted;
        }
    }
    if (desktop_clipboard_cut) {
        desktop_clipboard_valid = false;
        desktop_clipboard_cut = false;
        desktop_clipboard_count = 0;
    }
    desktop_auto_grid_icons();
}

static void desktop_copy_selected(int primary, bool cut) {
    if (primary < 0 || primary >= DESKTOP_ICON_COUNT) return;
    desktop_clipboard_valid = true;
    desktop_clipboard_cut = cut;
    desktop_clipboard_count = 0;
    desktop_clipboard_apps[desktop_clipboard_count] = desktop_icon_apps[primary];
    copy_string(desktop_clipboard_names[desktop_clipboard_count], desktop_icon_names[primary], DESKTOP_ICON_NAME_MAX + 1);
    ++desktop_clipboard_count;
    for (int i = 0; i < DESKTOP_ICON_COUNT && desktop_clipboard_count < DESKTOP_ICON_COUNT; ++i) {
        if (i != primary && desktop_icon_multi_selected[i] && desktop_icon_visible[i]) {
            desktop_clipboard_apps[desktop_clipboard_count] = desktop_icon_apps[i];
            copy_string(desktop_clipboard_names[desktop_clipboard_count], desktop_icon_names[i], DESKTOP_ICON_NAME_MAX + 1);
            ++desktop_clipboard_count;
        }
    }
    if (cut) {
        save_desktop_undo_state();
        for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
            if (i == primary || desktop_icon_multi_selected[i]) {
                desktop_icon_visible[i] = false;
            }
        }
        selected_desktop_icon = -1;
        memset_local(desktop_icon_multi_selected, 0, sizeof(desktop_icon_multi_selected));
        desktop_auto_grid_icons();
    }
}

static void desktop_delete_selected(int primary) {
    if (primary < 0 || primary >= DESKTOP_ICON_COUNT) return;
    save_desktop_undo_state();
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        if (i == primary || desktop_icon_multi_selected[i]) {
            desktop_icon_visible[i] = false;
        }
    }
    selected_desktop_icon = -1;
    memset_local(desktop_icon_multi_selected, 0, sizeof(desktop_icon_multi_selected));
    desktop_auto_grid_icons();
}

static void handle_desktop_icon_menu_click(void) {
    if (!desktop_icon_menu_open || !mouse.left || mouse.prev_left) {
        return;
    }
    if (!point_in_rect(mouse.x, mouse.y, desktop_icon_menu_x, desktop_icon_menu_y, 120, 75)) {
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
        desktop_copy_selected(target, row == 0);
    } else if (row == 2 && desktop_clipboard_valid) {
        desktop_paste_from_clipboard(desktop_icon_menu_x + 16, desktop_icon_menu_y + 16);
    } else if (row == 3) {
        desktop_begin_rename(target);
    } else if (row == 4) {
        desktop_delete_selected(target);
    }
    desktop_icon_menu_open = false;
    desktop_auto_grid_icons();
}

static void handle_start_app_menu_click(void) {
    if (!start_app_menu_open || !mouse.left || mouse.prev_left) {
        return;
    }
    if (point_in_rect(mouse.x, mouse.y, start_app_menu_x, start_app_menu_y, 154, 28)) {
        desktop_pin_app(start_app_menu_app, 18, 24);
        desktop_auto_grid_icons();
    }
    start_app_menu_open = false;
    menu_open = false;
}

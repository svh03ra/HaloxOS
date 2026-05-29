static void render_taskbar(void) {
    char datetime[40] = {0};
    int total_open = 0;
    int idx;

    fill_rect(0, OS_HEIGHT - TASKBAR_H, OS_WIDTH, TASKBAR_H, color_gray_dark);
    draw_rect(0, OS_HEIGHT - TASKBAR_H, OS_WIDTH, TASKBAR_H, color_black);
    draw_button(4, OS_HEIGHT - 24, 46, 18, "START", color_green, color_black, color_black);

    for (int i = 0; i < APP_COUNT; ++i) {
        if (windows[i].open) ++total_open;
    }
    int open_test = count_open_test_windows();
    if (open_test > 0) ++total_open;

    idx = taskbar_scroll;
    int slot = 0;
    for (; slot < 4 && idx < APP_COUNT; ++idx) {
        if (!windows[idx].open) continue;
        const char *title = app_titles[idx];
        char label[13];
        int title_len = (int)strlen_local(title);
        if (title_len > 9) {
            memcpy_local(label, title, 6);
            memcpy_local(label + 6, "...", 4);
            title = label;
        }
        draw_button(58 + slot * 88, OS_HEIGHT - 24, 84, 18, title,
                    active_window == idx ? color_blue : color_gray_light, color_black,
                    active_window == idx ? color_white : color_black);
        ++slot;
    }
    if (open_test > 0 && idx >= APP_COUNT && slot < 4) {
        char test_label[16];
        size_t nlen = 0;
        memcpy_local(test_label, "Test (", 7);
        nlen = 6;
        append_uint(test_label, &nlen, sizeof(test_label), (uint32_t)open_test);
        test_label[nlen] = ')';
        test_label[nlen + 1] = '\0';
        draw_button(58 + slot * 88, OS_HEIGHT - 24, 84, 18, test_label,
                    color_gray_light, color_black, color_black);
        ++slot;
        ++idx;
    }

    read_datetime(datetime, sizeof(datetime));
    int clock_x = OS_WIDTH - (int)strlen_local(datetime) * 8 - 8;

    if (total_open > 4) {
        int arrow_x = clock_x - 4 - 28;
        bool can_left = false;
        for (int i = taskbar_scroll - 1; i >= 0; --i) {
            if (windows[i].open) { can_left = true; break; }
        }
        bool can_right = false;
        for (int i = idx; i < APP_COUNT; ++i) {
            if (windows[i].open) { can_right = true; break; }
        }
        draw_button(arrow_x, OS_HEIGHT - 24, 12, 18, "<", can_left ? color_gray_light : color_gray, color_black, color_black);
        draw_button(arrow_x + 16, OS_HEIGHT - 24, 12, 18, ">", can_right ? color_gray_light : color_gray, color_black, color_black);
    }

    draw_text(clock_x, OS_HEIGHT - 20, datetime, color_white, color_gray_dark, true);
}

static int start_menu_item_y(int menu_y, int row) {
    switch (row) {
        case 0: return menu_y + 22;
        case 1: return menu_y + 38;
        case 2: return menu_y + 54;
        case 3: return menu_y + 70;
        case 4: return menu_y + 86;
        case 6: return menu_y + 120;
        case 9: return menu_y + 158;
        case 10: return menu_y + 174;
        case 11: return menu_y + 190;
        default: return -1;
    }
}

static int start_menu_hit_row(int menu_x, int menu_y, int px, int py) {
    static const int rows[] = {0, 1, 2, 3, 4, 6, 9, 10, 11};
    for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); ++i) {
        int row_y = start_menu_item_y(menu_y, rows[i]);
        if (point_in_rect(px, py, menu_x + 4, row_y, 172, 15)) {
            return rows[i];
        }
    }
    return -1;
}

static void draw_start_menu_item(int menu_x, int menu_y, int row, const char *label, uint8_t text) {
    int item_x = menu_x + 4;
    int item_y = start_menu_item_y(menu_y, row);
    int item_w = 172;
    int item_h = 15;
    bool hover = start_menu_hover_row == row;
    bool pressed = hover && mouse.left;

    draw_interactive_row(item_x, item_y, item_w, item_h, label, text, hover, pressed, start_menu_hover_tick);
}

static void render_taskbar_menu(void) {
    if (!taskbar_menu_open) return;
    int w = 150;
    int h = 16;
    bool has_open = count_open_test_windows() > 0;
    for (int i = 0; i < APP_COUNT && !has_open; ++i) {
        if (windows[i].open) { has_open = true; }
    }
    bool hover = has_open && point_in_rect(mouse.x, mouse.y, taskbar_menu_x, taskbar_menu_y, w, h);
    bool pressed = hover && mouse.left;
    uint8_t text = has_open ? color_black : color_gray_dark;
    if (!has_open) cursor_hand_hint = false;
    draw_interactive_row(taskbar_menu_x, taskbar_menu_y, w, h, "Close all windows", text, hover, pressed, 0);
    draw_rect(taskbar_menu_x, taskbar_menu_y, w, h, color_black);
}

static void render_start_menu(void) {
    if (!menu_open) {
        return;
    }
    int x = 0;
    int y = OS_HEIGHT - TASKBAR_H - 214;
    int hover_row = -1;
    if (point_in_rect(mouse.x, mouse.y, x, y, 180, 214)) {
        hover_row = start_menu_hit_row(x, y, mouse.x, mouse.y);
    }
    if (hover_row != start_menu_hover_row) {
        start_menu_hover_row = hover_row;
        start_menu_hover_tick = timer_ticks;
    }

    fill_rect(x, y, 180, 214, color_gray_light);
    draw_rect(x, y, 180, 214, color_black);
    draw_text(x + 8, y + 8, "Tools:", color_blue_dark, color_gray_light, true);
    draw_start_menu_item(x, y, 0, "Notepad", color_black);
    draw_start_menu_item(x, y, 1, "Command Prompt", color_black);
    draw_start_menu_item(x, y, 2, "Paint", color_black);
    draw_start_menu_item(x, y, 3, "Explorer", color_black);
    draw_start_menu_item(x, y, 4, "Task Manager", color_black);
    draw_text(x + 8, y + 106, "Games:", color_blue_dark, color_gray_light, true);
    draw_start_menu_item(x, y, 6, "Game Center", color_black);
    draw_text(x + 8, y + 144, "Options:", color_blue_dark, color_gray_light, true);
    draw_start_menu_item(x, y, 9, "Power Options", color_black);
    draw_start_menu_item(x, y, 10, "Settings", color_black);
    draw_start_menu_item(x, y, 11, "x Close", color_red);
}

static int context_menu_hit_row(int py) {
    int row = (py - context_menu_y) / 16;
    if (row >= 0 && row <= 3) {
        return row;
    }
    return -1;
}

static void draw_context_menu_item(int x, int y, int row, const char *label, uint8_t text) {
    int item_y = y + row * 16;
    int item_w = 120;
    int item_h = 16;
    bool hover = context_menu_hover_row == row;
    bool pressed = hover && mouse.left;

    draw_interactive_row(x, item_y, item_w, item_h, label, text, hover, pressed, context_menu_hover_tick);
}

static void render_context_menu(void) {
    if (!context_menu_open) {
        return;
    }
    int x = context_menu_x;
    int y = context_menu_y;
    int hover_row = -1;
    if (point_in_rect(mouse.x, mouse.y, x, y, 120, 64)) {
        hover_row = context_menu_hit_row(mouse.y);
    }
    if (hover_row != context_menu_hover_row) {
        context_menu_hover_row = hover_row;
        context_menu_hover_tick = timer_ticks;
    }

    {
        int item_y = y + 0 * 16;
        bool hover = context_menu_hover_row == 0;
        bool pressed = hover && mouse.left;
        draw_interactive_row(x, item_y, 120, 16, "", color_black, hover, pressed, context_menu_hover_tick);
        int cb_x = x + 8;
        int cb_y = item_y + 3;
        int cb_s = 10;
        draw_rect(cb_x, cb_y, cb_s, cb_s, color_black);
        if (desktop_auto_grid) {
            fill_rect(cb_x + 2, cb_y + 2, cb_s - 4, cb_s - 4, color_black);
        }
        draw_text(x + 24, item_y + 4, "Auto Grid", color_black, color_gray_light, true);
    }
    fill_rect(x + 8, y + 16 - 2, 104, 2, color_gray);
    draw_context_menu_item(x, y, 1, "Refresh", color_black);
    draw_context_menu_item(x, y, 2, "Explorer", color_black);
    draw_context_menu_item(x, y, 3, "Settings", color_black);
    draw_rect(x, y, 120, 64, color_black);
}

static int desktop_icon_menu_hit_row(int py) {
    int row = (py - desktop_icon_menu_y) / 15;
    if (row >= 0 && row <= 4) {
        return row;
    }
    return -1;
}

static void draw_desktop_icon_menu_item(int x, int y, int row, const char *label, uint8_t text, bool enabled) {
    int item_y = y + row * 15;
    int item_w = 120;
    int item_h = 15;
    bool hover = enabled && desktop_icon_menu_hover_row == row;
    bool pressed = hover && mouse.left;

    if (!enabled) {
        cursor_hand_hint = false;
    }

    draw_interactive_row(x, item_y, item_w, item_h, label, text, hover, pressed, desktop_icon_menu_hover_tick);
}

static void render_desktop_icon_menu(void) {
    static const char *items[] = {"Cut", "Copy", "Paste", "Rename", "Delete"};
    if (!desktop_icon_menu_open) {
        return;
    }
    int x = desktop_icon_menu_x;
    int y = desktop_icon_menu_y;
    int hover_row = -1;
    if (point_in_rect(mouse.x, mouse.y, x, y, 120, 75)) {
        hover_row = desktop_icon_menu_hit_row(mouse.y);
    }
    if (hover_row != desktop_icon_menu_hover_row) {
        desktop_icon_menu_hover_row = hover_row;
        desktop_icon_menu_hover_tick = timer_ticks;
    }

    for (int i = 0; i < 5; ++i) {
        bool enabled = (i != 2) || desktop_clipboard_valid;
        uint8_t text = enabled ? color_black : color_gray_dark;
        draw_desktop_icon_menu_item(x, y, i, items[i], text, enabled);
    }
    draw_rect(x, y, 120, 75, color_black);
}

static void render_start_app_menu(void) {
    if (!start_app_menu_open) {
        return;
    }
    int x = start_app_menu_x;
    int y = start_app_menu_y;
    int w = 154;
    int h = 28;
    bool hover = point_in_rect(mouse.x, mouse.y, x, y, w, h);
    bool pressed = hover && mouse.left;

    draw_interactive_row(x, y, w, h, "Pin to Desktop Icon", color_black, hover, pressed, 0);
    draw_rect(x, y, w, h, color_black);
}


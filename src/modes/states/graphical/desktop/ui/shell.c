static void render_taskbar(void) {
    char datetime[40] = {0};
    int tab_x = 58;

    fill_rect(0, OS_HEIGHT - TASKBAR_H, OS_WIDTH, TASKBAR_H, color_gray_dark);
    draw_rect(0, OS_HEIGHT - TASKBAR_H, OS_WIDTH, TASKBAR_H, color_black);
    draw_button(4, OS_HEIGHT - 24, 46, 18, "MENU", color_green, color_black, color_black);

    for (int app = 0; app < APP_COUNT; ++app) {
        if (!windows[app].open) {
            continue;
        }
        draw_button(tab_x, OS_HEIGHT - 24, 84, 18, app_titles[app], active_window == app ? color_blue : color_gray_light, color_black, active_window == app ? color_white : color_black);
        tab_x += 88;
    }

    read_datetime(datetime, sizeof(datetime));
    draw_text(OS_WIDTH - (int)strlen_local(datetime) * 8 - 8, OS_HEIGHT - 20, datetime, color_white, color_gray_dark, true);
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

static void render_context_menu(void) {
    if (!context_menu_open) {
        return;
    }
    fill_rect(context_menu_x, context_menu_y, 120, 60, color_gray_light);
    draw_rect(context_menu_x, context_menu_y, 120, 60, color_black);
    draw_text(context_menu_x + 8, context_menu_y + 8, "Refresh", color_black, color_gray_light, true);
    draw_text(context_menu_x + 8, context_menu_y + 24, "Explorer", color_black, color_gray_light, true);
    draw_text(context_menu_x + 8, context_menu_y + 40, "Settings", color_black, color_gray_light, true);
}

static void render_desktop_icon_menu(void) {
    static const char *items[] = {"Cut", "Copy", "Paste", "Rename", "Delete"};
    if (!desktop_icon_menu_open) {
        return;
    }
    fill_rect(desktop_icon_menu_x, desktop_icon_menu_y, 120, 84, color_gray_light);
    draw_rect(desktop_icon_menu_x, desktop_icon_menu_y, 120, 84, color_black);
    for (int i = 0; i < 5; ++i) {
        uint8_t text = (i == 2 && !desktop_clipboard_valid) ? color_gray_dark : color_black;
        draw_text(desktop_icon_menu_x + 8, desktop_icon_menu_y + 8 + i * 15, items[i], text, color_gray_light, true);
    }
}

static void render_start_app_menu(void) {
    if (!start_app_menu_open) {
        return;
    }
    fill_rect(start_app_menu_x, start_app_menu_y, 154, 28, color_gray_light);
    draw_rect(start_app_menu_x, start_app_menu_y, 154, 28, color_black);
    draw_text(start_app_menu_x + 8, start_app_menu_y + 10, "Pin to Desktop Icon", color_black, color_gray_light, true);
}


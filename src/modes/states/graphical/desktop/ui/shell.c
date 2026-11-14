// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: shell.c, taskbar shell rendering.

// This repository is licensed under the GNU General Public License.

/*
 * The taskbar clock box on its own.
 *
 * Separate from render_taskbar() because it is also the entire job of the
 * once-per-second clock tick (cursor_render_clock_only), which must not
 * recomposite the desktop. The box is filled first: draw_text() is
 * transparent, so the previous digits have to be painted out.
 */
static void render_taskbar_clock(void) {
    char clock_time[16] = {0};
    char clock_date[16] = {0};
    int clock_y = clock_widget_y();

    fill_rect(clock_widget_x(), clock_y, CLOCK_BOX_W, CLOCK_BOX_H, color_gray_dark);
    clock_widget_lines(clock_time, sizeof(clock_time), clock_date, sizeof(clock_date));
    draw_text(clock_line_x(clock_time), clock_y, clock_time, color_white, color_gray_dark, true);
    draw_text(clock_line_x(clock_date), clock_y + CLOCK_LINE_ADVANCE, clock_date, color_white, color_gray_dark, true);
}

static void render_taskbar(void) {
    TaskbarLayout layout;

    fill_rect(0, OS_HEIGHT - TASKBAR_H, OS_WIDTH, TASKBAR_H, color_gray_dark);
    draw_rect(0, OS_HEIGHT - TASKBAR_H, OS_WIDTH, TASKBAR_H, color_black);
    draw_button(4, OS_HEIGHT - 24, 46, 18, "START", color_green, color_black, color_black);

    layout = taskbar_layout();

    for (int slot = 0; slot < layout.count; ++slot) {
        int idx = taskbar_slot_app(&layout, slot);
        int x = taskbar_app_button_x(slot, layout.count);
        int w = taskbar_app_button_w(slot, layout.count);

        if (idx == TASKBAR_SLOT_TEST) {
            char test_label[16];
            size_t nlen = 0;
            memcpy_local(test_label, "Test (", 7);
            nlen = 6;
            append_uint(test_label, &nlen, sizeof(test_label), (uint32_t)layout.open_test);
            test_label[nlen] = ')';
            test_label[nlen + 1] = '\0';
            draw_button(x, OS_HEIGHT - 24, w, 18, test_label,
                        color_gray_light, color_black, color_black);
        } else if (idx >= 0) {
            const char *title = app_titles[idx];
            char label[16];
            int max_chars = (w - 8) / 8;
            int title_len;

            if (max_chars < 4) max_chars = 4;
            title_len = (int)strlen_local(title);
            if (title_len > max_chars) {
                int keep = max_chars - 3;
                if (keep > (int)sizeof(label) - 4) {
                    keep = (int)sizeof(label) - 4;
                }
                memcpy_local(label, title, keep);
                memcpy_local(label + keep, "...", 4);
                title = label;
            }
            draw_button(x, OS_HEIGHT - 24, w, 18, title,
                        active_window == idx ? color_blue : color_gray_light, color_black,
                        active_window == idx ? color_white : color_black);
        }
    }

    if (layout.arrows) {
        int arrow_x = taskbar_arrow_x();
        bool can_left = taskbar_can_scroll_left();
        bool can_right = taskbar_can_scroll_right();
        draw_button(arrow_x, OS_HEIGHT - 24, 12, 18, "<",
                    can_left ? color_gray_light : color_gray, color_black, color_black);
        draw_button(arrow_x + 16, OS_HEIGHT - 24, 12, 18, ">",
                    can_right ? color_gray_light : color_gray, color_black, color_black);
    }

    render_taskbar_clock();
}

/*
 * Start menu geometry.
 *
 * Debug builds put a "TEST ONLY:" block above the Tools rows.  It shifts
 * every existing row down by the same amount and grows the menu, so the
 * list does not cover the START button it opens from.  Release builds keep
 * the original 180x232 menu byte for byte.
 */
#define START_MENU_W       180
#define START_MENU_BASE_H  232
#if HALOXOS_CONFIG_DEBUG
#define START_MENU_TEST_SHIFT 34
#else
#define START_MENU_TEST_SHIFT 0
#endif
#define START_MENU_H (START_MENU_BASE_H + START_MENU_TEST_SHIFT)
#define START_MENU_TEST_ROW 12

static int start_menu_y(void) {
    return OS_HEIGHT - TASKBAR_H - START_MENU_H;
}

static int start_menu_item_y(int menu_y, int row) {
    int shift = START_MENU_TEST_SHIFT;

    switch (row) {
#if HALOXOS_CONFIG_DEBUG
        case START_MENU_TEST_ROW: return menu_y + 22;
#else
        /* Kept out of every hit test in release builds: this is below the
         * desktop, so no pointer coordinate can ever land on it. */
        case START_MENU_TEST_ROW: return -10000;
#endif
        case 0: return menu_y + 22 + shift;
        case 1: return menu_y + 38 + shift;
        case 2: return menu_y + 54 + shift;
        case 3: return menu_y + 70 + shift;
        case 4: return menu_y + 86 + shift;
        case 6: return menu_y + 120 + shift;
        case 7: return menu_y + 136 + shift;
        case 9: return menu_y + 174 + shift;
        case 10: return menu_y + 190 + shift;
        case 11: return menu_y + 206 + shift;
        default: return -1;
    }
}

static int start_menu_hit_row(int menu_x, int menu_y, int px, int py) {
    static const int rows[] = {START_MENU_TEST_ROW, 0, 1, 2, 3, 4, 6, 7, 9, 10, 11};
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
    int y = start_menu_y();
    int shift = START_MENU_TEST_SHIFT;
    int hover_row = -1;
    if (point_in_rect(mouse.x, mouse.y, x, y, START_MENU_W, START_MENU_H)) {
        hover_row = start_menu_hit_row(x, y, mouse.x, mouse.y);
    }
    if (hover_row != start_menu_hover_row) {
        start_menu_hover_row = hover_row;
        start_menu_hover_tick = timer_ticks;
    }

    fill_rect(x, y, START_MENU_W, START_MENU_H, color_gray_light);
    draw_rect(x, y, START_MENU_W, START_MENU_H, color_black);
#if HALOXOS_CONFIG_DEBUG
    /* Debug builds only: the stress tool is the very first entry in the
     * start menu, clearly marked so it cannot be mistaken for a normal app. */
    draw_text(x + 8, y + 8, "TEST ONLY:", color_red, color_gray_light, true);
    draw_start_menu_item(x, y, START_MENU_TEST_ROW, "Stress Test", color_black);
#endif
    draw_text(x + 8, y + 8 + shift, "Tools:", color_blue_dark, color_gray_light, true);
    draw_start_menu_item(x, y, 0, "Notepad", color_black);
    draw_start_menu_item(x, y, 1, "Command Prompt", color_black);
    draw_start_menu_item(x, y, 2, "Paint", color_black);
    draw_start_menu_item(x, y, 3, "Explorer", color_black);
    draw_start_menu_item(x, y, 4, "Task Manager", color_black);
    draw_text(x + 8, y + 106 + shift, "Games:", color_blue_dark, color_gray_light, true);
    draw_start_menu_item(x, y, 6, "Game Center", color_black);
    draw_start_menu_item(x, y, 7, "Demo Center", color_black);
    draw_text(x + 8, y + 160 + shift, "Options:", color_blue_dark, color_gray_light, true);
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
        fill_rect(cb_x + 1, cb_y + 1, cb_s - 2, cb_s - 2, color_white);
        if (desktop_auto_grid) {
            /* A real blue tick, not just a filled black square: the old
             * one-pixel fill was easy to read as "unchecked" against the
             * grey menu row, which made the Auto-Grid toggle look dead. */
            for (int tick = 0; tick < 3; ++tick) {
                draw_pixel(cb_x + 2 + tick, cb_y + 4 + tick, color_blue);
            }
            for (int tick = 0; tick < 5; ++tick) {
                draw_pixel(cb_x + 4 + tick, cb_y + 6 - tick, color_blue);
            }
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

/*
 * The start menu's app submenu: one row, sized from the label it shows.
 *
 * The size used to be the literal 154x28, repeated in four places - this
 * renderer, the placement clamp in start_menu.c, the pointer test in
 * cursor.c and the click test in icon_actions.c.  The label is 19 characters
 * and the font is 8 pixels wide, so it needs 152 pixels: the text fitted with
 * exactly one pixel to spare and its last letter sat against the border,
 * which is what made the row look off.  Deriving the size from the text keeps
 * the label, the box and every hit test in agreement, and leaves the label
 * room to be reworded.
 */
#define START_APP_MENU_LABEL "Pin to Desktop Icon"
#define START_APP_MENU_PAD   16   /* eight pixels either side of the text */
#define START_APP_MENU_H     28

static int start_app_menu_width(void) {
    return text_pixel_width(START_APP_MENU_LABEL) + START_APP_MENU_PAD;
}

static void render_start_app_menu(void) {
    if (!start_app_menu_open) {
        return;
    }
    int x = start_app_menu_x;
    int y = start_app_menu_y;
    int w = start_app_menu_width();
    int h = START_APP_MENU_H;
    bool hover = point_in_rect(mouse.x, mouse.y, x, y, w, h);
    bool pressed = hover && mouse.left;

    draw_interactive_row(x, y, w, h, START_APP_MENU_LABEL, color_black, hover, pressed, 0);
    draw_rect(x, y, w, h, color_black);
}

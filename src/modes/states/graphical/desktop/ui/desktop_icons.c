// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: desktop_icons.c, desktop icon rendering and logic.

// This repository is licensed under the GNU General Public License.

static const uint8_t *app_icon_image(AppId app) {
    switch (app) {
        case APP_NOTEPAD: return _binary_build_notepad_icon_bin_start;
        case APP_PAINT: return _binary_build_paint_icon_bin_start;
        case APP_POWER: return _binary_build_power_icon_bin_start;
        case APP_CMD: return _binary_build_terminal_icon_bin_start;
        case APP_EXPLORER: return _binary_build_explorer_icon_bin_start;
        case APP_GAME_CENTER: return _binary_build_game_icon_bin_start;
        case APP_SETTINGS: return _binary_build_settings_icon_bin_start;
        case APP_TASK_MANAGER: return _binary_build_taskmgr_icon_bin_start;
        case APP_MINES: return _binary_build_mines_icon_bin_start;
        case APP_SNAKE: return _binary_build_snake_icon_bin_start;
        case APP_GUESS: return _binary_build_guessnum_icon_bin_start;
        default: return _binary_build_program_icon_bin_start;
    }
}

static const uint8_t *desktop_icon_image(int index) {
    if (index < 0 || index >= DESKTOP_ICON_COUNT) {
        return _binary_build_program_icon_bin_start;
    }
    return app_icon_image(desktop_icon_apps[index]);
}

static const char *desktop_icon_label(int index) {
    if (index < 0 || index >= DESKTOP_ICON_COUNT) {
        return "";
    }
    return desktop_icon_names[index];
}

static AppId desktop_icon_app(int index) {
    if (index < 0 || index >= DESKTOP_ICON_COUNT) {
        return APP_NOTEPAD;
    }
    return desktop_icon_apps[index];
}

static void init_desktop_icon_defaults(void) {
    static const AppId apps[DESKTOP_ICON_COUNT] = {
        APP_NOTEPAD, APP_CMD, APP_EXPLORER, APP_TASK_MANAGER, APP_GAME_CENTER,
        APP_MINES, APP_SNAKE, APP_GUESS, APP_SETTINGS, APP_PAINT, APP_POWER
    };
    static const char *names[DESKTOP_ICON_COUNT] = {
        "Notepad", "Terminal", "Explorer", "Task Manager", "Game Center",
        "Minesweeper", "Snake", "Guess Number", "Settings", "Paint", "Power"
    };
    static const int visible_count = 2;

    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        desktop_icons[i].x = 18 + (i / 3) * 78;
        desktop_icons[i].y = 24 + (i % 3) * 80;
        desktop_icon_apps[i] = apps[i];
        desktop_icon_visible[i] = i < visible_count;
        copy_string(desktop_icon_names[i], names[i], sizeof(desktop_icon_names[i]));
    }
}

static uint32_t desktop_layout_checksum(const DesktopLayoutSector *layout) {
    uint32_t checksum = layout->magic;
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        checksum ^= (uint32_t)layout->icon_x[i] ^ (uint32_t)layout->icon_y[i];
    }
    return checksum;
}

static void desktop_icon_bounds(int x, int y, const uint8_t *image, const char *label, int *out_x, int *out_y, int *out_w, int *out_h) {
    int icon_w = image_width(image);
    int icon_h = image_height(image);
    int label_w = text_pixel_width(label);
    int width = (icon_w > label_w ? icon_w : label_w) + 8;

    *out_x = x;
    *out_y = y;
    *out_w = width;
    *out_h = icon_h + 24;
}

static void draw_dither_rect(int x, int y, int w, int h, uint8_t a, uint8_t b) {
    int x0 = clampi(x, 0, OS_WIDTH);
    int y0 = clampi(y, 0, OS_HEIGHT);
    int x1 = clampi(x + w, 0, OS_WIDTH);
    int y1 = clampi(y + h, 0, OS_HEIGHT);

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            draw_pixel(px, py, ((px + py) & 1) == 0 ? a : b);
        }
    }
}

static bool load_desktop_icon_positions(void) {
    uint8_t sector[512];
    const DesktopLayoutSector *layout = (const DesktopLayoutSector *)(const void *)sector;

    if (!boot_drive_valid || boot_drive_number < 0x80u) {
        return false;
    }
    if (!ata_pio_read_sector(DESKTOP_LAYOUT_LBA, sector)) {
        return false;
    }
    if (layout->magic != DESKTOP_LAYOUT_MAGIC || layout->checksum != desktop_layout_checksum(layout)) {
        return false;
    }

    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        if (layout->icon_x[i] < 0 || layout->icon_x[i] > OS_WIDTH - 64 ||
            layout->icon_y[i] < 0 || layout->icon_y[i] > OS_HEIGHT - TASKBAR_H - 64) {
            return false;
        }
        desktop_icons[i].x = layout->icon_x[i];
        desktop_icons[i].y = layout->icon_y[i];
    }

    desktop_icon_persistence_enabled = true;
    return true;
}

static void save_desktop_icon_positions(void) {
    uint8_t sector[512];
    DesktopLayoutSector *layout = (DesktopLayoutSector *)(void *)sector;

    if (!desktop_icon_persistence_enabled) {
        return;
    }

    memset_local(sector, 0, sizeof(sector));
    layout->magic = DESKTOP_LAYOUT_MAGIC;
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        layout->icon_x[i] = desktop_icons[i].x;
        layout->icon_y[i] = desktop_icons[i].y;
    }
    layout->checksum = desktop_layout_checksum(layout);
    ata_pio_write_sector(DESKTOP_LAYOUT_LBA, sector);
}

static void draw_desktop_icon(int index, bool selected) {
    int box_x;
    int box_y;
    int box_w;
    int box_h;
    int label_x;
    int label_y;
    int icon_x;
    int x = desktop_icons[index].x;
    int y = desktop_icons[index].y;
    const uint8_t *image = desktop_icon_image(index);
    const char *label = desktop_icon_label(index);
    int icon_w = image_width(image);
    int label_w = text_pixel_width(label);

    if (!desktop_icon_visible[index]) {
        return;
    }

    desktop_icon_bounds(x, y, image, label, &box_x, &box_y, &box_w, &box_h);
    icon_x = box_x + (box_w - icon_w) / 2;
    label_x = box_x + (box_w - label_w) / 2;
    label_y = box_y + image_height(image) + 10;

    if (desktop_rename_active && desktop_rename_icon == index) {
        int rename_w = text_pixel_width(desktop_rename_buffer);
        int rn_box_w = (rename_w > icon_w ? rename_w : icon_w) + 8;
        int rn_label_x = box_x + (rn_box_w - rename_w) / 2;
        draw_dither_rect(box_x - 2, box_y - 2, rn_box_w > box_w ? rn_box_w + 4 : box_w + 4, image_height(image) + 6, color_blue, color_blue_dark);
        fill_rect(rn_label_x - 4, label_y - 3, rename_w + 8, 14, color_blue);
        draw_rect(rn_label_x - 4, label_y - 3, rename_w + 8, 14, color_white);
        draw_text(rn_label_x, label_y, desktop_rename_buffer, color_white, color_blue, true);
        if ((timer_ticks / 15) & 1) {
            draw_char(rn_label_x + rename_w, label_y, '_', color_white, color_blue, true);
        }
    } else if (selected) {
        draw_dither_rect(box_x - 2, box_y - 2, box_w + 4, image_height(image) + 6, color_blue, color_blue_dark);
        fill_rect(label_x - 4, label_y - 2, label_w + 8, 12, color_blue);
        draw_text(label_x, label_y, label, color_white, color_blue, true);
    } else {
        draw_text(label_x, label_y, label, color_black, 0, true);
    }

    draw_image_at(image, icon_x, box_y, true);
}

static bool desktop_icon_intersects_rect(int idx, int rx, int ry, int rw, int rh) {
    if (idx < 0 || idx >= DESKTOP_ICON_COUNT || !desktop_icon_visible[idx]) {
        return false;
    }
    int ix, iy, iw, ih;
    desktop_icon_bounds(desktop_icons[idx].x, desktop_icons[idx].y,
                        desktop_icon_image(idx), desktop_icon_label(idx),
                        &ix, &iy, &iw, &ih);
    return ix < rx + rw && ix + iw > rx && iy < ry + rh && iy + ih > ry;
}

static int desktop_icon_hit_test(int x, int y) {
    for (int i = DESKTOP_ICON_COUNT - 1; i >= 0; --i) {
        int icon_x;
        int icon_y;
        int icon_w;
        int icon_h;

        if (!desktop_icon_visible[i]) {
            continue;
        }

        desktop_icon_bounds(desktop_icons[i].x, desktop_icons[i].y,
                            desktop_icon_image(i), desktop_icon_label(i),
                            &icon_x, &icon_y, &icon_w, &icon_h);
        if (point_in_rect(x, y, icon_x, icon_y, icon_w, icon_h)) {
            return i;
        }
    }
    return -1;
}

static uint8_t solid_color_index(uint8_t mode) {
    switch (mode) {
        case 2: return color_red;
        case 3: return color_orange;
        case 4: return color_yellow;
        case 5: return color_green;
        case 6: return color_blue;
        case 7: return color_pink;
        case 8: return color_white;
        case 9: return color_black;
        default: return color_blue_dark;
    }
}

static void desktop_auto_grid_icons(void) {
    if (!desktop_auto_grid) {
        return;
    }
    bool messy = false;
    for (int i = 0; i < DESKTOP_ICON_COUNT && !messy; ++i) {
        if (!desktop_icon_visible[i]) continue;
        int ax, ay, aw, ah;
        desktop_icon_bounds(desktop_icons[i].x, desktop_icons[i].y,
                            desktop_icon_image(i), desktop_icon_label(i),
                            &ax, &ay, &aw, &ah);
        if (ax + aw > OS_WIDTH || ay + ah > OS_HEIGHT - TASKBAR_H) {
            messy = true;
            break;
        }
        for (int j = i + 1; j < DESKTOP_ICON_COUNT; ++j) {
            if (!desktop_icon_visible[j]) continue;
            int bx, by, bw, bh;
            desktop_icon_bounds(desktop_icons[j].x, desktop_icons[j].y,
                                desktop_icon_image(j), desktop_icon_label(j),
                                &bx, &by, &bw, &bh);
            if (ax + aw > bx && bx + bw > ax && ay + ah > by && by + bh > ay) {
                messy = true;
                break;
            }
        }
    }
    if (!messy) return;

    int idx = 0;
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        if (desktop_icon_visible[i]) {
            desktop_icons[i].x = 18 + (idx / 3) * 78;
            desktop_icons[i].y = 24 + (idx % 3) * 80;
            ++idx;
        }
    }
    save_desktop_icon_positions();
}

static void draw_desktop_background(void) {
    if (settings_applied.background_mode == 0) {
        draw_image(_binary_build_theme1_bin_start);
    } else if (settings_applied.background_mode == 1) {
        draw_image(_binary_build_theme2_bin_start);
    } else {
        clear_screen(solid_color_index(settings_applied.background_mode));
    }
}

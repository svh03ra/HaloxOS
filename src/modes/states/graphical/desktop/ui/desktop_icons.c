// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: desktop_icons.c, desktop icon rendering and logic.

// This repository is licensed under the GNU General Public License.

/*
 * Paint order of the icon layer, bottom to top.
 *
 * Array order is the initial order, but with Auto-Grid off two icons can be
 * dropped on top of each other. Painting and hit-testing both walk this
 * list, and pressing an icon moves it to the top: the face the user can see
 * is then also the face a click grabs. Before this, icons were painted in
 * array order while the click test walked that order backwards, so the two
 * disagreed exactly when it mattered - an icon dropped over a lower-index
 * one was painted behind it yet could still swallow clicks on it (and a
 * dragged icon slid under its neighbours, looking like it had gone through
 * them).
 */
static int desktop_icon_order[DESKTOP_ICON_COUNT];
static bool desktop_icon_order_ready = false;

static void desktop_icon_order_init(void) {
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        desktop_icon_order[i] = i;
    }
    desktop_icon_order_ready = true;
}

/* Icon drawn at a position in the paint order, top of the list last. */
static int desktop_icon_paint_at(int position) {
    if (!desktop_icon_order_ready) {
        desktop_icon_order_init();
    }
    return desktop_icon_order[position];
}

/* Move an icon to the top of the paint order (pressing it raises it). */
static void desktop_icon_raise(int index) {
    int at = -1;

    if (index < 0 || index >= DESKTOP_ICON_COUNT) {
        return;
    }
    if (!desktop_icon_order_ready) {
        desktop_icon_order_init();
    }
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        if (desktop_icon_order[i] == index) {
            at = i;
            break;
        }
    }
    if (at < 0) {
        return;
    }
    for (int i = at; i < DESKTOP_ICON_COUNT - 1; ++i) {
        desktop_icon_order[i] = desktop_icon_order[i + 1];
    }
    desktop_icon_order[DESKTOP_ICON_COUNT - 1] = index;
}

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
        case APP_3D_BOX: return _binary_build_box3d_icon_bin_start;
        case APP_FIRECRACKER: return _binary_build_firecracker_icon_bin_start;
        case APP_DOOM: return _binary_build_doom_icon_bin_start;
        case APP_COIN_RUN: return _binary_build_coinrun_icon_bin_start;
        case APP_STRESS_TEST: return _binary_build_stress_icon_bin_start;
        case APP_DEMO_CENTER: return _binary_build_demo_icon_bin_start;
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
    desktop_icon_order_init();
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

/*
 * The rectangle an icon actually PAINTS: its image plus its label.
 *
 * desktop_icon_bounds() is a layout box: max(image, label) wide plus 8px of
 * padding and a fixed 24px of label room. It is the right thing to reserve
 * on the grid, but it is far bigger than the picture, so using it for hit
 * testing meant clicks on empty wallpaper beside an icon - and on the
 * label row of a neighbour - still grabbed it. "Touching outside the icon's
 * face" is exactly that padding being part of the target.
 */
static void desktop_icon_face_bounds(int index, int *out_x, int *out_y, int *out_w, int *out_h) {
    const uint8_t *image;
    const char *label;
    int box_x, box_y, box_w, box_h;
    int icon_w, icon_h, label_w;
    int icon_x, label_x, label_y;
    int x0, y0, x1, y1;

    if (index < 0 || index >= DESKTOP_ICON_COUNT) {
        *out_x = *out_y = *out_w = *out_h = 0;
        return;
    }

    image = desktop_icon_image(index);
    label = desktop_icon_label(index);
    icon_w = image_width(image);
    icon_h = image_height(image);
    label_w = text_pixel_width(label);

    desktop_icon_bounds(desktop_icons[index].x, desktop_icons[index].y,
                        image, label, &box_x, &box_y, &box_w, &box_h);
    icon_x = box_x + (box_w - icon_w) / 2;
    label_x = box_x + (box_w - label_w) / 2;
    label_y = box_y + icon_h + 10;

    x0 = icon_x;
    y0 = box_y;
    x1 = icon_x + icon_w;
    y1 = box_y + icon_h;
    if (label_x - 4 < x0) x0 = label_x - 4;
    if (label_x - 4 + label_w + 8 > x1) x1 = label_x - 4 + label_w + 8;
    if (label_y - 2 < y0) y0 = label_y - 2;
    if (label_y - 2 + 14 > y1) y1 = label_y - 2 + 14;

    *out_x = x0;
    *out_y = y0;
    *out_w = x1 - x0;
    *out_h = y1 - y0;
}

/*
 * Does a rectangle overlap an open window (app or test)?
 *
 * Desktop icons belong to the desktop layer, which every window paints
 * over. An icon that ends up under a window is therefore invisible, but a
 * drag used to paint the grabbed icon ABOVE the windows, so dragging one
 * across a window made both bleed through each other: the icon's
 * transparent pixels showed the window and the window's face showed the
 * icon. Keeping icons off every window rectangle removes both halves of
 * that, and is what Auto-Grid and the drag path below enforce.
 */
static bool desktop_rect_over_window(int x, int y, int w, int h) {
    for (int i = 0; i < APP_COUNT; ++i) {
        if (!windows[i].open) continue;
        if (x < windows[i].x + windows[i].w && windows[i].x < x + w &&
            y < windows[i].y + windows[i].h && windows[i].y < y + h) {
            return true;
        }
    }
    for (int i = 0; i < test_window_count; ++i) {
        if (!test_windows[i].open) continue;
        if (x < test_windows[i].x + test_windows[i].w && test_windows[i].x < x + w &&
            y < test_windows[i].y + test_windows[i].h && test_windows[i].y < y + h) {
            return true;
        }
    }
    return false;
}

/*
 * Keep everything that lives on the desktop inside a canvas that just
 * changed size.
 *
 * The canvas follows the resolution menu, so it can shrink as well as grow:
 * a window parked at x = 900 on a 1024x768 desktop would sit entirely off a
 * 640x480 one - title bar off-screen, so it could not even be dragged back -
 * and desktop icons would fall off the desktop the same way. Called from
 * update_canvas_for_framebuffer() whenever the canvas actually changes.
 */
static void desktop_fit_to_canvas(void) {
    int limit_x;
    int limit_y;

    for (int i = 0; i < APP_COUNT; ++i) {
        if (!windows[i].open) {
            continue;
        }
        limit_x = OS_WIDTH - windows[i].w;
        limit_y = OS_HEIGHT - TASKBAR_H - windows[i].h;
        if (limit_x < 0) limit_x = 0;
        if (limit_y < 0) limit_y = 0;
        windows[i].x = clampi(windows[i].x, 0, limit_x);
        windows[i].y = clampi(windows[i].y, 0, limit_y);
    }

    for (int i = 0; i < test_window_count; ++i) {
        if (!test_windows[i].open) {
            continue;
        }
        limit_x = OS_WIDTH - test_windows[i].w;
        limit_y = OS_HEIGHT - TASKBAR_H - test_windows[i].h;
        if (limit_x < 0) limit_x = 0;
        if (limit_y < 0) limit_y = 0;
        test_windows[i].x = clampi(test_windows[i].x, 0, limit_x);
        test_windows[i].y = clampi(test_windows[i].y, 0, limit_y);
    }

    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        int box_x = 0;
        int box_y = 0;
        int box_w = 0;
        int box_h = 0;

        desktop_icon_bounds(desktop_icons[i].x, desktop_icons[i].y,
                            desktop_icon_image(i), desktop_icon_label(i),
                            &box_x, &box_y, &box_w, &box_h);
        limit_x = OS_WIDTH - box_w;
        limit_y = OS_HEIGHT - TASKBAR_H - box_h;
        if (limit_x < 0) limit_x = 0;
        if (limit_y < 0) limit_y = 0;
        desktop_icons[i].x = clampi(desktop_icons[i].x, 0, limit_x);
        desktop_icons[i].y = clampi(desktop_icons[i].y, 0, limit_y);
    }
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

/*
 * The colour an icon's name has to be to stay readable on what is already
 * behind it.
 *
 * An unselected name is painted straight onto the desktop - there is no plate
 * under it - so one fixed colour can only ever be right for one background:
 * black names vanish on the Black theme and on any dark wallpaper, white ones
 * on White. Instead of guessing, sample the strip the label is about to
 * occupy (the wallpaper layer has already been drawn when icons are) and pick
 * the end of the brightness range that contrasts with it, which is what makes
 * the names readable on the solid Red/Orange/Yellow/Green/Blue/Pink themes as
 * well as on both paintings.
 *
 * Colour 0 is not "black": the label's background is transparent, so the text
 * colour alone decides. Channel weights are the usual perceptual ones (green
 * dominates), applied to the 5/6/5-bit framebuffer channels widened to 8 bits.
 */
static uint8_t desktop_label_text_color(int x, int y, int w, int h) {
    uint32_t sum = 0;
    int samples = 0;

    for (int py = y; py < y + h; py += 2) {
        if (py < 0 || py >= OS_HEIGHT - TASKBAR_H) {
            continue;
        }
        for (int px = x; px < x + w; px += 2) {
            uint16_t rgb;
            uint32_t r;
            uint32_t g;
            uint32_t b;

            if (px < 0 || px >= OS_WIDTH) {
                continue;
            }

            rgb = plane_get_pixel_rgb(px, py);
            r = (uint32_t)((rgb >> 11) & 0x1Fu) * 8u;
            g = (uint32_t)((rgb >> 5) & 0x3Fu) * 4u;
            b = (uint32_t)(rgb & 0x1Fu) * 8u;
            sum += (r * 30u + g * 59u + b * 11u) / 100u;
            ++samples;
        }
    }

    if (samples == 0) {
        return color_black;
    }

    return (sum / (uint32_t)samples) > 127u ? color_black : color_white;
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
        draw_text(label_x, label_y, label,
                  desktop_label_text_color(label_x, label_y, label_w, 8), 0, true);
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
    for (int pos = DESKTOP_ICON_COUNT - 1; pos >= 0; --pos) {
        int i = desktop_icon_paint_at(pos);
        int icon_x;
        int icon_y;
        int icon_w;
        int icon_h;

        if (!desktop_icon_visible[i]) {
            continue;
        }

        desktop_icon_face_bounds(i, &icon_x, &icon_y, &icon_w, &icon_h);
        if (point_in_rect(x, y, icon_x, icon_y, icon_w, icon_h)) {
            return i;
        }
    }
    return -1;
}

/*
 * Bounding box of everything a drag is currently moving: the grabbed icon
 * plus the rest of a multi-selection, which keeps its formation. Both the
 * window-overlap test and the edge-slide below work on this one box, so a
 * selection moves (or stops) as a single shape.
 */
static bool desktop_icon_group_box(int leader_x, int leader_y,
                                   int *out_x, int *out_y, int *out_w, int *out_h) {
    bool any = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        int nx, ny, bx, by, bw, bh;

        if (i != drag_desktop_icon && !desktop_icon_multi_selected[i]) continue;
        nx = i == drag_desktop_icon
            ? leader_x
            : leader_x + desktop_icon_drag_start_x[i] - desktop_icon_drag_start_x[drag_desktop_icon];
        ny = i == drag_desktop_icon
            ? leader_y
            : leader_y + desktop_icon_drag_start_y[i] - desktop_icon_drag_start_y[drag_desktop_icon];
        desktop_icon_bounds(nx, ny, desktop_icon_image(i), desktop_icon_label(i),
                            &bx, &by, &bw, &bh);
        if (!any) {
            x0 = bx;
            y0 = by;
            x1 = bx + bw;
            y1 = by + bh;
            any = true;
        } else {
            if (bx < x0) x0 = bx;
            if (by < y0) y0 = by;
            if (bx + bw > x1) x1 = bx + bw;
            if (by + bh > y1) y1 = by + bh;
        }
    }

    if (!any) {
        return false;
    }
    *out_x = x0;
    *out_y = y0;
    *out_w = x1 - x0;
    *out_h = y1 - y0;
    return true;
}

static bool desktop_icon_group_overlaps_icons(int bx, int by, int bw, int bh) {
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        if (i == drag_desktop_icon || desktop_icon_multi_selected[i]) {
            continue;
        }
        if (desktop_icon_intersects_rect(i, bx, by, bw, bh)) {
            return true;
        }
    }
    return false;
}

static bool desktop_icon_group_free(int leader_x, int leader_y) {
    int bx, by, bw, bh;

    if (!desktop_icon_group_box(leader_x, leader_y, &bx, &by, &bw, &bh)) {
        return true;
    }
    if (desktop_rect_over_window(bx, by, bw, bh)) {
        return false;
    }
    if (!desktop_auto_grid && desktop_icon_group_overlaps_icons(bx, by, bw, bh)) {
        return false;
    }
    return true;
}

/*
 * Move the grabbed icon (and the rest of the selection) to a new position.
 *
 * Two rules decide where it lands:
 *   - the 16x16 snap grid whenever Auto-Grid is on (or the debugger forced
 *     the grid overlay on, which is what makes that command visible),
 *   - never over an open window. If the wanted position would overlap one,
 *     the icon slides along the window's edge on whichever axis is still
 *     free and only then stops.
 *
 * Before this, a drag was free to cross a window and the grabbed icon was
 * painted after the windows to stay visible, so icon and window showed
 * through each other for the whole drag - and the icon could be dropped
 * somewhere it could never be grabbed again, because once the drag ended it
 * was painted by the (lower) desktop layer while the click router gave
 * those pixels to the window.
 */
static void desktop_icon_drag_to(int leader_x, int leader_y) {
    int cur_x, cur_y;
    int bx, by, bw, bh;

    if (drag_desktop_icon < 0 || drag_desktop_icon >= DESKTOP_ICON_COUNT) {
        return;
    }
    cur_x = desktop_icons[drag_desktop_icon].x;
    cur_y = desktop_icons[drag_desktop_icon].y;

    if (desktop_auto_grid || desktop_grid_overlay_on) {
        leader_x = (leader_x / DESKTOP_GRID_CELL) * DESKTOP_GRID_CELL;
        leader_y = (leader_y / DESKTOP_GRID_CELL) * DESKTOP_GRID_CELL;
    }

    if (!desktop_icon_group_box(leader_x, leader_y, &bx, &by, &bw, &bh)) {
        return;
    }
    if (desktop_rect_over_window(bx, by, bw, bh) ||
        (!desktop_auto_grid && desktop_icon_group_overlaps_icons(bx, by, bw, bh))) {
        if (desktop_icon_group_free(leader_x, cur_y)) {
            leader_y = cur_y;
        } else if (desktop_icon_group_free(cur_x, leader_y)) {
            leader_x = cur_x;
        } else {
            leader_x = cur_x;
            leader_y = cur_y;
        }
    }

    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        int nx, ny;

        if (i != drag_desktop_icon && !desktop_icon_multi_selected[i]) continue;
        nx = i == drag_desktop_icon
            ? leader_x
            : leader_x + desktop_icon_drag_start_x[i] - desktop_icon_drag_start_x[drag_desktop_icon];
        ny = i == drag_desktop_icon
            ? leader_y
            : leader_y + desktop_icon_drag_start_y[i] - desktop_icon_drag_start_y[drag_desktop_icon];
        desktop_icon_bounds(0, 0, desktop_icon_image(i), desktop_icon_label(i), &bx, &by, &bw, &bh);
        desktop_icons[i].x = clampi(nx, 0, OS_WIDTH - bw);
        desktop_icons[i].y = clampi(ny, 0, OS_HEIGHT - TASKBAR_H - bh);
    }
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

/* Slot grid Auto-Grid lays icons out on: the same three-rows-per-column,
 * 78x80 pitch the default layout uses, eight columns wide. More slots than
 * visible icons exist so a window covering the first few can be stepped
 * over instead of forcing an icon underneath it. */
#define DESKTOP_ICON_SLOT_ROWS 3
#define DESKTOP_ICON_SLOT_COLS 8
#define DESKTOP_ICON_SLOTS (DESKTOP_ICON_SLOT_ROWS * DESKTOP_ICON_SLOT_COLS)

/*
 * Tidy the icon layout: on-screen, not overlapping each other and - the
 * addition - not under an open window.
 *
 * A window paints over the desktop layer, so an icon left under one is
 * simply hidden: the desktop looks like it lost an icon and the window that
 * opens on top of it looks like it is "over" something invisible. Auto-Grid
 * is the right place to fix that, because it already owns "where do icons
 * belong" and already runs after every drag.
 *
 * Each icon takes the first slot that is neither covered by a window nor
 * already taken by an icon placed earlier in this pass; an icon with no
 * free slot keeps the position it has rather than being pushed under a
 * window.
 */
static void desktop_auto_grid_icons(void) {
    bool messy = false;
    int placed_x[DESKTOP_ICON_COUNT];
    int placed_y[DESKTOP_ICON_COUNT];
    int placed_w[DESKTOP_ICON_COUNT];
    int placed_h[DESKTOP_ICON_COUNT];
    int placed_count = 0;

    if (!desktop_auto_grid) {
        return;
    }
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
        if (desktop_rect_over_window(ax, ay, aw, ah)) {
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

    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        int bx, by, bw, bh;
        bool found = false;

        if (!desktop_icon_visible[i]) continue;

        for (int slot = 0; slot < DESKTOP_ICON_SLOTS && !found; ++slot) {
            int sx = 18 + (slot / DESKTOP_ICON_SLOT_ROWS) * 78;
            int sy = 24 + (slot % DESKTOP_ICON_SLOT_ROWS) * 80;
            bool clash = false;

            desktop_icon_bounds(sx, sy, desktop_icon_image(i), desktop_icon_label(i),
                                &bx, &by, &bw, &bh);
            if (bx + bw > OS_WIDTH || by + bh > OS_HEIGHT - TASKBAR_H) continue;
            if (desktop_rect_over_window(bx, by, bw, bh)) continue;
            for (int k = 0; k < placed_count && !clash; ++k) {
                if (bx < placed_x[k] + placed_w[k] && placed_x[k] < bx + bw &&
                    by < placed_y[k] + placed_h[k] && placed_y[k] < by + bh) {
                    clash = true;
                }
            }
            if (clash) continue;

            desktop_icons[i].x = sx;
            desktop_icons[i].y = sy;
            placed_x[placed_count] = bx;
            placed_y[placed_count] = by;
            placed_w[placed_count] = bw;
            placed_h[placed_count] = bh;
            ++placed_count;
            found = true;
        }
    }
    save_desktop_icon_positions();
}

/*
 * Paint a wallpaper image over the whole canvas, in the fit the Desktop tab
 * asks for.
 *
 * Stretch (the default) scales one copy of the painting to the canvas, so a
 * bigger desktop shows the same picture larger instead of repeating it - the
 * tile seams across the middle of a 1024x768 screen were the giveaway that
 * "more wallpaper" was the wrong reading of "more desktop".
 *
 * Fit keeps every painted pixel at its original size and centres the image,
 * leaving the rest of the canvas black (a small wallpaper on a big desktop
 * then reads as a picture on a wall instead of a stretched one).
 *
 * The fit only matters while the canvas and the art disagree; at the 640x480
 * base both paths draw the image 1:1.
 */
static void draw_wallpaper(const uint8_t *image) {
    int image_w = (int)image_width(image);
    int image_h = (int)image_height(image);

    if (image_w <= 0 || image_h <= 0) {
        return;
    }

    if (settings_applied.wallpaper_fit) {
        clear_screen(color_black);
        draw_image_at(image, (OS_WIDTH - image_w) / 2, (OS_HEIGHT - image_h) / 2, false);
        return;
    }

    draw_image_stretched(image);
}

static void draw_desktop_background(void) {
    if (settings_applied.background_mode == 0) {
        draw_wallpaper(_binary_build_theme1_bin_start);
    } else if (settings_applied.background_mode == 1) {
        draw_wallpaper(_binary_build_theme2_bin_start);
    } else {
        clear_screen(solid_color_index(settings_applied.background_mode));
    }
}

// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: cursor.c, desktop cursor and icon rendering.

// This repository is licensed under the GNU General Public License.

static void render_desktop_icons(void) {
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        draw_desktop_icon(i, i == selected_desktop_icon || desktop_icon_multi_selected[i]);
    }
}

static bool cursor_over_clickable(void) {
    if (cursor_hand_hint || desktop_icon_hit_test(mouse.x, mouse.y) >= 0) {
        return true;
    }
    if (context_menu_open && point_in_rect(mouse.x, mouse.y, context_menu_x, context_menu_y, 120, 64)) {
        return true;
    }
    if (desktop_icon_menu_open && point_in_rect(mouse.x, mouse.y, desktop_icon_menu_x, desktop_icon_menu_y, 120, 75)) {
        return true;
    }
    if (start_app_menu_open && point_in_rect(mouse.x, mouse.y, start_app_menu_x, start_app_menu_y, 154, 28)) {
        return true;
    }
    for (int app = 0; app < APP_COUNT; ++app) {
        Window *window = &windows[app];
        if (window->open && point_in_rect(mouse.x, mouse.y, window->x + window->w - 18, window->y + 3, 12, 12)) {
            return true;
        }
    }
    return false;
}

/*
 * Pointer save-under.
 *
 * Moving the pointer used to mean a full desktop recomposite plus a full
 * 640x480 scanout every single frame - the single most expensive thing a
 * user can do on a slow machine, and it happens constantly. Instead the
 * pixels the pointer is about to cover are kept aside, so a frame that
 * ONLY moved the pointer can put them back, redraw the pointer and scan
 * out just those two small rectangles.
 *
 * One plane is live at a time (see present_index_plane_live in vga.c), so
 * the save format is RGB565: exact for the RGB565 plane, and an exact
 * round trip for the 8bpp plane through the palette lookups.
 */
#define CURSOR_CAPTURE_W 24
#define CURSOR_CAPTURE_H 24

static uint16_t cursor_capture[CURSOR_CAPTURE_W * CURSOR_CAPTURE_H];
static int cursor_capture_x = 0;
static int cursor_capture_y = 0;
static int cursor_capture_w = 0;
static int cursor_capture_h = 0;
static bool cursor_capture_ok = false;
static bool cursor_captured_hand = false;

/* Pointer box for the current position: the hand pointer is 20x20 and
 * drawn six pixels left of the hotspot (one pixel lower while pressed),
 * the arrow is 16x12 with the hotspot at its tip. */
static void cursor_box(bool hand, bool pressed, int *x, int *y, int *w, int *h) {
    if (hand) {
        *x = mouse.x - 6;
        *y = mouse.y + (pressed ? 1 : 0);
        *w = 20;
        *h = 20 + (pressed ? 1 : 0);
    } else {
        *x = mouse.x;
        *y = mouse.y;
        *w = 16;
        *h = 12;
    }
}

/* Capture the (screen-clipped) box the pointer is about to cover. A box
 * partly off-screen still captures its visible part: the pointer itself
 * is clipped by draw_pixel anyway. */
static void cursor_capture_under(int x, int y, int w, int h, bool hand) {
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    cursor_capture_ok = false;
    if (x1 <= 0 || y1 <= 0 || x0 >= OS_WIDTH || y0 >= OS_HEIGHT) {
        return;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > OS_WIDTH) x1 = OS_WIDTH;
    if (y1 > OS_HEIGHT) y1 = OS_HEIGHT;
    if (x1 - x0 > CURSOR_CAPTURE_W || y1 - y0 > CURSOR_CAPTURE_H) {
        return;
    }

    for (int row = 0; row < y1 - y0; ++row) {
        for (int col = 0; col < x1 - x0; ++col) {
            cursor_capture[row * CURSOR_CAPTURE_W + col] =
                plane_get_pixel_rgb(x0 + col, y0 + row);
        }
    }

    cursor_capture_x = x0;
    cursor_capture_y = y0;
    cursor_capture_w = x1 - x0;
    cursor_capture_h = y1 - y0;
    cursor_captured_hand = hand;
    cursor_capture_ok = true;
}

/* Put the captured pixels back and forget them. */
static void cursor_capture_restore(void) {
    if (!cursor_capture_ok) {
        return;
    }

    for (int row = 0; row < cursor_capture_h; ++row) {
        for (int col = 0; col < cursor_capture_w; ++col) {
            int px = cursor_capture_x + col;
            int py = cursor_capture_y + row;
            plane_set_pixel_rgb((uint32_t)py * OS_WIDTH + (uint32_t)px,
                                cursor_capture[row * CURSOR_CAPTURE_W + col]);
        }
    }
    cursor_capture_ok = false;
}

/* Is the saved background still a valid stand-in for what is on screen?
 * Any repaint, overlay, pointer lock or pointer-shape change says no. */
static bool cursor_capture_valid(void) {
    if (!cursor_capture_ok) {
        return false;
    }
    if (debug_overlay_open || doom_pointer_locked()) {
        return false;
    }
    return cursor_over_clickable() == cursor_captured_hand;
}

static void render_cursor(void) {
    /* DOOM owns the pointer while its game window has focus.  Keeping the
     * cursor out of the game framebuffer also prevents a software cursor
     * from being baked into every captured/rendered DOOM frame. */
    if (doom_pointer_locked()) {
        cursor_capture_ok = false;
        return;
    }

    bool hand = cursor_over_clickable();
    int box_x;
    int box_y;
    int box_w;
    int box_h;

    /* Snapshot what the pointer is about to cover, then draw over it. */
    cursor_box(hand, mouse.left != 0, &box_x, &box_y, &box_w, &box_h);
    cursor_capture_under(box_x, box_y, box_w, box_h, hand);
    /* black outline */
    static const uint16_t outline[12] = {
        0b1000000000000000,
        0b1100000000000000,
        0b1110000000000000,
        0b1111000000000000,
        0b1111100000000000,
        0b1111110000000000,
        0b1111111000000000,
        0b1111111100000000,
        0b1111100000000000,
        0b1101100000000000,
        0b1000110000000000,
        0b0000011000000000
    };

    /* white interior */
    static const uint16_t fill[12] = {
        0b0000000000000000,
        0b0100000000000000,
        0b0110000000000000,
        0b0111000000000000,
        0b0111100000000000,
        0b0111110000000000,
        0b0111111000000000,
        0b0111111100000000,
        0b0111100000000000,
        0b0100100000000000,
        0b0000010000000000,
        0b0000000000000000
    };

    if (hand) {
        static const char hand_cursor[20][21] = {
            "....XXXXX...........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX.XXX......",
            "...XOOOOOXXOOOX.....",
            "...XOOOOOXXOOOX.XX..",
            "...XOOOOOXXOOOXXOOX.",
            "...XOOOOOXXOOOXXOOX.",
            "XX.XOOOOOXXOOOXXOOX.",
            "XOOXOOOOOOXOOOOOOOX.",
            "XOOOOOOOOOOOOOOOOOX.",
            ".XOOOOOOOOOOOOOOOX..",
            "..XOOOOOOOOOOOOOX...",
            "...XOOOOOOOOOOOX....",
            "....XOOOOOOOOX......",
            ".....XXXXXXXX......."
        };
        int press = mouse.left ? 1 : 0;
        int x = mouse.x - 6;
        int y = mouse.y + press;

        for (int row = 0; row < 20; ++row) {
            for (int col = 0; col < 20; ++col) {
                char pixel = hand_cursor[row][col];
                if (pixel == 'X') {
                    draw_pixel(x + col, y + row, color_black);
                } else if (pixel == 'O') {
                    draw_pixel(x + col, y + row, color_white);
                }
            }
        }
        return;
    }

    /* draw outline */
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 16; ++col) {
            if (outline[row] & (1u << (15 - col))) {
                draw_pixel(mouse.x + col, mouse.y + row, color_black);
            }
        }
    }

    /* draw fill */
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 16; ++col) {
            if (fill[row] & (1u << (15 - col))) {
                draw_pixel(mouse.x + col, mouse.y + row, color_white);
            }
        }
    }
}

/*
 * Pointer-only repaint: put the old save-under box back, redraw the
 * pointer at its new position (which captures the background there) and
 * scan out only the union of the two boxes. Called when the pointer
 * moved and nothing else on the desktop changed - the expensive case
 * that used to recomposite everything, dozens of times per second.
 */
static void cursor_render_motion_only(void) {
    int old_x = cursor_capture_x;
    int old_y = cursor_capture_y;
    int old_w = cursor_capture_w;
    int old_h = cursor_capture_h;
    bool had_old = cursor_capture_ok;
    int x0;
    int y0;
    int x1;
    int y1;

    cursor_capture_restore();
    render_cursor();

    x0 = had_old ? old_x : 0;
    y0 = had_old ? old_y : 0;
    x1 = had_old ? old_x + old_w : 0;
    y1 = had_old ? old_y + old_h : 0;

    if (cursor_capture_ok) {
        if (!had_old || cursor_capture_x < x0) x0 = cursor_capture_x;
        if (!had_old || cursor_capture_y < y0) y0 = cursor_capture_y;
        if (!had_old || cursor_capture_x + cursor_capture_w > x1) {
            x1 = cursor_capture_x + cursor_capture_w;
        }
        if (!had_old || cursor_capture_y + cursor_capture_h > y1) {
            y1 = cursor_capture_y + cursor_capture_h;
        }
    }

    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    present_rect(x0, y0, x1 - x0, y1 - y0);
}

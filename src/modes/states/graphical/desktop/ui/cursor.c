// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: cursor.c, desktop cursor and icon rendering.

// This repository is licensed under the GNU General Public License.

/*
 * The desktop icon layer.
 *
 * Icons are painted here - before any window - and nowhere else. They used
 * to be painted a second time after the windows while one was being
 * dragged, so a grabbed icon would show through a window it crossed; the
 * drag path now keeps icons off every window rectangle (see
 * desktop_icon_drag_to in desktop_icons.c), so the desktop layer stays the
 * single, consistent owner of the icon pixels.
 */
static void render_desktop_icons(void) {
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        draw_desktop_icon(i, i == selected_desktop_icon || desktop_icon_multi_selected[i]);
    }
}

/* 4x4 pixel snap-grid overlay.
 *
 * Off unless the debugger's "show grid" (aliases: app grid, desktop grid,
 * ag, dg, ...) turns it on, so the desktop does not start up looking like
 * graph paper. Auto-Grid still snaps icon drags to the same 4x4 grid; only
 * these dots are command-controlled. Light dots so it reads on any
 * wallpaper without hiding the icons.
 *
 * Counting a 4px lattice by eye does not work on a scaled output (a VBE
 * mode wider than the 640-wide canvas, or a zoomed emulator window): there
 * every logical pixel becomes an NxN block, so the same 4px step measures
 * 8 screen pixels at 2x and 16 at 4x, and the desktop reads as an 8x8 or
 * 16x16 grid even though nothing is snapped that coarsely. So every fourth
 * dot in both axes gets a small plus - enough to count the fine step at a
 * glance, without coarsening the snap. The dots still sit exactly where a
 * drag lands, which is the point of showing them. */
#define DESKTOP_GRID_MAJOR 16

static void draw_desktop_grid_overlay(void) {
    if (!desktop_grid_overlay_on) {
        return;
    }
    for (int gy = 0; gy < OS_HEIGHT - TASKBAR_H; gy += DESKTOP_GRID_CELL) {
        for (int gx = 0; gx < OS_WIDTH; gx += DESKTOP_GRID_CELL) {
            draw_pixel(gx, gy, color_gray);
            if ((gy % DESKTOP_GRID_MAJOR) == 0 && (gx % DESKTOP_GRID_MAJOR) == 0) {
                /* One pixel per arm, so a major mark is a crosshair and
                 * never reaches the next lattice dot 4px away. Darker than
                 * the dots so it stands out on the light wallpapers; on a
                 * black one the arms fade but the gray dots still draw the
                 * lattice. Negative coordinates clip inside draw_pixel. */
                draw_pixel(gx - 1, gy, color_gray_dark);
                draw_pixel(gx + 1, gy, color_gray_dark);
                draw_pixel(gx, gy - 1, color_gray_dark);
                draw_pixel(gx, gy + 1, color_gray_dark);
            }
        }
    }
}

/*
 * The hand cursor.
 *
 * The checks run in the desktop's own painting order, from the topmost
 * layer down, because "is something clickable here" has to agree with the
 * click router - and it did not:
 *
 *   - any widget that rendered itself over the pointer used to set
 *     cursor_hand_hint, including a button of a window that was painted
 *     over by another window, so the hand appeared on the window in front
 *     for no visible reason ("the buttons behind seem through");
 *   - a desktop icon that a window covers was still a hit, so the hand
 *     showed over the window's own face.
 *
 * Menus are drawn last, then the topmost window, then the desktop icons -
 * and the icons only count when nothing is drawn on top of them.
 */
static bool cursor_over_clickable_at(int px, int py) {
    const Window *window = desktop_top_window_at(px, py);

    if (cursor_hand_hint) {
        return true;
    }
    if (context_menu_open && point_in_rect(px, py, context_menu_x, context_menu_y, 120, 64)) {
        return true;
    }
    if (desktop_icon_menu_open && point_in_rect(px, py, desktop_icon_menu_x, desktop_icon_menu_y, 120, 75)) {
        return true;
    }
    if (start_app_menu_open &&
        point_in_rect(px, py, start_app_menu_x, start_app_menu_y,
                      start_app_menu_width(), START_APP_MENU_H)) {
        return true;
    }
    if (window != 0) {
        /* Only the topmost window's close box is clickable; a lower
         * window's close box under the overlap is not. */
        return point_in_rect(px, py,
                             window->x + window->w - 18, window->y + 3, 12, 12);
    }
    return desktop_icon_hit_test(px, py) >= 0;
}

static bool cursor_over_clickable(void) {
    return cursor_over_clickable_at(mouse.x, mouse.y);
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
static void cursor_box_at(bool hand, bool pressed, int px, int py, int *x, int *y, int *w, int *h) {
    if (hand) {
        *x = px - 6;
        *y = py + (pressed ? 1 : 0);
        *w = 20;
        *h = 20 + (pressed ? 1 : 0);
    } else {
        *x = px;
        *y = py;
        *w = 16;
        *h = 12;
    }
}

static void cursor_box(bool hand, bool pressed, int *x, int *y, int *w, int *h) {
    cursor_box_at(hand, pressed, mouse.x, mouse.y, x, y, w, h);
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
        /* No cursor is stamped while locked; book the live position so
         * the gate's painted-vs-live test does not force pointless full
         * repaints on a quiet locked DOOM. Pointer motion still dirties
         * the desktop through the input driver. */
        cursor_painted_x = mouse.x;
        cursor_painted_y = mouse.y;
        return;
    }

    /* One snapshot per stamp. IRQ12 can move the real pointer at any
     * instant - including between the capture below and the pixel loops
     * at the end of this function. Every read of the pointer state in
     * here must see the SAME values, or the cursor is stamped at
     * coordinates its own restore box does not cover: the orphaned
     * pixels stay in the framebuffer and later pointer-only frames
     * present them as a frozen "ghost" cursor - over idle app windows
     * like the Task Manager grid, where nothing else schedules a repaint
     * to wash them away. */
    int mx = mouse.x;
    int my = mouse.y;
    bool mleft = mouse.left != 0;

    cursor_painted_x = mx;
    cursor_painted_y = my;

    bool hand = cursor_over_clickable_at(mx, my);
    int box_x;
    int box_y;
    int box_w;
    int box_h;

    /* Snapshot what the pointer is about to cover, then draw over it. */
    cursor_box_at(hand, mleft, mx, my, &box_x, &box_y, &box_w, &box_h);
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
        int press = mleft ? 1 : 0;
        int x = mx - 6;
        int y = my + press;

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
                draw_pixel(mx + col, my + row, color_black);
            }
        }
    }

    /* draw fill */
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 16; ++col) {
            if (fill[row] & (1u << (15 - col))) {
                draw_pixel(mx + col, my + row, color_white);
            }
        }
    }
}

/* Book the live pointer position for a frame that did NOT stamp the
 * cursor (the debug overlay owns the screen). The redraw gate compares
 * the live pointer against cursor_painted_x/y; without this booking it
 * would see a permanent mismatch and schedule full repaints every frame
 * while the overlay sits idle. */
static void cursor_book_not_painted(void) {
    cursor_painted_x = mouse.x;
    cursor_painted_y = mouse.y;
}

/*
 * Scan out two regions that changed this frame.
 *
 * Touching rectangles are merged (the merge wastes only the pixels they
 * share), but two rectangles that do not meet are scanned out separately.
 * Their bounding box is NOT equivalent: the pointer parked mid-screen and
 * the taskbar clock in the corner are two 24x24-ish boxes whose bounding
 * box covers a quarter of the screen, and paying for that every second is
 * what made the once-per-second clock tick cost as much as a full frame.
 *
 * A region of zero area means "nothing changed there" and is skipped.
 */
static void present_changed_regions(int ax, int ay, int aw, int ah,
                                    int bx, int by, int bw, int bh) {
    if (aw <= 0 || ah <= 0) {
        if (bw > 0 && bh > 0) {
            present_rect(bx, by, bw, bh);
        }
        return;
    }
    if (bw <= 0 || bh <= 0) {
        present_rect(ax, ay, aw, ah);
        return;
    }

    bool apart = (ax + aw <= bx) || (bx + bw <= ax) ||
                 (ay + ah <= by) || (by + bh <= ay);

    /* Two rectangles are only cheaper than their bounding box when the
     * backend can actually scan out a rectangle; where it cannot, both
     * calls would paint the whole frame. */
    if (apart && present_partial_supported()) {
        present_rect(ax, ay, aw, ah);
        present_rect(bx, by, bw, bh);
        return;
    }

    {
        int x0 = ax < bx ? ax : bx;
        int y0 = ay < by ? ay : by;
        int x1 = (ax + aw > bx + bw) ? ax + aw : bx + bw;
        int y1 = (ay + ah > by + bh) ? ay + ah : by + bh;

        present_rect(x0, y0, x1 - x0, y1 - y0);
    }
}

/*
 * Pointer-only repaint: put the old save-under box back, redraw the
 * pointer at its new position (which captures the background there) and
 * scan out those two boxes. Called when the pointer moved and nothing
 * else on the desktop changed - the expensive case that used to
 * recomposite everything, dozens of times per second.
 */
static void cursor_render_motion_only(void) {
    int old_x = cursor_capture_x;
    int old_y = cursor_capture_y;
    int old_w = cursor_capture_w;
    int old_h = cursor_capture_h;
    bool had_old = cursor_capture_ok;
    int new_x;
    int new_y;
    int new_w;
    int new_h;

    cursor_capture_restore();
    render_cursor();

    new_x = cursor_capture_ok ? cursor_capture_x : 0;
    new_y = cursor_capture_ok ? cursor_capture_y : 0;
    new_w = cursor_capture_ok ? cursor_capture_w : 0;
    new_h = cursor_capture_ok ? cursor_capture_h : 0;

    present_changed_regions(had_old ? old_x : 0, had_old ? old_y : 0,
                            had_old ? old_w : 0, had_old ? old_h : 0,
                            new_x, new_y, new_w, new_h);
}

/*
 * Clock-only repaint: the taskbar clock shows seconds, so it needs a touch
 * every second, but the desktop must not be recomposited for it.
 *
 * Order matters. Put the pointer's save-under back first (only the desktop
 * changed underneath, so those pixels are still the truth), then repaint
 * the clock box, then redraw the pointer - which re-captures the background
 * there, now including the new time, so the next motion-only frame restores
 * the right pixels. The scanout covers the two regions that can differ: the
 * clock box, and the pointer's old box merged with its new one.
 *
 * Unlike the motion path this does not require a valid capture: if there is
 * none there is simply nothing to restore, and render_cursor() creates one.
 */
static void cursor_render_clock_only(void) {
    int clock_x = clock_widget_x();
    int clock_y = clock_widget_y();
    int old_x = cursor_capture_x;
    int old_y = cursor_capture_y;
    int old_w = cursor_capture_w;
    int old_h = cursor_capture_h;
    bool had_old = cursor_capture_ok;
    int pointer_x;
    int pointer_y;
    int pointer_w;
    int pointer_h;

    cursor_capture_restore();
    render_taskbar_clock();
    render_cursor();

    /* The pointer's old box (just restored) merged with the box it covers
     * now. Neither frame moves the pointer, so the two normally coincide
     * and the merge is exact; a shape change between them widens it by a
     * couple of pixels. */
    pointer_x = had_old ? old_x : 0;
    pointer_y = had_old ? old_y : 0;
    pointer_w = had_old ? old_w : 0;
    pointer_h = had_old ? old_h : 0;
    if (cursor_capture_ok) {
        if (pointer_w <= 0 || pointer_h <= 0) {
            pointer_x = cursor_capture_x;
            pointer_y = cursor_capture_y;
            pointer_w = cursor_capture_w;
            pointer_h = cursor_capture_h;
        } else {
            int x1 = pointer_x + pointer_w;
            int y1 = pointer_y + pointer_h;

            if (cursor_capture_x < pointer_x) pointer_x = cursor_capture_x;
            if (cursor_capture_y < pointer_y) pointer_y = cursor_capture_y;
            if (cursor_capture_x + cursor_capture_w > x1) {
                x1 = cursor_capture_x + cursor_capture_w;
            }
            if (cursor_capture_y + cursor_capture_h > y1) {
                y1 = cursor_capture_y + cursor_capture_h;
            }
            pointer_w = x1 - pointer_x;
            pointer_h = y1 - pointer_y;
        }
    }

    present_changed_regions(clock_x, clock_y, CLOCK_BOX_W, CLOCK_BOX_H,
                            pointer_x, pointer_y, pointer_w, pointer_h);
}

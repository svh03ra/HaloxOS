// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: widgets.c, common UI widget rendering.

// This repository is licensed under the GNU General Public License.

/*
 * Pointer-reactive widget bookkeeping - see HOVER_RECT_MAX in kernel.c.
 * Cleared at the start of every full desktop render and filled in by the
 * painters below, so the list always describes what is on screen. The
 * pointer-only repaint path refuses any move that enters or leaves one of
 * these rectangles.
 */
static void hover_widget_reset(void) {
    hover_rect_count = 0;
}

static void hover_widget_add(int x, int y, int w, int h) {
    if (hover_rect_count >= HOVER_RECT_MAX) {
        return;
    }
    hover_rect_x[hover_rect_count] = x;
    hover_rect_y[hover_rect_count] = y;
    hover_rect_w[hover_rect_count] = w;
    hover_rect_h[hover_rect_count] = h;
    ++hover_rect_count;
}

static bool hover_widget_hit(int x, int y) {
    for (int i = 0; i < hover_rect_count; ++i) {
        if (point_in_rect(x, y, hover_rect_x[i], hover_rect_y[i],
                          hover_rect_w[i], hover_rect_h[i])) {
            return true;
        }
    }
    return false;
}

static void draw_button(int x, int y, int w, int h, const char *label, uint8_t fill, uint8_t border, uint8_t text) {
    bool hover = ui_window_interactive && point_in_rect(mouse.x, mouse.y, x, y, w, h);
    bool pressed = hover && mouse.left;
    hover_widget_add(x, y, w, h);
    uint32_t hover_id = ((uint32_t)(x & 0x3FF) << 20) ^ ((uint32_t)(y & 0x3FF) << 10) ^ (uint32_t)((w & 0x1F) << 5) ^ (uint32_t)(h & 0x1F);
    uint8_t draw_fill = fill;
    uint8_t hover_fill = fill == color_green ? color_green_dark :
                         (fill == color_blue ? color_blue_dark :
                         (fill == color_white ? color_gray_light : color_gray));

    if (hover) {
        cursor_hand_hint = true;
        if (button_hover_id != hover_id) {
            button_hover_id = hover_id;
            button_hover_tick = timer_ticks;
        }
    }

    if (pressed) {
        draw_fill = color_gray_dark;
    }

    if (!pressed && hover && fill != color_gray) {
        draw_hover_fade_rect(x, y, w, h, fill, hover_fill, button_hover_tick);
        draw_fill = (timer_ticks - button_hover_tick) >= 9u ? hover_fill : fill;
    } else {
        fill_rect(x, y, w, h, draw_fill);
    }
    draw_rect(x, y, w, h, border);
    if (!pressed) {
        draw_pixel(x + w - 2, y + 1, color_white);
        draw_pixel(x + 1, y + h - 2, color_black);
    }
    draw_text_center(x + w / 2 + (pressed ? 1 : 0), y + (h - 8) / 2 + (pressed ? 1 : 0), label, text, draw_fill, true);
}

static void draw_hover_fade_rect(int x, int y, int w, int h, uint8_t base, uint8_t dark, uint32_t hover_tick) {
    uint32_t age = timer_ticks - hover_tick;
    int density = (int)(age / 3u) + 1;

    if (density >= 4) {
        fill_rect(x, y, w, h, dark);
        return;
    }

    /* The fade is still animating (partial dither). Tell the redraw gate
     * to keep scheduling full repaints: the pointer may be perfectly
     * still, and without this the animation would freeze on whatever
     * density the first hover frame happened to paint. */
    ui_hover_fade_active = true;

    fill_rect(x, y, w, h, base);
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            if (((px + py) & 3) < density) {
                draw_pixel(px, py, dark);
            }
        }
    }
}

static void draw_interactive_row(int x, int y, int w, int h,
                                 const char *label,
                                 uint8_t text,
                                 bool hover,
                                 bool pressed,
                                 uint32_t hover_tick) {
    uint8_t bg = color_gray_light;
    int text_dx = pressed ? 1 : 0;
    int text_dy = pressed ? 1 : 0;

    hover_widget_add(x, y, w, h);

    /* A row inside a covered window must not react: see
     * ui_window_interactive in kernel.c. */
    if (!ui_window_interactive) {
        hover = false;
        pressed = false;
    }

    if (hover) {
        cursor_hand_hint = true;
    }

    if (pressed) {
        fill_rect(x + 1, y + 1, w, h, color_black);
        fill_rect(x, y, w, h, color_gray_dark);
        draw_rect(x, y, w, h, color_black);
    } else if (hover) {
        draw_hover_fade_rect(x, y, w, h, color_gray_light, color_gray, hover_tick);
        draw_rect(x, y, w, h, color_gray_dark);
        bg = (timer_ticks - hover_tick) >= 9u ? color_gray : color_gray_light;
    } else {
        fill_rect(x, y, w, h, color_gray_light);
    }

    draw_text(x + 8 + text_dx, y + (h - 8) / 2 + text_dy, label, text, bg, true);
}

static bool button_clicked(int x, int y, int w, int h) {
    return point_in_rect(mouse.x, mouse.y, x, y, w, h) && mouse.left && !mouse.prev_left;
}

static bool button_right_clicked(int x, int y, int w, int h) {
    return point_in_rect(mouse.x, mouse.y, x, y, w, h) && mouse.right && !mouse.prev_right;
}

/*
 * Topmost window under a point, or 0 when the point is on the desktop.
 *
 * The scan below repeats render_desktop's paint order exactly, because the
 * window that paints last owns those pixels and therefore owns the mouse:
 * test windows paint after the app windows, and inside each group the
 * active window paints last of all. Every consumer - the click router, the
 * close-button hint and the per-window hover gating - goes through this one
 * function, so hit testing and drawing can never disagree about which
 * window is "in front".
 *
 * That disagreement was the root cause of three symptoms: a click landing
 * on a window that was painted over (the click used to reach the covered
 * window as well), the hand cursor appearing while hovering a window's own
 * body (a hidden button of the window underneath matched the coordinates),
 * and covered buttons showing a press fill.
 */
static const Window *desktop_top_window_at(int x, int y) {
    int first = active_test_window;

    for (int pass = 0; pass <= test_window_count; ++pass) {
        int i = pass == 0 ? first : (test_window_count - pass);

        if (i < 0 || i >= test_window_count) continue;
        if (pass > 0 && i == first) continue;
        if (!test_windows[i].open) continue;
        if (point_in_rect(x, y, test_windows[i].x, test_windows[i].y,
                          test_windows[i].w, test_windows[i].h)) {
            return &test_windows[i];
        }
    }

    first = active_window;
    for (int pass = 0; pass <= APP_COUNT; ++pass) {
        int app = pass == 0 ? first : (APP_COUNT - pass);

        if (app < 0 || app >= APP_COUNT) continue;
        if (pass > 0 && app == first) continue;
        if (!windows[app].open) continue;
        if (point_in_rect(x, y, windows[app].x, windows[app].y,
                          windows[app].w, windows[app].h)) {
            return &windows[app];
        }
    }

    return 0;
}

/* Is this exact window the one the pointer is over (and therefore the one
 * that owns hover state and clicks)? */
static bool desktop_window_owns_pointer(const Window *window) {
    return desktop_top_window_at(mouse.x, mouse.y) == window;
}

/* The app whose window owns the pointer, or APP_COUNT when the pointer is
 * on the desktop / over a test window. */
static AppId desktop_pointer_owner_app(void) {
    const Window *window = desktop_top_window_at(mouse.x, mouse.y);

    if (window == 0 || window < windows || window >= windows + APP_COUNT) {
        return APP_COUNT;
    }
    return (AppId)(window - windows);
}

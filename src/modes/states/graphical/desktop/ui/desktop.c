// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: desktop.c, desktop background and layout.

// This repository is licensed under the GNU General Public License.

/*
 * The debugger overlay covers nearly the whole screen.  Once it has been
 * painted over a complete desktop frame, a redraw that only changes the
 * overlay (a log line, the bitmap scroll, the terminal cursor blink) can
 * repaint the overlay in place instead of recompositing the wallpaper,
 * every window and the taskbar underneath it.  The cached backdrop stays
 * valid until the overlay closes; the first frame after it opens is
 * always a full render, so the pixels outside the overlay are real.
 */
static bool debug_overlay_backdrop_ready = false;
static bool debug_overlay_was_open = false;
/* The overlay is not one fixed rectangle: the terminal log covers the top
 * half, the memory viewer nearly the whole screen.  Repainting only the
 * new rectangle after a switch left the old bitmap visible below the log
 * (and left the old FPS box behind when the overlay was hidden).  These
 * remember the shape the cached backdrop was built for, so a shape change
 * takes the full-render path once. */
static bool debug_overlay_was_memory_view = false;
static bool debug_overlay_was_fps = false;
/* A video-mode change clears the shadow planes and the scan-out, so the
 * cached backdrop is gone even though the overlay shape did not change. */
static int debug_overlay_was_fb_width = 0;
static int debug_overlay_was_fb_height = 0;
static int debug_overlay_was_fb_bpp = 0;
static int debug_overlay_was_canvas_w = 0;
static int debug_overlay_was_canvas_h = 0;

static void render_desktop(void) {
    if (debug_overlay_open && debug_overlay_was_open && debug_overlay_backdrop_ready &&
        debug_overlay_was_memory_view == debug_memory_view_open &&
        debug_overlay_was_fps == fps_overlay_on &&
        debug_overlay_was_fb_width == fb.width &&
        debug_overlay_was_fb_height == fb.height &&
        debug_overlay_was_fb_bpp == fb.bpp &&
        debug_overlay_was_canvas_w == OS_WIDTH &&
        debug_overlay_was_canvas_h == OS_HEIGHT) {
        cursor_hand_hint = false;
        /* The overlay does not cover the whole screen, and the pointer is
         * free to sit outside it.  Put the save-under pixels back FIRST;
         * the old code discarded the snapshot after repainting the overlay,
         * so a pointer outside the box left its previous image behind on
         * every move.  Restoring before the overlay also handles a pointer
         * that WAS over the box: the overlay repaint covers those pixels
         * again, and render_cursor() takes a fresh snapshot. */
        cursor_capture_restore();
        render_debug_overlay();
        if (debug_memory_pointer_visible()) {
            render_cursor();
        } else {
            cursor_book_not_painted();
        }
        return;
    }

    cursor_hand_hint = false;
    /* Fresh assumption for this frame's widgets; render_app_window narrows
     * it to the window that actually owns the pointer (see widgets.c). */
    ui_window_interactive = true;
    /* Rebuild the pointer-reactive rectangle list for this frame (see
     * HOVER_RECT_MAX in kernel.c). */
    hover_widget_reset();
    draw_desktop_background();
    /* The overlay belongs to the wallpaper layer, not to the icons: drawn
     * after them, its dots speckled icon art and labels. Windows still
     * paint over it, so the lattice only shows on bare desktop. */
    draw_desktop_grid_overlay();
    render_desktop_icons();
    if (desktop_select_dragging) {
        int rx = desktop_select_x1 < desktop_select_x2 ? desktop_select_x1 : desktop_select_x2;
        int ry = desktop_select_y1 < desktop_select_y2 ? desktop_select_y1 : desktop_select_y2;
        int rw = (desktop_select_x1 > desktop_select_x2 ? desktop_select_x1 : desktop_select_x2) - rx;
        int rh = (desktop_select_y1 > desktop_select_y2 ? desktop_select_y1 : desktop_select_y2) - ry;
        if (rw > 0 && rh > 0) {
            /* Two interlaced outlines, 1px apart: the black stays visible
             * on white backgrounds and the white on black/blue ones, so
             * the band can never blend into the wallpaper (it was plain
             * white before, invisible on the Solid White theme). */
            draw_rect(rx - 1, ry - 1, rw + 2, rh + 2, color_black);
            draw_rect(rx, ry, rw, rh, color_white);
        }
    }
    render_taskbar();
    /* Anchored to the canvas corner, not to 640x480 (see the canvas note
     * in kernel.c): at the base canvas these are the original 438/430,
     * 438/440, 480/430 and 480/440 positions. */
#if HALOXOS_CONFIG_DEV_MODE
    draw_text(OS_WIDTH - 202, OS_HEIGHT - 50, "HaloxOS Version 1.1F", color_black, color_gray_light, true);
    draw_text(OS_WIDTH - 202, OS_HEIGHT - 40, "For Testing purposes only", color_black, color_gray_light, true);
#else
    draw_text(OS_WIDTH - 160, OS_HEIGHT - 50, "HaloxOS Version 1.1", color_black, color_gray_light, true);
    draw_text(OS_WIDTH - 160, OS_HEIGHT - 40, "Stable Copy.", color_black, color_gray_light, true);
#endif
    if (settings_applied.window_trails) {
        for (int i = 0; i < TRAIL_COUNT; ++i) {
            uint32_t age = timer_ticks - trail_tick[i];
            if (age < 6 && trail_w[i] > 0) {
                int density = 4 - (int)age;
                if (density < 0) density = 0;
                uint8_t col = age < 2 ? color_gray : color_gray_dark;
                if (age < 1) {
                    fill_rect(trail_x[i], trail_y[i], trail_w[i], trail_h[i], col);
                } else {
                    for (int py = trail_y[i]; py < trail_y[i] + trail_h[i]; ++py) {
                        for (int px = trail_x[i]; px < trail_x[i] + trail_w[i]; ++px) {
                            if (ui_checker_dot(px, py, density)) {
                                draw_pixel(px, py, col);
                            }
                        }
                    }
                }
            }
        }
    }
    for (int app = 0; app < APP_COUNT; ++app) {
        if (app == active_window) {
            continue;
        }
        render_app_window((AppId)app);
    }
    if (active_window >= 0 && active_window < APP_COUNT) {
        render_app_window((AppId)active_window);
    }
    for (int i = 0; i < test_window_count; ++i) {
        if (i == active_test_window || !test_windows[i].open) continue;
        /* Same pointer-ownership rule as the app windows: only the topmost
         * test window under the pointer shows hover/press state. */
        ui_window_interactive = desktop_window_owns_pointer(&test_windows[i]);
        draw_window_chrome(&test_windows[i]);
        draw_text_center(test_windows[i].x + test_windows[i].w / 2, test_windows[i].y + test_windows[i].h / 2 - 4, "This is a test!", color_black, color_gray_light, true);
    }
    if (active_test_window >= 0 && active_test_window < test_window_count && test_windows[active_test_window].open) {
        ui_window_interactive = desktop_window_owns_pointer(&test_windows[active_test_window]);
        draw_window_drop_shadow(&test_windows[active_test_window]);
        draw_window_chrome(&test_windows[active_test_window]);
        draw_text_center(test_windows[active_test_window].x + test_windows[active_test_window].w / 2, test_windows[active_test_window].y + test_windows[active_test_window].h / 2 - 4, "This is a test!", color_black, color_gray_light, true);
    }
    ui_window_interactive = true;
    render_power_overlay();
    render_start_menu();
    render_context_menu();
    render_desktop_icon_menu();
    render_start_app_menu();
    render_taskbar_menu();

    if (cpu_halted_overlay) {
        fill_rect(OS_WIDTH / 2 - 160, OS_HEIGHT / 2 - 60, 320, 80, color_gray_light);
        draw_rect(OS_WIDTH / 2 - 160, OS_HEIGHT / 2 - 60, 320, 80, color_black);
        draw_text_center(OS_WIDTH / 2, OS_HEIGHT / 2 - 25, "Halting CPU... *boom* DONE!", color_black, color_gray_light, true);
    }

    if (window_fade_active) {
        uint32_t age = timer_ticks - window_fade_tick;
        if (age < 18) {
            int threshold = ((int)age * 3) / 17 + 1;
            for (int py = window_fade_y; py < window_fade_y + window_fade_h; ++py) {
                for (int px = window_fade_x; px < window_fade_x + window_fade_w; ++px) {
                    if (ui_checker_dot(px, py, threshold)) {
                        draw_pixel(px, py, color_black);
                    }
                }
            }
        }
    }

    render_debug_overlay();
    if (!debug_overlay_open || debug_memory_pointer_visible()) {
        /* The pointer is stamped over the memory-viewer box; the terminal
         * overlay and the wallpaper strip around the box hide it. */
        render_cursor();
    } else {
        /* No cursor is stamped there; the full frame just painted over any
         * old stamp, so retire the save-under too.  The redraw gate still
         * compares the live pointer with the painted one - book the live
         * position so an idle overlay does not force full repaints. */
        cursor_book_not_painted();
        cursor_invalidate_capture();
    }

    debug_overlay_backdrop_ready = debug_overlay_open;
    debug_overlay_was_open = debug_overlay_open;
    debug_overlay_was_memory_view = debug_memory_view_open;
    debug_overlay_was_fps = fps_overlay_on;
    debug_overlay_was_fb_width = fb.width;
    debug_overlay_was_fb_height = fb.height;
    debug_overlay_was_fb_bpp = fb.bpp;
    debug_overlay_was_canvas_w = OS_WIDTH;
    debug_overlay_was_canvas_h = OS_HEIGHT;
}

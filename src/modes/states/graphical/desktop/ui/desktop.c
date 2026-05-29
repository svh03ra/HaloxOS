static void render_desktop(void) {
    cursor_hand_hint = false;
    draw_desktop_background();
    render_desktop_icons();
    if (desktop_select_dragging) {
        int rx = desktop_select_x1 < desktop_select_x2 ? desktop_select_x1 : desktop_select_x2;
        int ry = desktop_select_y1 < desktop_select_y2 ? desktop_select_y1 : desktop_select_y2;
        int rw = (desktop_select_x1 > desktop_select_x2 ? desktop_select_x1 : desktop_select_x2) - rx;
        int rh = (desktop_select_y1 > desktop_select_y2 ? desktop_select_y1 : desktop_select_y2) - ry;
        if (rw > 0 && rh > 0) {
            draw_rect(rx, ry, rw, rh, color_white);
        }
    }
    render_taskbar();
    draw_text(438, 430, "HaloxOS Version 1.0D", color_black, color_gray_light, true);
    draw_text(438, 440, "For Testing purposes only", color_black, color_gray_light, true);
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
                            if (((px + py) & 3) < density) {
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
        draw_window_chrome(&test_windows[i]);
        draw_text_center(test_windows[i].x + test_windows[i].w / 2, test_windows[i].y + test_windows[i].h / 2 - 4, "This is a test!", color_black, color_gray_light, true);
    }
    if (active_test_window >= 0 && active_test_window < test_window_count && test_windows[active_test_window].open) {
        draw_window_chrome(&test_windows[active_test_window]);
        draw_text_center(test_windows[active_test_window].x + test_windows[active_test_window].w / 2, test_windows[active_test_window].y + test_windows[active_test_window].h / 2 - 4, "This is a test!", color_black, color_gray_light, true);
    }
    render_power_overlay();
    render_start_menu();
    render_context_menu();
    render_desktop_icon_menu();
    render_start_app_menu();
    render_taskbar_menu();

    if (cpu_halted_overlay) {
        fill_rect(160, 180, 320, 80, color_gray_light);
        draw_rect(160, 180, 320, 80, color_black);
        draw_text_center(OS_WIDTH / 2, 215, "Halting CPU... *boom* DONE!", color_black, color_gray_light, true);
    }

    if (window_fade_active) {
        uint32_t age = timer_ticks - window_fade_tick;
        if (age < 18) {
            int threshold = ((int)age * 3) / 17 + 1;
            for (int py = window_fade_y; py < window_fade_y + window_fade_h; ++py) {
                for (int px = window_fade_x; px < window_fade_x + window_fade_w; ++px) {
                    if (((px + py) & 3) < threshold) {
                        draw_pixel(px, py, color_black);
                    }
                }
            }
        }
    }

    render_debug_overlay();
    if (!debug_overlay_open) {
        render_cursor();
    }
}

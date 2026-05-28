static void render_desktop(void) {
    cursor_hand_hint = false;
    draw_desktop_background();
    render_desktop_icons();
    render_taskbar();
    for (int app = 0; app < APP_COUNT; ++app) {
        if (app == active_window) {
            continue;
        }
        render_app_window((AppId)app);
    }
    if (active_window >= 0 && active_window < APP_COUNT) {
        render_app_window((AppId)active_window);
    }
    render_power_overlay();
    render_start_menu();
    render_context_menu();
    render_desktop_icon_menu();
    render_start_app_menu();

    if (cpu_halted_overlay) {
        fill_rect(160, 180, 320, 80, color_gray_light);
        draw_rect(160, 180, 320, 80, color_black);
        draw_text_center(OS_WIDTH / 2, 215, "Halting CPU... *boom* DONE!", color_black, color_gray_light, true);
    }

    render_debug_overlay();
    if (!debug_overlay_open) {
        render_cursor();
    }
    draw_text(438, 430,  "HaloxOS Version 1.0D", color_black, color_gray_light, true);
    draw_text(438, 440,  "For Testing purposes only",             color_black, color_gray_light, true);
}

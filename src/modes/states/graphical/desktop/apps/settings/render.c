static void render_settings(const Window *window) {
    char resolution[24] = {0};
    char current_mode[24] = {0};
    int icon_w = image_width(_binary_build_settings_icon_bin_start);
    int header_text_w = text_pixel_width("Settings");
    int header_x = window->x + (window->w - (icon_w + 8 + header_text_w)) / 2;
    bool dirty = settings_dirty();
    resolution_string(resolution, sizeof(resolution), &settings_pending);
    framebuffer_mode_string(current_mode, sizeof(current_mode), fb.width, fb.height, fb.bpp);

    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_white);
    draw_image_at(_binary_build_settings_icon_bin_start, header_x, window->y + 24, true);
    draw_text(header_x + icon_w + 8, window->y + 36, "Settings", color_black, color_white, true);
    draw_button(window->x + 14, window->y + 56, 70, 20, "Video", settings_tab == 0 ? color_blue : color_gray_light, color_black, settings_tab == 0 ? color_white : color_black);
    draw_button(window->x + 90, window->y + 56, 80, 20, "Desktop", settings_tab == 1 ? color_blue : color_gray_light, color_black, settings_tab == 1 ? color_white : color_black);
    draw_button(window->x + 176, window->y + 56, 60, 20, "Fun", settings_tab == 2 ? color_blue : color_gray_light, color_black, settings_tab == 2 ? color_white : color_black);

    if (settings_tab == 0) {
        draw_text(window->x + 20, window->y + 92, "Set to Color Modes:", color_black, color_white, true);
        draw_button(window->x + 178, window->y + 86, 20, 18, "<", live_palette_supported(settings_pending.palette_mode) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 202, window->y + 86, 140, 18, palette_name(settings_pending.palette_mode), live_palette_supported(settings_pending.palette_mode) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 346, window->y + 86, 20, 18, ">", live_palette_supported(settings_pending.palette_mode) ? color_gray_light : color_gray, color_black, color_black);

        draw_text(window->x + 20, window->y + 122, "Screen Resolution:", color_black, color_white, true);
        draw_button(window->x + 178, window->y + 116, 20, 18, "<", live_resolution_supported(settings_pending.resolution_mode) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 202, window->y + 116, 140, 18, resolution_name(settings_pending.resolution_mode), live_resolution_supported(settings_pending.resolution_mode) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 346, window->y + 116, 20, 18, ">", live_resolution_supported(settings_pending.resolution_mode) ? color_gray_light : color_gray, color_black, color_black);

        fill_rect(window->x + 20, window->y + 148, 12, 12, settings_pending.widescreen ? color_green : color_white);
        draw_rect(window->x + 20, window->y + 148, 12, 12, color_black);
        draw_text(window->x + 40, window->y + 150, "Set to Widescreen", color_black, color_white, true);
        draw_text(window->x + 20, window->y + 174, "Total Screen:", color_black, color_white, true);
        draw_text(window->x + 128, window->y + 174, resolution, color_blue_dark, color_white, true);
        draw_text(window->x + 20, window->y + 192, "Current Output:", color_black, color_white, true);
        draw_text(window->x + 143, window->y + 192, current_mode, color_blue_dark, color_white, true);
        draw_text(window->x + 20, window->y + 206, video_mode_switch_available ? "Apply switches the video mode." : "ERROR: video mode switch is unavailable!", color_black, color_white, true);
    } else if (settings_tab == 1) {
        draw_text(window->x + 20, window->y + 92, "Desktop Background", color_black, color_white, true);
        draw_button(window->x + 170, window->y + 86, 20, 18, "<", color_gray_light, color_black, color_black);
        draw_button(window->x + 194, window->y + 86, 160, 18, background_name(settings_pending.background_mode), color_gray_light, color_black, color_black);
        draw_button(window->x + 358, window->y + 86, 20, 18, ">", color_gray_light, color_black, color_black);
        draw_text(window->x + 20, window->y + 120, "Select one to want you'd like:", color_blue_dark, color_white, true);
        draw_text(window->x + 20, window->y + 130, "Apply immediately after OK.", color_black, color_white, true);
    } else {
        draw_text(window->x + 20, window->y + 88, "Fun:", color_black, color_white, true);
        fill_rect(window->x + 20, window->y + 102, 12, 12, settings_pending.window_fade ? color_green : color_white);
        draw_rect(window->x + 20, window->y + 102, 12, 12, color_black);
        draw_text(window->x + 40, window->y + 104, "Faded-Closed window transition:", color_blue_dark, color_white, true);
        draw_text(window->x + 20, window->y + 119, "Animate the window when closing it.", color_black, color_white, true);
        fill_rect(window->x + 20, window->y + 146, 12, 12, settings_pending.window_trails ? color_green : color_white);
        draw_rect(window->x + 20, window->y + 146, 12, 12, color_black);
        draw_text(window->x + 40, window->y + 148, "Window Trails!:", color_blue_dark, color_white, true);
        draw_text(window->x + 20, window->y + 162, "Leaves an after-image while dragging.", color_black, color_white, true);
    }

    draw_button(window->x + 120, window->y + window->h - 36, 60, 22, "Cancel", dirty ? color_gray_light : color_gray, color_black, color_black);
    draw_button(window->x + 188, window->y + window->h - 36, 60, 22, "Apply", dirty ? color_gray_light : color_gray, color_black, color_black);
    draw_button(window->x + 256, window->y + window->h - 36, 60, 22, "OK", dirty ? color_gray_light : color_gray, color_black, color_black);
}

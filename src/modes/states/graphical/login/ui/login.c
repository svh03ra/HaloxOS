static void render_login(void) {
    const char *line1 = "Welcome! Enter to your login access:";
    const char *line2 = "Press ENTER to start";
    const char *line3 = "Press ESC to shutdown";
    int icon_w = image_width(_binary_build_user_frame_bin_start);
    int text_w = text_pixel_width(line1);
    int text_x;
    int icon_x;

    if (text_pixel_width(line2) > text_w) {
        text_w = text_pixel_width(line2);
    }
    if (text_pixel_width(line3) > text_w) {
        text_w = text_pixel_width(line3);
    }

    icon_x = (OS_WIDTH - (icon_w + 16 + text_w)) / 2;
    text_x = icon_x + icon_w + 16;

    draw_image(_binary_build_login_bin_start);
    if (debug) {
        draw_text(8, 8, "DEBUG MODE!", color_red, color_gray_light, true);
    }
    draw_image_at(_binary_build_user_frame_bin_start, icon_x, 182, true);
    draw_text(text_x, 190, line1, color_black, 0, true);
    draw_text(text_x, 216, line2, color_black, 0, true);
    draw_text(text_x, 227, line3, color_black, 0, true);
    draw_text(505, 10, "Version 1.0:", color_black, color_gray_light, true);
    draw_text(505, 20, HALOXOS_BUILD_TEXT, color_black, color_gray_light, true);
    draw_text_center(OS_WIDTH / 2, 460, "(C) 2026 Svh03ra, Final Release.", color_black, color_gray_light, true);
    draw_text_center(OS_WIDTH / 2, 450, "Available here! https://github.com/svh03ra/HaloxOS", color_black, color_gray_light, true);
}

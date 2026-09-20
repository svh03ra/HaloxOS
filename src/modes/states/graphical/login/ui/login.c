// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: login.c, login screen rendering.

// This repository is licensed under the GNU General Public License.

#if HALOXOS_CONFIG_DEV_MODE
#define HALOXOS_LOGIN_MTXT "For Development Purposes only!"
#else
#define HALOXOS_LOGIN_MTXT "Final Release."
#endif

static void render_login(void) {
    const char *line1 = "Welcome! Enter to your login access:";
    const char *line2 = "Press ENTER to start";
    const char *line3 = "Press ESC to shutdown";
    int icon_w = image_width(_binary_build_user_frame_bin_start);
    int text_w = text_pixel_width(line1);
    int text_x;
    int icon_x;
    /* The canvas grows with the output mode, so the login block is placed
     * relative to it: the same 56px-tall block sits slightly above the
     * middle and the footer hugs the bottom, at any resolution. At the
     * 640x480 base these are exactly the coordinates the screen always
     * used (182/190/216/227, 450/460, 505). */
    int block_y = OS_HEIGHT / 2 - 52;
    int version_x = OS_WIDTH - 135;

    if (text_pixel_width(line2) > text_w) {
        text_w = text_pixel_width(line2);
    }
    if (text_pixel_width(line3) > text_w) {
        text_w = text_pixel_width(line3);
    }

    icon_x = (OS_WIDTH - (icon_w + 16 + text_w)) / 2;
    text_x = icon_x + icon_w + 16;

    /* The login backdrop follows the wallpaper fit the user chose in the
     * Desktop tab (Stretch by default), so the login screen and the desktop
     * show the same treatment of the same art. */
    draw_wallpaper(_binary_build_login_bin_start);
    if (debug) {
        draw_text(8, 8, "DEBUG MODE!", color_red, color_gray_light, true);
    }
    draw_image_at(_binary_build_user_frame_bin_start, icon_x, block_y - 6, true);
    draw_text(text_x, block_y + 2, line1, color_black, 0, true);
    draw_text(text_x, block_y + 28, line2, color_black, 0, true);
    draw_text(text_x, block_y + 39, line3, color_black, 0, true);
    draw_text(version_x, 10, "Version 1.1:", color_black, color_gray_light, true);
    draw_text(version_x, 20, HALOXOS_BUILD_TEXT, color_black, color_gray_light, true);
    draw_text_center(OS_WIDTH / 2, OS_HEIGHT - 20, "(C) 2026 Svh03ra, " HALOXOS_LOGIN_MTXT, color_black, color_gray_light, true);
    draw_text_center(OS_WIDTH / 2, OS_HEIGHT - 30, "Available here! https://github.com/svh03ra/HaloxOS", color_black, color_gray_light, true);
}

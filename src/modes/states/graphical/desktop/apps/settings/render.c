// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, settings rendering.

// This repository is licensed under the GNU General Public License.

static void render_settings(const Window *window) {
    char resolution[24] = {0};
    char current_mode[24] = {0};
    char mode_list[48] = {0};
    bool widescreen_row = resolution_widescreen_applicable();
    int icon_w = image_width(_binary_build_settings_icon_bin_start);
    int header_text_w = text_pixel_width("Settings");
    int header_x = window->x + (window->w - (icon_w + 8 + header_text_w)) / 2;
    bool dirty = settings_dirty();
    resolution_string(resolution, sizeof(resolution), &settings_pending);
    /* The two lines answer two different questions now that the desktop can be
     * re-sized inside a mode it does not fill: "Desktop Size" is what the
     * arrows select, "Current Output" is the mode the card is really
     * presenting right now (read back from fb, so a card that rounded the
     * request - BGA turns 1366 into 1360 - reports the real number). */
    framebuffer_mode_string(current_mode, sizeof(current_mode),
                            fb.width, fb.height, fb.bpp);

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
        /* An arrow is usable when there IS an applicable entry that way, not
         * when the current one happens to be applicable - that is what the
         * click does (resolution_step skips entries this backend cannot
         * apply), so the colour now describes the button's own effect. */
        draw_button(window->x + 178, window->y + 116, 20, 18, "<", resolution_step_available(-1) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 202, window->y + 116, 140, 18, settings_pending.widescreen ? resolution_name_wide(settings_pending.resolution_mode) : resolution_name(settings_pending.resolution_mode), pending_resolution_supported(settings_pending.resolution_mode) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 346, window->y + 116, 20, 18, ">", resolution_step_available(1) ? color_gray_light : color_gray, color_black, color_black);

        /* The checkbox is grey on a machine whose row is one list of sizes:
         * there is no second column for it to switch to, so it is shown as
         * the control it is - unavailable here - rather than left looking
         * clickable while doing nothing. */
        fill_rect(window->x + 20, window->y + 148, 12, 12,
                  widescreen_row ? (settings_pending.widescreen ? color_green : color_white)
                                 : color_gray_light);
        draw_rect(window->x + 20, window->y + 148, 12, 12, color_black);
        draw_text(window->x + 40, window->y + 150, "Set to Widescreen",
                  widescreen_row ? color_black : color_gray, color_white, true);
        draw_text(window->x + 20, window->y + 174, "Desktop Size:", color_black, color_white, true);
        draw_text(window->x + 128, window->y + 174, resolution, color_blue_dark, color_white, true);
        draw_text(window->x + 20, window->y + 192, "Current Output:", color_black, color_white, true);
        draw_text(window->x + 143, window->y + 192, current_mode, color_blue_dark, color_white, true);
        if (video_backend == VIDEO_BACKEND_VGA) {
            draw_text(window->x + 20, window->y + 206, "VGA: Default 320x240, Low 640x480.", color_black, color_white, true);
        } else if (video_backend == VIDEO_BACKEND_MULTIBOOT) {
            /* Where the sizes in the row above come from, and how many there
             * are: the firmware's own list, or the reason this machine has
             * none.  One line, one value - the row above it is the setting. */
            size_t len = 0;

            append_string(mode_list, &len, sizeof(mode_list), "Video BIOS: ");
            video_mode_list_string(mode_list + len, sizeof(mode_list) - len);
            draw_text(window->x + 20, window->y + 206, mode_list, color_black, color_white, true);
        } else {
            draw_text(window->x + 20, window->y + 206, video_mode_switch_available ? "Apply switches the video mode." : "ERROR: video mode switch is unavailable!", color_black, color_white, true);
        }
    } else if (settings_tab == 1) {
        draw_text(window->x + 20, window->y + 92, "Desktop Background", color_black, color_white, true);
        draw_button(window->x + 170, window->y + 86, 20, 18, "<", color_gray_light, color_black, color_black);
        draw_button(window->x + 194, window->y + 86, 160, 18, background_name(settings_pending.background_mode), color_gray_light, color_black, color_black);
        draw_button(window->x + 358, window->y + 86, 20, 18, ">", color_gray_light, color_black, color_black);

        /* Wallpaper fit: same arrow shape as the Video tab's rows so the two
         * tabs read the same way. The row names the setting; it does not
         * explain it. */
        draw_text(window->x + 20, window->y + 122, "Wallpaper Fit:", color_black, color_white, true);
        draw_button(window->x + 178, window->y + 116, 20, 18, "<", color_gray_light, color_black, color_black);
        draw_button(window->x + 202, window->y + 116, 140, 18, wallpaper_fit_name(settings_pending.wallpaper_fit), color_gray_light, color_black, color_black);
        draw_button(window->x + 346, window->y + 116, 20, 18, ">", color_gray_light, color_black, color_black);
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

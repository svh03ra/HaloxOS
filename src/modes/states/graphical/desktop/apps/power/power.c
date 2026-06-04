// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: power.c, power menu app.

// This repository is licensed under the GNU General Public License.

static void render_power(const Window *window) {
    int x = window->x + 20;
    int y = window->y + 36;
    draw_button(x, y, 160, 24, "Shutdown", color_gray_light, color_black, color_black);
    draw_button(x, y + 32, 160, 24, "Restart", color_gray_light, color_black, color_black);
    draw_button(x, y + 64, 160, 24, "Halt", color_gray_light, color_black, color_black);
    draw_button(x, y + 96, 160, 24, "x Close", color_gray_light, color_black, color_black);
}

static void render_power_overlay(void) {
    int x = 212;
    int y = 150;
    if (!power_menu_open) {
        return;
    }
    fill_rect(x, y, 216, 158, color_gray_light);
    draw_rect(x, y, 216, 158, color_black);
    draw_text_center(x + 108, y + 12, "Power Options", color_blue_dark, color_gray_light, true);
    draw_button(x + 28, y + 34, 160, 24, "Shutdown", color_gray_light, color_black, color_black);
    draw_button(x + 28, y + 66, 160, 24, "Restart", color_gray_light, color_black, color_black);
    draw_button(x + 28, y + 98, 160, 24, "Halt", color_gray_light, color_black, color_black);
    draw_button(x + 28, y + 126, 160, 20, "Close", color_gray_light, color_black, color_black);
}

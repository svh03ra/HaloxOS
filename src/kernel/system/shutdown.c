// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: shutdown.c, shutdown graphical logic.

// This repository is licensed under the GNU General Public License.

static void render_shutdown(void) {
    const char *message = shutdown_poweroff_failed ? "Power off failed. Hold power button." : "Shutting down...";
    if (boot_text_mode) {
        vga_text_clear(VGA_TEXT_ATTR_GRAY);
        vga_text_disable_cursor();
        draw_text_mode_center(12, message, VGA_TEXT_ATTR_GRAY);
    } else {
        clear_screen(color_black);
        draw_text_center_scaled(OS_WIDTH / 2, 212, message, color_white, color_black, true, 2);
    }
}

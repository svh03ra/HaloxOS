// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: shutdown.c, shutdown graphical logic.

// This repository is licensed under the GNU General Public License.

#ifndef HALOXOS_TEST_SHUTDOWN_FAILED
#define HALOXOS_TEST_SHUTDOWN_FAILED 0
#endif

static void render_shutdown(void) {
#if HALOXOS_TEST_SHUTDOWN_FAILED
    /* test hook: render the poweroff-failed screen directly */
    shutdown_poweroff_failed = true;
#endif
    if (shutdown_poweroff_failed) {
        if (boot_text_mode) {
            vga_text_clear(VGA_TEXT_ATTR_GRAY);
            vga_text_disable_cursor();
            /* 0x0E = yellow/orange on black text attribute */
            draw_text_mode_center(10, "It's now safe to turn off", 0x0E);
            draw_text_mode_center(11, "your computer", 0x0E);
            draw_text_mode_center(14, "Please hold the power button to shutdown when you're done.", VGA_TEXT_ATTR_GRAY);
        } else {
            clear_screen(color_black);
            /* Big orange two-line message, vertically centred. A 2x glyph
             * is 16px tall; a 24px step (16 + 8) reads as a normal text
             * line spacing instead of a split paragraph. */
            draw_text_center_scaled(OS_WIDTH / 2, 194, "It's now safe to turn off", color_orange, color_black, true, 2);
            draw_text_center_scaled(OS_WIDTH / 2, 218, "your computer", color_orange, color_black, true, 2);
            /* +2 gap lines below the big text */
            draw_text_center(OS_WIDTH / 2, 282, "Please hold the power button to shutdown when you're done.", color_gray_light, color_black, true);
        }
        return;
    }

    if (boot_text_mode) {
        vga_text_clear(VGA_TEXT_ATTR_GRAY);
        vga_text_disable_cursor();
        draw_text_mode_center(12, "Shutting down...", VGA_TEXT_ATTR_GRAY);
    } else {
        clear_screen(color_black);
        draw_text_center_scaled(OS_WIDTH / 2, 212, "Shutting down...", color_white, color_black, true, 2);
    }
}

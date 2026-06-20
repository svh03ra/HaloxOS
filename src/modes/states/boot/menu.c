// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: menu.c, main startup core.

// This repository is licensed under the GNU General Public License.

static void render_boot_menu(void) {
    if (!boot_menu_dirty) {
        return;
    }
    vga_text_clear(VGA_TEXT_ATTR_GRAY);
    vga_text_disable_cursor();
    draw_text_mode_center(8, "Hello World! Greetings HaloxOS!", VGA_TEXT_ATTR_BLUE);
    draw_text_mode_center(9, "Select an option to choose boot:", VGA_TEXT_ATTR_BLUE);
    draw_text_mode_center(12, "1) Main Boot", VGA_TEXT_ATTR_GRAY);
    draw_text_mode_center(13, "2) Command Prompt", VGA_TEXT_ATTR_GRAY);
    if (boot_status_text[0] != '\0') {
        draw_text_mode_center(21, boot_status_text, VGA_TEXT_ATTR_GRAY);
    }
    boot_menu_dirty = false;
}

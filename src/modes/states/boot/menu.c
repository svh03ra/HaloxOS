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
#if HALOXOS_CONFIG_DEBUG
    /* Debug bring-up panel: which firmware disks answer INT 13h, whether
     * any carries the validated log volume, and which USB controllers the
     * machine has.  On a real machine with no serial port this photograph
     * is the only way to tell a firmware refusal (AH code) from a bridge
     * fault (F vector) or a dead keyboard path.  Release builds keep the
     * menu clean: none of these diagnostics is shown there. */
    for (uint8_t line = 0; line < 6; ++line) {
        const char *text = serial_bootlog_diag_line(line);
        if (text[0] != '\0') {
            draw_text_mode_center(14 + line, text, VGA_TEXT_ATTR_GRAY);
        }
    }
    if (xhci_input_status_text()[0] != '\0') {
        draw_text_mode_center(20, xhci_input_status_text(), VGA_TEXT_ATTR_GRAY);
    }
    if (ps2_input_status_text()[0] != '\0') {
        draw_text_mode_center(21, ps2_input_status_text(), VGA_TEXT_ATTR_GRAY);
    }
    if (serial_bootlog_status_text()[0] != '\0') {
        draw_text_mode_center(22, serial_bootlog_status_text(), VGA_TEXT_ATTR_GRAY);
    }
#endif
    if (boot_status_text[0] != '\0') {
        draw_text_mode_center(23, boot_status_text, VGA_TEXT_ATTR_GRAY);
    }
    boot_menu_dirty = false;
}

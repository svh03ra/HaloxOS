// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, select to boot states in boot.

// This repository is licensed under the GNU General Public License.

static void handle_boot_menu_keys(KeyEvent event) {
    if (event.ch == '1') {
        serial_trace("INFO", "input: boot menu received 1 (Main Boot)");
        clear_boot_status();
        enter_main_graphics_mode();
        if (!boot_text_mode) {
            system_state = STATE_LOGIN;
        } else {
            set_boot_status("ERROR: Graphics mode unavailable!!!");
        }
    } else if (event.ch == '2') {
        serial_trace("INFO", "input: boot menu received 2 (Command Prompt)");
        clear_boot_status();
        enter_boot_text_mode();
        system_state = STATE_BOOT_TERMINAL;
        boot_terminal_dirty = true;
    }
}

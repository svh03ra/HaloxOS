// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render_dispatch.c, all of the render dispatches.

// This repository is licensed under the GNU General Public License.s

static void draw_everything(void) {
    switch (system_state) {
        case STATE_BOOT_MENU: render_boot_menu(); break;
        case STATE_BOOT_TERMINAL: render_boot_terminal_text(); break;
        case STATE_LOGIN: render_login(); break;
        case STATE_DESKTOP: render_desktop(); break;
        case STATE_SHUTDOWN: render_shutdown(); break;
    }
}

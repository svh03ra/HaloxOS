// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, login screen keyboard handling.

// This repository is licensed under the GNU General Public License.

static void handle_login_keys(KeyEvent event) {
    if (event.code == KEY_ENTER) {
        open_desktop();
    } else if (event.code == KEY_ESC) {
        shutdown_system();
    }
}

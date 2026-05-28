// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: state_update.c, state updates logic.

// This repository is licensed under the GNU General Public License.

static void update_state(void) {
    KeyEvent event;

    if (cpu_halted_overlay) {
        __asm__ volatile("cli");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    while (dequeue_key(&event)) {
        if (system_state == STATE_DESKTOP && keyboard_ctrl && keyboard_shift && event.code == KEY_ENTER) {
            debug_enter();
            continue;
        }
        if (debug_overlay_open) {
            debug_handle_key(event);
            continue;
        }
        if (system_state == STATE_DESKTOP && keyboard_ctrl && keyboard_shift && event.code == KEY_ESC) {
            open_window(APP_TASK_MANAGER);
            continue;
        }
        switch (system_state) {
            case STATE_BOOT_MENU:
                handle_boot_menu_keys(event);
                break;
            case STATE_BOOT_TERMINAL:
                terminal_handle_key(&boot_term, event, true);
                break;
            case STATE_LOGIN:
                handle_login_keys(event);
                break;
            case STATE_DESKTOP:
                if (event.code == KEY_F4 && keyboard_alt && active_window >= 0 && active_window < APP_COUNT) {
                    close_window((AppId)active_window);
                } else if (event.code == KEY_ESC && active_window == -1) {
                    menu_open = false;
                    context_menu_open = false;
                } else {
                    handle_text_target(event);
                }
                break;
            case STATE_SHUTDOWN:
                break;
        }
    }

    if (system_state == STATE_DESKTOP) {
        handle_desktop_mouse();
        update_snake();
    }
}

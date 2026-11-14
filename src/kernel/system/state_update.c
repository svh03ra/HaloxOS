// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: state_update.c, state updates logic.

// This repository is licensed under the GNU General Public License.

static void update_state(void) {
    KeyEvent event;

    if (cpu_halted_overlay) {
        serial_trace("INFO", "system halt requested");
        serial_trace_uint_value("INFO", "uptime ticks at halt", timer_ticks);
        serial_trace_hex_value("INFO", "8042 status register at halt", inb(0x64));
        serial_trace_hex_value("INFO", "PIC1 IMR at halt", inb(0x21));
        serial_trace_hex_value("INFO", "PIC2 IMR at halt", inb(0xA1));
        init_acpi_power();
        if (acpi_power.available) {
            serial_trace("INFO", "ACPI available at halt");
            serial_trace_hex_value("INFO", "halt PM1a event block", acpi_power.pm1a_event_block);
            serial_trace_hex_value("INFO", "halt PM1a control block", acpi_power.pm1a_control_block);
            serial_trace_hex_value("INFO", "halt PM1a control readback",
                                   inw((uint16_t)acpi_power.pm1a_control_block));
        } else {
            serial_trace("WARNING", "ACPI unavailable at halt");
        }
        serial_trace("INFO", "CPU halting (cli + hlt loop)");
        /* The trace survives the halt: flush the boot log before the
         * interrupts go off for good. */
        serial_bootlog_flush("halt");
        __asm__ volatile("cli");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    while (dequeue_key(&event)) {
        /* Any key press invalidates the pointer-only repaint shortcut: the
         * key may have changed what is on the desktop (text, focus, menu
         * state) and only a full repaint can be trusted after it. */
        last_key_input_tick = timer_ticks;
        if (system_state == STATE_DESKTOP && keyboard_ctrl && keyboard_shift && event.code == KEY_ENTER) {
            debug_enter();
            continue;
        }
        if (debug_overlay_open) {
            debug_handle_key(event);
            continue;
        }
        /* Stress Test stop shortcut: the trigger keeps loading the machine
         * while the desktop is in the background, so this has to work no
         * matter which window is active.  Shift is reserved for the Task
         * Manager shortcut below. */
        if (system_state == STATE_DESKTOP && stress_test_running &&
            keyboard_ctrl && !keyboard_shift && event.code == KEY_ESC) {
            stress_stop();
            continue;
        }
        if (system_state == STATE_DESKTOP && keyboard_ctrl && keyboard_shift && event.code == KEY_ESC) {
            open_window(APP_TASK_MANAGER);
            continue;
        }
        if (system_state == STATE_DESKTOP && keyboard_ctrl && keyboard_alt && event.code == KEY_DEL) {
            open_window(APP_POWER);
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
                /* A real key press opens the next breakpoint frame. Keys
                 * consumed by the debugger overlay never reach here. */
                debug_bp_note_input();
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
        update_stress_test();
        update_snake();
        update_coin_run();
        update_demos();
        update_doom();

        if (window_fade_active && timer_ticks - window_fade_tick >= 18) {
            window_fade_active = false;
        }
    }
}

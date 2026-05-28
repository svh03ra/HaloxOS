static void execute_terminal_command(Terminal *term, bool boot_console) {
    char command[TERM_LINE_LEN];
    copy_string(command, term->input, sizeof(command));

    term->input_len = 0;
    term->input[0] = '\0';

    if (streq(command, "")) {
        return;
    }

    terminal_add_line(term, command);
    if (boot_console) {
        boot_terminal_dirty = true;
    }

    if (streq(command, "help")) {
        terminal_add_line(term, "help clear cls date time boot shutdown restart halt echo");
    } else if (streq(command, "clear") || streq(command, "cls")) {
        terminal_reset(term);
    } else if (streq(command, "boot")) {
        if (boot_console) {
            enter_main_graphics_mode();
            if (boot_text_mode) {
                terminal_add_line(term, "ERROR: Graphics mode unavailable!!!");
                return;
            }
        }
        open_desktop();
    } else if (streq(command, "shutdown")) {
        shutdown_system();
    } else if (streq(command, "restart")) {
        restart_system();
    } else if (streq(command, "halt")) {
        serial_trace("INFO", "halt command requested");
        cpu_halted_overlay = true;
        terminal_reset(term);
        terminal_add_line(term, "GAME OVER!");
        terminal_add_line(term, "Your world's halting!!! so unpromising...");
    } else if (streq(command, "date") || streq(command, "time")) {
        char buffer[40] = {0};
        read_datetime(buffer, sizeof(buffer));
        terminal_add_line(term, buffer);
    } else if (starts_with(command, "echo ")) {
        terminal_add_line(term, command + 5);
    } else if (streq(command, "about")) {
        terminal_add_line(term, boot_console ? "HaloxOS boot console" : "HaloxOS command prompt");
    } else {
        terminal_add_line(term, "Unknown command. Type help.");
    }
}

static void terminal_handle_key(Terminal *term, KeyEvent event, bool boot_console) {
    if (event.code == KEY_BACKSPACE) {
        if (term->input_len > 0) {
            --term->input_len;
            term->input[term->input_len] = '\0';
            if (boot_console) {
                boot_terminal_dirty = true;
            }
        }
        return;
    }

    if (event.code == KEY_ENTER) {
        execute_terminal_command(term, boot_console);
        return;
    }

    if (event.code == KEY_ESC && boot_console) {
        enter_boot_text_mode();
        system_state = STATE_BOOT_MENU;
        boot_menu_dirty = true;
        return;
    }

    if (event.ch >= 32 && event.ch <= 126 && term->input_len + 1 < TERM_LINE_LEN) {
        term->input[term->input_len++] = event.ch;
        term->input[term->input_len] = '\0';
        if (boot_console) {
            boot_terminal_dirty = true;
        }
    }
}


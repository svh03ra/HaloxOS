static void handle_text_target(KeyEvent event) {
    if (desktop_rename_active) {
        if (event.code == KEY_ENTER) {
            desktop_finish_rename(true);
        } else if (event.code == KEY_ESC) {
            desktop_finish_rename(false);
        } else if (event.code == KEY_BACKSPACE) {
            if (desktop_rename_len > 0) {
                --desktop_rename_len;
                desktop_rename_buffer[desktop_rename_len] = '\0';
            }
        } else if (event.ch >= 32 && event.ch <= 126 && desktop_rename_len < DESKTOP_ICON_NAME_MAX) {
            desktop_rename_buffer[desktop_rename_len++] = event.ch;
            desktop_rename_buffer[desktop_rename_len] = '\0';
        }
        return;
    }

    if (active_window == APP_NOTEPAD) {
        if (event.code == KEY_BACKSPACE) {
            if (notepad_len > 0) {
                --notepad_len;
                notepad_text[notepad_len] = '\0';
            }
        } else if (event.code == KEY_ENTER) {
            append_char(notepad_text, &notepad_len, sizeof(notepad_text), '\n');
        } else if (event.ch >= 32 && event.ch <= 126) {
            append_char(notepad_text, &notepad_len, sizeof(notepad_text), event.ch);
        }
    } else if (active_window == APP_CMD) {
        terminal_handle_key(&cmd_term, event, false);
    } else if (active_window == APP_GUESS) {
        if (event.code == KEY_BACKSPACE) {
            if (guess_input_len > 0) {
                --guess_input_len;
                guess_input[guess_input_len] = '\0';
            }
        } else if (event.code == KEY_ENTER) {
            handle_guess_submit();
        } else if (event.ch >= '0' && event.ch <= '9' && guess_input_len + 1 < (int)sizeof(guess_input)) {
            guess_input[guess_input_len++] = event.ch;
            guess_input[guess_input_len] = '\0';
        }
    } else if (active_window == APP_SNAKE) {
        if (event.code == KEY_UP) snake_next_dir = 0;
        if (event.code == KEY_RIGHT) snake_next_dir = 1;
        if (event.code == KEY_DOWN) snake_next_dir = 2;
        if (event.code == KEY_LEFT) snake_next_dir = 3;
        if (event.code == KEY_ENTER && snake_dead) {
            reset_snake();
            snake_spawn_food();
        }
    } else if (active_window == APP_MINES) {
        if (event.code == KEY_ENTER && (mines_lost || mines_won)) {
            mines_place();
        }
    }
}

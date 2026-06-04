// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: keyboard_routing.c, desktop keyboard input routing.

// This repository is licensed under the GNU General Public License.

static void desktop_paste_from_clipboard(int px, int py);
static void desktop_copy_selected(int primary, bool cut);
static void desktop_delete_selected(int primary);
static void desktop_undo(void);

static void handle_text_target(KeyEvent event) {
    if (keyboard_ctrl && (event.ch == 'z' || event.ch == 'Z') && !desktop_rename_active && active_window < 0) {
        desktop_undo();
        return;
    }
    if (keyboard_ctrl && (event.ch == 'a' || event.ch == 'A') && !desktop_rename_active && active_window < 0) {
        int first = -1;
        for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
            if (desktop_icon_visible[i]) {
                desktop_icon_multi_selected[i] = true;
                if (first < 0) first = i;
            }
        }
        if (first >= 0) selected_desktop_icon = first;
        return;
    }
    if (keyboard_ctrl && (event.ch == 'c' || event.ch == 'C') && !desktop_rename_active && active_window < 0) {
        int primary = selected_desktop_icon;
        if (primary < 0) {
            for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                if (desktop_icon_multi_selected[i]) { primary = i; break; }
            }
        }
        if (primary >= 0) {
            desktop_copy_selected(primary, false);
        }
        return;
    }
    if (keyboard_ctrl && (event.ch == 'x' || event.ch == 'X') && !desktop_rename_active && active_window < 0) {
        int primary = selected_desktop_icon;
        if (primary < 0) {
            for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                if (desktop_icon_multi_selected[i]) { primary = i; break; }
            }
        }
        if (primary >= 0) {
            desktop_copy_selected(primary, true);
        }
        return;
    }
    if (event.code == KEY_DEL && !desktop_rename_active && active_window < 0) {
        int primary = selected_desktop_icon;
        if (primary < 0) {
            for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                if (desktop_icon_multi_selected[i]) { primary = i; break; }
            }
        }
        if (primary >= 0) {
            desktop_delete_selected(primary);
        }
        return;
    }
    if (keyboard_ctrl && (event.ch == 'v' || event.ch == 'V') && !desktop_rename_active && active_window < 0) {
        if (desktop_clipboard_valid) {
            desktop_paste_from_clipboard(mouse.x - 28, mouse.y - 28);
        }
        return;
    }

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
    } else if (active_window == APP_PAINT) {
        if (keyboard_ctrl && (event.ch == 'c' || event.ch == 'C')) {
            memcpy_local(paint_clipboard, paint_canvas, sizeof(paint_canvas));
            paint_clipboard_valid = true;
            return;
        }
        if (keyboard_ctrl && (event.ch == 'x' || event.ch == 'X')) {
            memcpy_local(paint_clipboard, paint_canvas, sizeof(paint_canvas));
            paint_clipboard_valid = true;
            memcpy_local(paint_undo_buffer, paint_canvas, sizeof(paint_canvas));
            paint_undo_valid = true;
            memset_local(paint_canvas, color_white, sizeof(paint_canvas));
            return;
        }
        if (keyboard_ctrl && (event.ch == 'v' || event.ch == 'V') && paint_clipboard_valid) {
            memcpy_local(paint_undo_buffer, paint_canvas, sizeof(paint_canvas));
            paint_undo_valid = true;
            memcpy_local(paint_canvas, paint_clipboard, sizeof(paint_canvas));
            return;
        }
        if (keyboard_ctrl && (event.ch == 'z' || event.ch == 'Z') && paint_undo_valid) {
            memcpy_local(paint_canvas, paint_undo_buffer, sizeof(paint_canvas));
            paint_undo_valid = false;
            return;
        }
        if (event.code == KEY_DEL) {
            memcpy_local(paint_undo_buffer, paint_canvas, sizeof(paint_canvas));
            paint_undo_valid = true;
            memset_local(paint_canvas, color_white, sizeof(paint_canvas));
            paint_text_x = -1;
            paint_text_y = -1;
            return;
        }
        if (paint_tool == 3 && paint_text_x >= 0 && paint_text_y >= 0) {
        if (event.code == KEY_BACKSPACE) {
            paint_text_x -= 8;
            if (paint_text_x < 0) { paint_text_x = 0; }
        } else if (event.code == KEY_ENTER) {
            paint_text_x = 0;
            paint_text_y += 10;
            if (paint_text_y >= PAINT_CANVAS_H) paint_text_y = PAINT_CANVAS_H - 8;
        } else if (event.ch >= 32 && event.ch <= 126) {
            for (int row = 0; row < 8; ++row) {
                uint8_t bits = font8x8_basic[(uint8_t)event.ch - 32][row];
                for (int col = 0; col < 8; ++col) {
                    if ((bits >> col) & 1u) {
                        int px = paint_text_x + col;
                        int py = paint_text_y + row;
                        if (px >= 0 && px < PAINT_CANVAS_W && py >= 0 && py < PAINT_CANVAS_H) {
                            paint_canvas[py * PAINT_CANVAS_W + px] = paint_color;
                        }
                    }
                }
            }
            paint_text_x += 8;
            if (paint_text_x + 8 > PAINT_CANVAS_W) {
                paint_text_x = 0;
                paint_text_y += 10;
                if (paint_text_y >= PAINT_CANVAS_H) paint_text_y = PAINT_CANVAS_H - 8;
            }
            set_active_window(APP_PAINT);
        }
    }
}
}

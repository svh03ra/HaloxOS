static void explorer_handle_mouse(void) {
    if (active_window != APP_EXPLORER || !windows[APP_EXPLORER].open) {
        return;
    }

    Window *window = &windows[APP_EXPLORER];
    int row_x = window->x + 96;
    int row_w = window->w - 110;
    for (int i = 0; i < 6; ++i) {
        int row_y = window->y + 52 + i * 16;
        if (button_clicked(row_x, row_y - 2, row_w, 14)) {
            set_active_window(APP_EXPLORER);
            if (explorer_selected == i) {
                switch (i) {
                    case 0: break;
                    case 1: open_window(APP_NOTEPAD); break;
                    case 2: open_window(APP_CMD); break;
                    case 3: open_window(APP_PAINT); break;
                    case 4: open_window(APP_GAME_CENTER); break;
                    case 5: open_window(APP_SETTINGS); break;
                    default: break;
                }
            } else {
                explorer_selected = i;
            }
        }
    }
}

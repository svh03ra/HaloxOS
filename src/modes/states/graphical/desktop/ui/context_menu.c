// finish in main desc later... so tedious...

static void handle_context_menu_click(void) {
    if (!context_menu_open || !mouse.left || mouse.prev_left) {
        return;
    }

    if (!point_in_rect(mouse.x, mouse.y, context_menu_x, context_menu_y, 120, 60)) {
        context_menu_open = false;
        return;
    }

    int row = (mouse.y - context_menu_y) / 16;
    if (row == 0) {
        context_menu_open = false;
    } else if (row == 1) {
        open_window(APP_EXPLORER);
        context_menu_open = false;
    } else if (row == 2) {
        open_window(APP_SETTINGS);
        context_menu_open = false;
    }
}

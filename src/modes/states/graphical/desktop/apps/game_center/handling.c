static void game_center_handle_mouse(void) {
    if (!windows[APP_GAME_CENTER].open || active_window != APP_GAME_CENTER || !mouse.left || mouse.prev_left) {
        return;
    }

    Window *window = &windows[APP_GAME_CENTER];
    int row_x = window->x + 20;
    int row_y = window->y + 108;
    int row_w = window->w - 40;

    if (point_in_rect(mouse.x, mouse.y, row_x, row_y, row_w, 36)) {
        open_window(APP_MINES);
    } else if (point_in_rect(mouse.x, mouse.y, row_x, row_y + 34, row_w, 36)) {
        open_window(APP_SNAKE);
    } else if (point_in_rect(mouse.x, mouse.y, row_x, row_y + 68, row_w, 36)) {
        open_window(APP_GUESS);
    }
}

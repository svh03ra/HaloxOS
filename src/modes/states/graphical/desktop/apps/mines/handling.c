static void reveal_mines(int x, int y) {
    if (x < 0 || y < 0 || x >= MINES_SIZE || y >= MINES_SIZE) {
        return;
    }
    if (mines_revealed[y][x] || mines_flagged[y][x]) {
        return;
    }
    mines_revealed[y][x] = true;
    if (mines_value[y][x] == 0) {
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox != 0 || oy != 0) {
                    reveal_mines(x + ox, y + oy);
                }
            }
        }
    }
}

static void update_mines_win(void) {
    int revealed = 0;
    for (int y = 0; y < MINES_SIZE; ++y) {
        for (int x = 0; x < MINES_SIZE; ++x) {
            if (mines_revealed[y][x]) {
                ++revealed;
            }
        }
    }
    if (revealed == MINES_SIZE * MINES_SIZE - MINES_COUNT) {
        mines_won = true;
    }
}

static void mines_handle_mouse(void) {
    if (active_window != APP_MINES || !windows[APP_MINES].open) {
        return;
    }

    Window *window = &windows[APP_MINES];
    int gx = window->x + 18;
    int gy = window->y + 34;
    if (point_in_rect(mouse.x, mouse.y, gx, gy, MINES_SIZE * 22, MINES_SIZE * 22)) {
        int cell_x = (mouse.x - gx) / 22;
        int cell_y = (mouse.y - gy) / 22;
        if (mouse.left && !mouse.prev_left && !mines_lost && !mines_won) {
            set_active_window(APP_MINES);
            if (mines_value[cell_y][cell_x] == 9) {
                mines_lost = true;
                for (int y = 0; y < MINES_SIZE; ++y) {
                    for (int x = 0; x < MINES_SIZE; ++x) {
                        if (mines_value[y][x] == 9) {
                            mines_revealed[y][x] = true;
                        }
                    }
                }
            } else {
                reveal_mines(cell_x, cell_y);
                update_mines_win();
            }
        } else if (mouse.right && !mouse.prev_right && !mines_lost && !mines_won) {
            set_active_window(APP_MINES);
            if (!mines_revealed[cell_y][cell_x]) {
                mines_flagged[cell_y][cell_x] = !mines_flagged[cell_y][cell_x];
            }
        }
    }
}

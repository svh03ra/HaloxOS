static void render_desktop_icons(void) {
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        draw_desktop_icon(i, i == selected_desktop_icon || desktop_icon_multi_selected[i]);
    }
}

static bool cursor_over_clickable(void) {
    if (cursor_hand_hint || desktop_icon_hit_test(mouse.x, mouse.y) >= 0) {
        return true;
    }
    if (context_menu_open && point_in_rect(mouse.x, mouse.y, context_menu_x, context_menu_y, 120, 64)) {
        return true;
    }
    if (desktop_icon_menu_open && point_in_rect(mouse.x, mouse.y, desktop_icon_menu_x, desktop_icon_menu_y, 120, 75)) {
        return true;
    }
    if (start_app_menu_open && point_in_rect(mouse.x, mouse.y, start_app_menu_x, start_app_menu_y, 154, 28)) {
        return true;
    }
    for (int app = 0; app < APP_COUNT; ++app) {
        Window *window = &windows[app];
        if (window->open && point_in_rect(mouse.x, mouse.y, window->x + window->w - 18, window->y + 3, 12, 12)) {
            return true;
        }
    }
    return false;
}

static void render_cursor(void) {
    bool hand = cursor_over_clickable();
    /* black outline */
    static const uint16_t outline[12] = {
        0b1000000000000000,
        0b1100000000000000,
        0b1110000000000000,
        0b1111000000000000,
        0b1111100000000000,
        0b1111110000000000,
        0b1111111000000000,
        0b1111111100000000,
        0b1111100000000000,
        0b1101100000000000,
        0b1000110000000000,
        0b0000011000000000
    };

    /* white interior */
    static const uint16_t fill[12] = {
        0b0000000000000000,
        0b0100000000000000,
        0b0110000000000000,
        0b0111000000000000,
        0b0111100000000000,
        0b0111110000000000,
        0b0111111000000000,
        0b0111111100000000,
        0b0111100000000000,
        0b0100100000000000,
        0b0000010000000000,
        0b0000000000000000
    };

    if (hand) {
        static const char hand_cursor[20][21] = {
            "....XXXXX...........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX..........",
            "...XOOOOOX.XXX......",
            "...XOOOOOXXOOOX.....",
            "...XOOOOOXXOOOX.XX..",
            "...XOOOOOXXOOOXXOOX.",
            "...XOOOOOXXOOOXXOOX.",
            "XX.XOOOOOXXOOOXXOOX.",
            "XOOXOOOOOOXOOOOOOOX.",
            "XOOOOOOOOOOOOOOOOOX.",
            ".XOOOOOOOOOOOOOOOX..",
            "..XOOOOOOOOOOOOOX...",
            "...XOOOOOOOOOOOX....",
            "....XOOOOOOOOX......",
            ".....XXXXXXXX......."
        };
        int press = mouse.left ? 1 : 0;
        int x = mouse.x - 6;
        int y = mouse.y + press;

        for (int row = 0; row < 20; ++row) {
            for (int col = 0; col < 20; ++col) {
                char pixel = hand_cursor[row][col];
                if (pixel == 'X') {
                    draw_pixel(x + col, y + row, color_black);
                } else if (pixel == 'O') {
                    draw_pixel(x + col, y + row, color_white);
                }
            }
        }
        return;
    }

    /* draw outline */
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 16; ++col) {
            if (outline[row] & (1u << (15 - col))) {
                draw_pixel(mouse.x + col, mouse.y + row, color_black);
            }
        }
    }

    /* draw fill */
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 16; ++col) {
            if (fill[row] & (1u << (15 - col))) {
                draw_pixel(mouse.x + col, mouse.y + row, color_white);
            }
        }
    }
}

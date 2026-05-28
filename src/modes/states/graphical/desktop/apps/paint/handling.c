static void paint_handle_mouse(void) {
    if (active_window != APP_PAINT || !windows[APP_PAINT].open) {
        return;
    }

    Window *window = &windows[APP_PAINT];
    int canvas_x = window->x + 8;
    int canvas_y = window->y + 52;
    uint8_t colors[8] = {color_black, color_red, color_orange, color_yellow, color_green, color_blue, color_pink, color_white};

    for (int i = 0; i < 8; ++i) {
        if (button_clicked(canvas_x + i * 18, window->y + 24, 14, 8)) {
            paint_color = colors[i];
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (button_clicked(window->x + 220 + i * 28, window->y + 38, 20, 12)) {
            paint_brush_size = (uint8_t)(i + 1);
            set_active_window(APP_PAINT);
        }
    }
    if (point_in_rect(mouse.x, mouse.y, canvas_x, canvas_y, PAINT_CANVAS_W, PAINT_CANVAS_H) && mouse.left) {
        int local_x = mouse.x - canvas_x;
        int local_y = mouse.y - canvas_y;
        int radius = (int)paint_brush_size - 1;
        for (int oy = -radius; oy <= radius; ++oy) {
            for (int ox = -radius; ox <= radius; ++ox) {
                int px = local_x + ox;
                int py = local_y + oy;
                if (px >= 0 && py >= 0 && px < PAINT_CANVAS_W && py < PAINT_CANVAS_H) {
                    paint_canvas[py * PAINT_CANVAS_W + px] = paint_color;
                }
            }
        }
        set_active_window(APP_PAINT);
    }
}

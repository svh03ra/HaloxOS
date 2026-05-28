static void render_paint(const Window *window) {
    int canvas_x = window->x + 8;
    int canvas_y = window->y + 52;
    int canvas_w = PAINT_CANVAS_W;
    int canvas_h = PAINT_CANVAS_H;
    int palette_y = window->y + 26;
    int brush_y = window->y + 40;
    uint8_t colors[8] = {color_black, color_red, color_orange, color_yellow, color_green, color_blue, color_pink, color_white};

    fill_rect(canvas_x, canvas_y, canvas_w, canvas_h, color_white);
    draw_rect(canvas_x, canvas_y, canvas_w, canvas_h, color_gray_dark);

    for (int i = 0; i < 8; ++i) {
        fill_rect(canvas_x + i * 18, palette_y, 14, 8, colors[i]);
        draw_rect(canvas_x + i * 18, palette_y, 14, 8, color_black);
    }

    draw_text(window->x + 176, window->y + 28, "Brush", color_black, color_gray_light, true);
    for (int i = 0; i < 3; ++i) {
        int bx = window->x + 220 + i * 28;
        int size = i * 2 + 1;
        fill_rect(bx, brush_y - 2, 20, 12, paint_brush_size == (uint8_t)(i + 1) ? color_blue : color_white);
        draw_rect(bx, brush_y - 2, 20, 12, color_black);
        fill_rect(bx + 10 - size / 2, brush_y + 4 - size / 2, size, size, color_black);
    }

    for (int y = 0; y < canvas_h; ++y) {
        for (int x = 0; x < canvas_w; ++x) {
            draw_pixel(canvas_x + x, canvas_y + y, paint_canvas[y * PAINT_CANVAS_W + x]);
        }
    }

    draw_text(window->x + 176, window->y + 58, "Left click to draw.", color_black, color_gray_light, true);
    draw_text(window->x + 176, window->y + 72, "Paper sheet fills", color_black, color_gray_light, true);
    draw_text(window->x + 176, window->y + 84, "the whole page now.", color_black, color_gray_light, true);
}

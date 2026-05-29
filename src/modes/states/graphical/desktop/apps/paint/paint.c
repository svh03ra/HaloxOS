static void render_paint(const Window *window) {
    int canvas_x = window->x + 8;
    int canvas_y = window->y + 52;
    int canvas_w = PAINT_CANVAS_W;
    int canvas_h = PAINT_CANVAS_H;
    uint8_t colors[8] = {color_black, color_white, color_red, color_orange, color_yellow, color_green, color_blue, color_pink};
    const char *tool_names[4] = {"Brush", "Fill", "Erase", "Text"};

    fill_rect(window->x, window->y + 18, window->w, 34, color_gray_light);
    draw_rect(window->x, window->y + 18, window->w, 34, color_gray_dark);

    int tool_x = window->x + 6;
    for (int i = 0; i < 4; ++i) {
        bool sel = paint_tool == (uint8_t)i;
        int tw = 44;
        int th = 14;
        fill_rect(tool_x, window->y + 20, tw, th, sel ? color_blue : color_white);
        draw_rect(tool_x, window->y + 20, tw, th, color_black);
        draw_text_center(tool_x + tw / 2, window->y + 22, tool_names[i],
                         sel ? color_white : color_black, sel ? color_blue : color_white, true);
        tool_x += tw + 3;
    }

    int sz_x = window->x + 210;
    draw_text(sz_x, window->y + 21, "Size", color_black, color_gray_light, true);
    for (int i = 0; i < 3; ++i) {
        int bx = sz_x + 32 + i * 22;
        int dot = (i + 1) * 2;
        bool sel = paint_brush_size == (uint8_t)(i + 1);
        fill_rect(bx, window->y + 20, 18, 14, sel ? color_blue : color_white);
        draw_rect(bx, window->y + 20, 18, 14, color_black);
        fill_rect(bx + 9 - dot / 2, window->y + 27 - dot / 2, dot, dot, color_black);
    }

    int palette_x = window->x + 6;
    int palette_y = window->y + 37;
    for (int i = 0; i < 8; ++i) {
        bool sel = paint_color == colors[i];
        fill_rect(palette_x + i * 20, palette_y, 18, 13, colors[i]);
        draw_rect(palette_x + i * 20, palette_y, 18, 13, sel ? color_white : color_black);
    }

    int base = canvas_y * OS_WIDTH + canvas_x;
    for (int y = 0; y < canvas_h; ++y) {
        int row_off = base + y * OS_WIDTH;
        int src_off = y * PAINT_CANVAS_W;
        for (int x = 0; x < canvas_w; ++x) {
            uint8_t c = paint_canvas[src_off + x];
            backbuffer[row_off + x] = c;
            backbuffer_rgb565[row_off + x] = palette_rgb565(c);
        }
    }
    draw_rect(canvas_x - 1, canvas_y - 1, canvas_w + 2, canvas_h + 2, color_black);

    if (paint_tool == 3 && paint_text_x >= 0 && paint_text_y >= 0) {
        int cx = canvas_x + paint_text_x;
        int cy = canvas_y + paint_text_y;
        if ((timer_ticks / 15) & 1) {
            fill_rect(cx, cy + 7, 8, 1, color_black);
        }
    }

    draw_text(canvas_x, window->y + window->h - 11, "Left Click to draw | Pick one with tools.", color_gray_dark, color_gray_light, true);
}

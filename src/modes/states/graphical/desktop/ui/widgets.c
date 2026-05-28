static void draw_button(int x, int y, int w, int h, const char *label, uint8_t fill, uint8_t border, uint8_t text) {
    bool hover = point_in_rect(mouse.x, mouse.y, x, y, w, h);
    bool pressed = hover && mouse.left;
    uint32_t hover_id = ((uint32_t)(x & 0x3FF) << 20) ^ ((uint32_t)(y & 0x3FF) << 10) ^ (uint32_t)((w & 0x1F) << 5) ^ (uint32_t)(h & 0x1F);
    uint8_t draw_fill = fill;
    uint8_t hover_fill = fill == color_green ? color_green_dark :
                         (fill == color_blue ? color_blue_dark :
                         (fill == color_white ? color_gray_light : color_gray));

    if (hover) {
        cursor_hand_hint = true;
        if (button_hover_id != hover_id) {
            button_hover_id = hover_id;
            button_hover_tick = timer_ticks;
        }
    }

    if (pressed) {
        draw_fill = color_gray_dark;
    }

    if (!pressed && hover && fill != color_gray) {
        draw_hover_fade_rect(x, y, w, h, fill, hover_fill, button_hover_tick);
        draw_fill = (timer_ticks - button_hover_tick) >= 9u ? hover_fill : fill;
    } else {
        fill_rect(x, y, w, h, draw_fill);
    }
    draw_rect(x, y, w, h, border);
    if (!pressed) {
        draw_pixel(x + w - 2, y + 1, color_white);
        draw_pixel(x + 1, y + h - 2, color_black);
    }
    draw_text_center(x + w / 2 + (pressed ? 1 : 0), y + (h - 8) / 2 + (pressed ? 1 : 0), label, text, draw_fill, true);
}

static void draw_hover_fade_rect(int x, int y, int w, int h, uint8_t base, uint8_t dark, uint32_t hover_tick) {
    uint32_t age = timer_ticks - hover_tick;
    int density = (int)(age / 3u) + 1;

    if (density >= 4) {
        fill_rect(x, y, w, h, dark);
        return;
    }

    fill_rect(x, y, w, h, base);
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            if (((px + py) & 3) < density) {
                draw_pixel(px, py, dark);
            }
        }
    }
}

static void draw_interactive_row(int x, int y, int w, int h,
                                 const char *label,
                                 uint8_t text,
                                 bool hover,
                                 bool pressed,
                                 uint32_t hover_tick) {
    uint8_t bg = color_gray_light;
    int text_dx = pressed ? 1 : 0;
    int text_dy = pressed ? 1 : 0;

    if (hover) {
        cursor_hand_hint = true;
    }

    if (pressed) {
        fill_rect(x + 1, y + 1, w, h, color_black);
        fill_rect(x, y, w, h, color_gray_dark);
        draw_rect(x, y, w, h, color_black);
    } else if (hover) {
        draw_hover_fade_rect(x, y, w, h, color_gray_light, color_gray, hover_tick);
        draw_rect(x, y, w, h, color_gray_dark);
        bg = (timer_ticks - hover_tick) >= 9u ? color_gray : color_gray_light;
    } else {
        fill_rect(x, y, w, h, color_gray_light);
    }

    draw_text(x + 8 + text_dx, y + (h - 8) / 2 + text_dy, label, text, bg, true);
}

static bool button_clicked(int x, int y, int w, int h) {
    return point_in_rect(mouse.x, mouse.y, x, y, w, h) && mouse.left && !mouse.prev_left;
}

static bool button_right_clicked(int x, int y, int w, int h) {
    return point_in_rect(mouse.x, mouse.y, x, y, w, h) && mouse.right && !mouse.prev_right;
}


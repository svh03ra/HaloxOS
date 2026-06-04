// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, paint app input handling.

// This repository is licensed under the GNU General Public License.

static void paint_flood_fill(int x, int y, uint8_t new_color) {
    uint8_t old_color = paint_canvas[y * PAINT_CANVAS_W + x];
    if (old_color == new_color) return;
    int stack[4096];
    int sp = 0;
    stack[sp++] = x;
    stack[sp++] = y;
    while (sp >= 2) {
        int cy = stack[--sp];
        int cx = stack[--sp];
        if (paint_canvas[cy * PAINT_CANVAS_W + cx] != old_color) continue;
        int left = cx;
        while (left > 0 && paint_canvas[cy * PAINT_CANVAS_W + (left - 1)] == old_color)
            --left;
        int right = cx;
        while (right + 1 < PAINT_CANVAS_W && paint_canvas[cy * PAINT_CANVAS_W + (right + 1)] == old_color)
            ++right;
        for (int col = left; col <= right; ++col)
            paint_canvas[cy * PAINT_CANVAS_W + col] = new_color;
        if (cy > 0) {
            bool in_span = false;
            for (int col = left; col <= right; ++col) {
                if (paint_canvas[(cy - 1) * PAINT_CANVAS_W + col] == old_color) {
                    if (!in_span) {
                        if (sp + 2 <= 4096) { stack[sp++] = col; stack[sp++] = cy - 1; }
                        in_span = true;
                    }
                } else {
                    in_span = false;
                }
            }
        }
        if (cy + 1 < PAINT_CANVAS_H) {
            bool in_span = false;
            for (int col = left; col <= right; ++col) {
                if (paint_canvas[(cy + 1) * PAINT_CANVAS_W + col] == old_color) {
                    if (!in_span) {
                        if (sp + 2 <= 4096) { stack[sp++] = col; stack[sp++] = cy + 1; }
                        in_span = true;
                    }
                } else {
                    in_span = false;
                }
            }
        }
    }
}

static void paint_handle_mouse(void) {
    static int paint_prev_x = -1;
    static int paint_prev_y = -1;
    bool clicked = mouse.left && !mouse.prev_left;

    if (active_window != APP_PAINT || !windows[APP_PAINT].open) {
        paint_prev_x = -1;
        return;
    }

    Window *window = &windows[APP_PAINT];
    int canvas_x = window->x + 8;
    int canvas_y = window->y + 52;
    int palette_y = window->y + 37;
    uint8_t colors[8] = {color_black, color_white, color_red, color_orange, color_yellow, color_green, color_blue, color_pink};

    int tool_x = window->x + 6;
    for (int i = 0; i < 4; ++i) {
        if (button_clicked(tool_x, window->y + 20, 44, 14)) {
            paint_tool = (uint8_t)i;
            set_active_window(APP_PAINT);
            return;
        }
        tool_x += 45;
    }
    int sz_x = window->x + 210;
    for (int i = 0; i < 3; ++i) {
        if (button_clicked(sz_x + 32 + i * 22, window->y + 20, 18, 14)) {
            paint_brush_size = (uint8_t)(i + 1);
            set_active_window(APP_PAINT);
            return;
        }
    }
    for (int i = 0; i < 8; ++i) {
        if (button_clicked(window->x + 6 + i * 20, palette_y, 18, 13)) {
            paint_color = colors[i];
        }
    }

    if (paint_tool == 1) {
        if (clicked && point_in_rect(mouse.x, mouse.y, canvas_x, canvas_y, PAINT_CANVAS_W, PAINT_CANVAS_H)) {
            int lx = mouse.x - canvas_x;
            int ly = mouse.y - canvas_y;
            paint_flood_fill(lx, ly, paint_color);
            set_active_window(APP_PAINT);
        }
        return;
    }

    if (paint_tool == 3) {
        if (clicked && point_in_rect(mouse.x, mouse.y, canvas_x, canvas_y, PAINT_CANVAS_W, PAINT_CANVAS_H)) {
            paint_text_x = mouse.x - canvas_x;
            paint_text_y = mouse.y - canvas_y;
            set_active_window(APP_PAINT);
        }
        return;
    }

    if (!mouse.left) {
        paint_prev_x = -1;
        return;
    }

    if (!point_in_rect(mouse.x, mouse.y, canvas_x, canvas_y, PAINT_CANVAS_W, PAINT_CANVAS_H)) {
        return;
    }

    int local_x = mouse.x - canvas_x;
    int local_y = mouse.y - canvas_y;
    uint8_t draw_color = (paint_tool == 2) ? color_white : paint_color;

    if (paint_prev_x < 0) {
        paint_prev_x = local_x;
        paint_prev_y = local_y;
    }

    int dx = local_x - paint_prev_x;
    int dy = local_y - paint_prev_y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int steps = adx > ady ? adx : ady;
    if (steps == 0) steps = 1;

    int radius = (int)paint_brush_size - 1;
    for (int s = 0; s <= steps; ++s) {
        int cx = paint_prev_x + (dx * s) / steps;
        int cy = paint_prev_y + (dy * s) / steps;
        for (int oy = -radius; oy <= radius; ++oy) {
            for (int ox = -radius; ox <= radius; ++ox) {
                int px = cx + ox;
                int py = cy + oy;
                if (px >= 0 && py >= 0 && px < PAINT_CANVAS_W && py < PAINT_CANVAS_H) {
                    paint_canvas[py * PAINT_CANVAS_W + px] = draw_color;
                }
            }
        }
    }

    paint_prev_x = local_x;
    paint_prev_y = local_y;
    set_active_window(APP_PAINT);
}

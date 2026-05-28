static void draw_window_chrome(const Window *window) {
    fill_rect(window->x, window->y, window->w, window->h, color_gray_light);
    draw_rect(window->x, window->y, window->w, window->h, color_black);
    fill_rect(window->x, window->y, window->w, 18, active_window == (int)(window - windows) ? color_blue : color_gray);
    draw_text(window->x + 4, window->y + 5, window->title, color_white, color_blue, true);
    fill_rect(window->x + window->w - 18, window->y + 3, 12, 12, color_red);
    draw_rect(window->x + window->w - 18, window->y + 3, 12, 12, color_black);
    draw_text(window->x + window->w - 15, window->y + 5, "x", color_white, color_red, true);
}

static void draw_window_chrome(const Window *window) {
    bool is_test = (window >= test_windows && window < test_windows + MAX_TEST_WINDOWS);
    int app_idx;
    if (is_test) {
        app_idx = (int)(window - test_windows);
    } else {
        app_idx = (int)(window - windows);
    }
    bool is_active = is_test ? (active_test_window == app_idx) : (active_window == app_idx);
    bool is_dragged = is_test ? (drag_test_window == app_idx) : (drag_window == app_idx);

    fill_rect(window->x, window->y, window->w, window->h, color_gray_light);
    draw_rect(window->x, window->y, window->w, window->h, color_black);

    uint8_t title_fill = is_active ? color_blue : color_gray;

    if (is_dragged) {
        draw_hover_fade_rect(window->x, window->y, window->w, 18, title_fill, color_blue_dark, drag_anim_tick);
        title_fill = (timer_ticks - drag_anim_tick) >= 9u ? color_blue_dark : title_fill;
    } else {
        fill_rect(window->x, window->y, window->w, 18, title_fill);
    }

    draw_text(window->x + 4, window->y + 5, window->title, color_white, title_fill, true);

    int cx = window->x + window->w - 18;
    int cy = window->y + 3;
    bool close_hover = point_in_rect(mouse.x, mouse.y, cx, cy, 12, 12);
    bool close_pressed = close_hover && mouse.left;
    bool close_hover_was = close_hover || close_pressed;

    if (close_hover_was && window_close_hover_tick == 0) {
        window_close_hover_tick = timer_ticks;
    } else if (!close_hover_was) {
        window_close_hover_tick = 0;
    }

    if (close_pressed) {
        fill_rect(cx + 1, cy + 1, 12, 12, color_black);
        fill_rect(cx, cy, 12, 12, color_gray_dark);
        draw_rect(cx, cy, 12, 12, color_black);
        draw_text(cx + 3 + 1, cy + 2 + 1, "x", color_white, color_gray_dark, true);
    } else if (close_hover && window_close_hover_tick > 0) {
        draw_hover_fade_rect(cx, cy, 12, 12, color_red, color_gray_dark, window_close_hover_tick);
        draw_rect(cx, cy, 12, 12, color_black);
        uint8_t close_bg = (timer_ticks - window_close_hover_tick) >= 9u ? color_gray_dark : color_red;
        draw_text(cx + 3, cy + 2, "x", color_white, close_bg, true);
    } else {
        fill_rect(cx, cy, 12, 12, color_red);
        draw_rect(cx, cy, 12, 12, color_black);
        draw_text(cx + 3, cy + 2, "x", color_white, color_red, true);
    }
}

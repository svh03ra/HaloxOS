// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, Stress Test rendering.

// This repository is licensed under the GNU General Public License.

/* A one-choice circle: black ring, blue dot when selected. */
static void stress_draw_radio(int center_x, int center_y, bool selected) {
    for (int dy = -5; dy <= 5; ++dy) {
        for (int dx = -5; dx <= 5; ++dx) {
            int distance = dx * dx + dy * dy;

            if (distance >= 16 && distance <= 30) {
                draw_pixel(center_x + dx, center_y + dy, color_black);
            }
        }
    }
    if (selected) {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (dx * dx + dy * dy <= 4) {
                    draw_pixel(center_x + dx, center_y + dy, color_blue_dark);
                }
            }
        }
    }
}

/* GPU mode: burn the framebuffer by stamping a pixel budget into the client
 * area before the controls are painted over them.  A large budget fills
 * whole scanlines (real pixel throughput), and the remainder lands as
 * random pixels.  The budget is refilled by update_stress_test once per
 * frame from the current live load. */
static void stress_render_gpu_load(const Window *window) {
    uint32_t budget = stress_gpu_budget;
    int left = window->x + 9;
    int top = window->y + 25;
    int width = window->w - 18;
    int height = window->h - 34;

    stress_gpu_budget = 0;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (budget > STRESS_GPU_MAX_PIXELS) {
        budget = STRESS_GPU_MAX_PIXELS;
    }

    /* Whole-window passes first: at 2000% the budget is many screens worth
     * of real framebuffer traffic, and a rect fill moves it far faster than
     * one draw_pixel call per pixel. */
    {
        uint32_t area = (uint32_t)width * (uint32_t)height;

        while (area != 0u && budget >= area) {
            fill_rect(left, top, width, height,
                      (uint8_t)(rand_range(16) + 1));
            budget -= area;
        }
    }
    if (budget >= (uint32_t)width) {
        uint32_t rows = budget / (uint32_t)width;

        if (rows > (uint32_t)height) {
            rows = (uint32_t)height;
        }
        for (uint32_t y = 0; y < rows; ++y) {
            fill_rect(left, top + (int)y, width, 1,
                      (uint8_t)(rand_range(16) + 1));
        }
        budget -= rows * (uint32_t)width;
    }
    for (uint32_t i = 0; i < budget; ++i) {
        draw_pixel(left + rand_range(width), top + rand_range(height),
                   (uint8_t)(rand_range(16) + 1));
    }
}

static void stress_draw_field(const Window *window, int which,
                              int y, const char *label, const char *hint) {
    int x = window->x;
    int box_x = x + STRESS_BOX_X;
    int box_y = window->y + y;
    const char *text = which == 0 ? stress_intensity_text : stress_interval_text;
    int len = which == 0 ? stress_intensity_len : stress_interval_len;
    bool focused = stress_field_focus == which &&
                   active_window == APP_STRESS_TEST;

    draw_text(x + 22, box_y + 5, label, color_black, color_white, true);
    fill_rect(box_x, box_y, STRESS_BOX_W, STRESS_BOX_H,
              focused ? color_white : color_gray_light);
    draw_rect(box_x, box_y, STRESS_BOX_W, STRESS_BOX_H,
              focused ? color_blue_dark : color_gray_dark);
    draw_text(box_x + 5, box_y + 5, text, color_black,
              focused ? color_white : color_gray_light, true);
    if (focused && ((timer_ticks / TERMINAL_CURSOR_BLINK_TICKS) & 1u) == 0u) {
        int caret_x = box_x + 5 + len * 8;

        if (caret_x < box_x + STRESS_BOX_W - 2) {
            fill_rect(caret_x, box_y + 5, 1, 8, color_blue_dark);
        }
    }
    draw_text(box_x + STRESS_BOX_W + 10, box_y + 5, hint,
              color_gray_dark, color_white, true);
}

static void stress_render_log(const Window *window) {
    char line[STRESS_LOG_LEN];
    int x = window->x + 16;
    int y = window->y + STRESS_ROW_LOG;
    int w = window->w - 32;

    fill_rect(x, y, w, STRESS_LOG_H, color_gray_light);
    draw_rect(x, y, w, STRESS_LOG_H, color_gray_dark);

    line[0] = '\0';
    if (stress_test_running) {
        uint32_t interval_frames = stress_interval_frames();
        uint32_t remain_frames = stress_frames_since_trigger >= interval_frames
                                     ? 0u
                                     : interval_frames - stress_frames_since_trigger;
        uint32_t remain_ms = (remain_frames * 1000u + TIMER_HZ - 1u) / TIMER_HZ;
        size_t len = 0;

        append_string(line, &len, sizeof(line), "Upcoming in Trigger: ");
        append_uint(line, &len, sizeof(line), remain_ms);
        append_string(line, &len, sizeof(line), " ms (");
        append_uint(line, &len, sizeof(line), remain_frames);
        append_string(line, &len, sizeof(line), " frames)");
    } else {
        copy_string(line, "Trigger: stopped.", sizeof(line));
    }
    draw_text_clipped(x + 6, y + 5, w - 12, line, color_blue_dark,
                      color_gray_light, true);

    line[0] = '\0';
    {
        size_t len = 0;

        append_string(line, &len, sizeof(line), stress_mode_name(stress_test_mode));
        append_string(line, &len, sizeof(line), "  Intensity: ");
        append_uint(line, &len, sizeof(line), stress_intensity);
        append_string(line, &len, sizeof(line), "  Every: ");
        append_uint(line, &len, sizeof(line), stress_interval_ms);
        append_string(line, &len, sizeof(line), " ms  Power: ");
        append_uint(line, &len, sizeof(line), stress_power);
        append_char(line, &len, sizeof(line), '%');
    }
    draw_text_clipped(x + 6, y + 15, w - 12, line, color_black,
                      color_gray_light, true);

    /* Rows 2..15 of the 16-line view: the event log, oldest cleared once
     * the newest one arrives. */
    for (int i = 0; i < stress_log_count && i < STRESS_EVENT_LINES; ++i) {
        draw_text_clipped(x + 6, y + 25 + i * 10, w - 12, stress_log[i],
                          color_black, color_gray_light, true);
    }
}

static void render_stress(const Window *window) {
    int x = window->x;
    int y = window->y;
    int client_w = window->w - 16;

    fill_rect(x + 8, y + 24, client_w, window->h - 32, color_white);
    if (stress_test_running && stress_test_mode == 2u) {
        stress_render_gpu_load(window);
    }

    /* The warning is intentionally red and first: this app can make the
     * machine crawl, so the caution is not buried in the log. */
    draw_text_clipped(x + 14, y + STRESS_ROW_WARN1, client_w - 12,
                      "To use this, BE CAUTIOUS it pushes the system to its limits may cause lag!",
                      color_red, color_white, true);
    draw_text(x + 14, y + STRESS_ROW_WARN2, "USE IT YOUR OWN RISK!!!",
              color_red, color_white, true);
    draw_text(x + 22, y + STRESS_ROW_SELECT, "Select mode you'd like:",
              color_black, color_white, true);

    for (int mode = 0; mode < 4; ++mode) {
        int rx = x + stress_radio_x((uint8_t)mode);

        stress_draw_radio(rx + 5, y + STRESS_ROW_RADIO + 5,
                          stress_test_mode == (uint8_t)mode);
        draw_text(rx + 16, y + STRESS_ROW_RADIO + 1,
                  stress_mode_name((uint8_t)mode), color_black, color_white, true);
    }

    stress_draw_field(window, 0, STRESS_BOX_Y_INTENSITY, "Intensity:", "max 65536");
    stress_draw_field(window, 1, STRESS_BOX_Y_INTERVAL, "Trigger per second:",
                      "ms, max 120000");

    draw_button(x + STRESS_TRIGGER_X, y + STRESS_ROW_BUTTONS,
                STRESS_BUTTON_W, STRESS_BUTTON_H, "Trigger",
                color_green, color_black, color_black);
    if (stress_test_running) {
        draw_button(x + STRESS_STOP_X, y + STRESS_ROW_BUTTONS,
                    STRESS_BUTTON_W, STRESS_BUTTON_H, "Stop",
                    color_red, color_black, color_white);
    }
    draw_text(x + STRESS_STOP_X + STRESS_BUTTON_W + 12,
              y + STRESS_ROW_BUTTONS + 8,
              "Ctrl+Esc stops any time.", color_gray_dark, color_white, true);

    stress_render_log(window);
}

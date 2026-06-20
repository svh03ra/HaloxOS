// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: explorer.c, file explorer app.

// This repository is licensed under the GNU General Public License.

static void render_explorer(const Window *window) {
    static const char *names[] = {
        "Desktop",
        "Notepad",
        "Command Prompt",
        "Paint",
        "Games",
        "Settings"
    };
    static const char *types[] = {
        "Folder",
        "Text App",
        "Console",
        "Image App",
        "Folder",
        "Control"
    };
    int left_x = window->x + 8;
    int left_w = 82;
    int right_x = left_x + left_w + 8;
    int content_y = window->y + 26;
    int content_h = window->h - 34;
    int footer_h = 38;
    int body_h = content_h - footer_h;
    int header_y = content_y + 8;
    int row_start_y = content_y + 26;
    int row_h = 16;
    int name_w = 124;
    int type_w = window->w - left_w - 34 - name_w;

    fill_rect(window->x + 8, content_y, window->w - 16, content_h, color_white);
    fill_rect(left_x, content_y, left_w, content_h, color_gray_light);
    draw_rect(left_x, content_y, left_w, content_h, color_gray_dark);
    draw_text(left_x + 6, header_y, "Places", color_blue_dark, color_gray_light, true);
    draw_text(left_x + 6, content_y + 24, "A:\\", color_black, color_gray_light, true);
    draw_text(left_x + 6, content_y + 40, "Desktop", color_black, color_gray_light, true);
    draw_text(left_x + 6, content_y + 56, "System", color_black, color_gray_light, true);

    draw_text(right_x, header_y, "Name", color_blue_dark, color_white, true);
    draw_text(right_x + name_w, header_y, "Type", color_blue_dark, color_white, true);
    draw_rect(right_x - 2, content_y + 18, window->w - left_w - 22, 1, color_gray_dark);

    for (int i = 0; i < 6; ++i) {
        int row_y = row_start_y + i * row_h;
        uint8_t row_fill = i == explorer_selected ? color_blue : color_white;
        uint8_t row_text = i == explorer_selected ? color_white : color_black;
        fill_rect(right_x - 2, row_y - 2, window->w - left_w - 22, 14, row_fill);
        draw_text_clipped(right_x, row_y, name_w - 6, names[i], row_text, row_fill, true);
        draw_text_clipped(right_x + name_w, row_y, type_w, types[i], row_text, row_fill, true);
    }

    draw_rect(right_x - 2, content_y + body_h, window->w - left_w - 22, 1, color_gray_dark);
    draw_text(right_x, content_y + body_h + 8, "Click an item to select it.", color_black, color_white, true);
    draw_text(right_x, content_y + body_h + 20, "Second click opens it.", color_black, color_white, true);
}

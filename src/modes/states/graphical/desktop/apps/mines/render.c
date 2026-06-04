// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, minesweeper rendering.

// This repository is licensed under the GNU General Public License.

static void render_mines(const Window *window) {
    int gx = window->x + 18;
    int gy = window->y + 34;
    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_gray_light);
    for (int y = 0; y < MINES_SIZE; ++y) {
        for (int x = 0; x < MINES_SIZE; ++x) {
            int cell_x = gx + x * 22;
            int cell_y = gy + y * 22;
            uint8_t fill = mines_revealed[y][x] ? color_white : color_gray;
            fill_rect(cell_x, cell_y, 20, 20, fill);
            draw_rect(cell_x, cell_y, 20, 20, color_black);
            if (mines_flagged[y][x]) {
                draw_text(cell_x + 6, cell_y + 6, "F", color_red, fill, true);
            } else if (mines_revealed[y][x]) {
                if (mines_value[y][x] == 9) {
                    draw_text(cell_x + 6, cell_y + 6, "*", color_red, fill, true);
                } else if (mines_value[y][x] > 0) {
                    char num[2] = {(char)('0' + mines_value[y][x]), '\0'};
                    draw_text(cell_x + 6, cell_y + 6, num, color_blue_dark, fill, true);
                }
            }
        }
    }
    if (mines_lost) {
        draw_text(window->x + 200, window->y + 42, "GAME OVER!", color_red, color_gray_light, true);
        draw_text(window->x + 200, window->y + 52, "*boom*", color_red, color_gray_light, true);
        draw_text(window->x + 200, window->y + 70, "Press ENTER", color_red, color_gray_light, true);
        draw_text(window->x + 200, window->y + 80, "to restart", color_red, color_gray_light, true);
    } else if (mines_won) {
        draw_text(window->x + 200, window->y + 42, "You win!", color_green, color_gray_light, true);
        draw_text(window->x + 200, window->y + 60, "Press ENTER", color_green, color_gray_light, true);
        draw_text(window->x + 200, window->y + 70, "to replay", color_green, color_gray_light, true);
    } else {
        draw_text(window->x + 200, window->y + 42, "LC=Reveal", color_black, color_gray_light, true);
        draw_text(window->x + 200, window->y + 52, "LR=Flag", color_black, color_gray_light, true);
    }
}

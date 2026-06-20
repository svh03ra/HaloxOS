// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: render.c, task manager rendering.

// This repository is licensed under the GNU General Public License.

static void render_task_manager(const Window *window) {
    char cpu_speed[32] = {0};
    char ram_total[32] = {0};
    char ram_used[32] = {0};
    char gpu_memory[32] = {0};
    char disk_io[24] = {0};
    char temp[24] = {0};
    char page_text[24] = {0};
    int process_count = task_manager_process_count();
    int content_x = window->x + 18;
    int grid_x = content_x;
    int grid_y = window->y + 64;
    int scroll_x = grid_x + GPU_GRID_W + 10;
    uint32_t total_gpu_entries = gpu_palette_entry_count();
    uint32_t max_gpu_scroll = gpu_palette_max_scroll();

    if (task_manager_selected_process >= process_count) {
        task_manager_selected_process = process_count > 0 ? process_count - 1 : 0;
    }
    if (gpu_palette_scroll > max_gpu_scroll) {
        gpu_palette_scroll = max_gpu_scroll;
    }

    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_white);
    draw_button(window->x + 14, window->y + 30, 84, 20, "Processes", task_manager_tab == 0 ? color_blue : color_gray_light, color_black, task_manager_tab == 0 ? color_white : color_black);
    draw_button(window->x + 104, window->y + 30, 96, 20, "Performance", task_manager_tab == 1 ? color_blue : color_gray_light, color_black, task_manager_tab == 1 ? color_white : color_black);
    draw_button(window->x + 206, window->y + 30, 54, 20, "GPU", task_manager_tab == 2 ? color_blue : color_gray_light, color_black, task_manager_tab == 2 ? color_white : color_black);

    if (task_manager_tab == 0) {
        int list_x = window->x + 18;
        int list_y = window->y + 62;
        int list_w = window->w - 36;
        int rows = process_count;

        fill_rect(list_x, list_y, list_w, 162, color_gray_light);
        draw_rect(list_x, list_y, list_w, 162, color_gray_dark);
        draw_text(list_x + 6, list_y + 6, "Process", color_blue_dark, color_gray_light, true);
        draw_text(list_x + 210, list_y + 6, "Type", color_blue_dark, color_gray_light, true);

        for (int i = 0; i < rows && i < 9; ++i) {
            const char *name = "";
            const char *type = "";
            AppId app = APP_NOTEPAD;
            bool killable = task_manager_process_info(i, &name, &type, &app);
            int row_y = list_y + 24 + i * 15;
            uint8_t fill = i == task_manager_selected_process ? color_blue : color_white;
            uint8_t text = i == task_manager_selected_process ? color_white : (killable ? color_black : color_blue_dark);
            fill_rect(list_x + 4, row_y - 2, list_w - 8, 13, fill);
            draw_text_clipped(list_x + 8, row_y, 194, name, text, fill, true);
            draw_text_clipped(list_x + 210, row_y, 140, type, text, fill, true);
        }

        {
            AppId selected_app;
            bool killable = task_manager_selected_is_killable(&selected_app);
            draw_button(window->x + 18, window->y + 232, 100, 22, "Kill Process", killable ? color_gray_light : color_gray, color_black, color_black);
        }
    } else if (task_manager_tab == 1) {
        format_cpu_speed(cpu_speed, sizeof(cpu_speed));
        format_ram_total(ram_total, sizeof(ram_total));
        format_ram_used(ram_used, sizeof(ram_used));
        format_gpu_memory(gpu_memory, sizeof(gpu_memory));
        format_disk_io(disk_io, sizeof(disk_io));

        draw_text(window->x + 20, window->y + 62, "CPU:", color_black, color_white, true);
        draw_usage_graph(window->x + 20, window->y + 74, cpu_usage_history, color_green);
        draw_text(window->x + 104, window->y + 76, "CPU Usage :", color_black, color_white, true);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), cpu_usage_percent);
            append_char(temp, &len, sizeof(temp), '%');
        }
        draw_text(window->x + 200, window->y + 76, temp, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 90, "CPU Speed :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 90, cpu_speed, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 104, "CPU State :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 104, cpu_state_label(), cpu_state_color(), color_white, true);

        draw_text(window->x + 20, window->y + 128, "RAM:", color_black, color_white, true);
        draw_usage_graph(window->x + 20, window->y + 140, ram_usage_history, color_yellow);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), ram_usage_percent);
            append_char(temp, &len, sizeof(temp), '%');
        }
        draw_text(window->x + 104, window->y + 142, "RAM Usage :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 142, temp, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 156, "RAM Bytes :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 156, ram_total, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 170, "RAM Used  :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 170, ram_used, color_blue_dark, color_white, true);

        draw_text(window->x + 20, window->y + 194, "GPU:", color_black, color_white, true);
        draw_usage_graph(window->x + 20, window->y + 206, gpu_usage_history, color_orange);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), gpu_usage_percent);
            append_char(temp, &len, sizeof(temp), '%');
        }
        draw_text(window->x + 104, window->y + 208, "GPU Usage :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 208, temp, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 222, "GPU Memory:", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 222, gpu_memory, color_blue_dark, color_white, true);

        draw_text(window->x + 20, window->y + 254, "Disk:", color_black, color_white, true);
        draw_usage_graph(window->x + 20, window->y + 266, disk_usage_history, color_white);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), disk_usage_percent);
            append_char(temp, &len, sizeof(temp), '%');
        }
        draw_text(window->x + 104, window->y + 268, "Disk Usage        :", color_black, color_white, true);
        draw_text(window->x + 262, window->y + 268, temp, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 282, "Disk I/O Activity :", color_black, color_white, true);
        draw_text(window->x + 262, window->y + 282, disk_io, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 296, "Disk Physical Type:", color_black, color_white, true);
        draw_text(window->x + 262, window->y + 296, disk_physical_type_label(), color_blue_dark, color_white, true);
    } else {
        draw_text(window->x + 20, window->y + 62, "GPU:", color_black, color_white, true);
        fill_rect(grid_x - 2, grid_y - 2, GPU_GRID_W + 4, GPU_GRID_H + 4, color_gray_light);
        draw_rect(grid_x - 2, grid_y - 2, GPU_GRID_W + 4, GPU_GRID_H + 4, color_gray_dark);
        for (int index = 0; index < GPU_GRID_PAGE_ENTRIES; ++index) {
            uint32_t entry = gpu_palette_scroll + (uint32_t)index;
            int gx = grid_x + (index % GPU_GRID_COLS) * GPU_SWATCH_SIZE;
            int gy = grid_y + (index / GPU_GRID_COLS) * GPU_SWATCH_SIZE;
            if (entry < total_gpu_entries) {
                fill_rect(gx, gy, GPU_SWATCH_SIZE - 1, GPU_SWATCH_SIZE - 1, gpu_palette_entry_color(entry));
                draw_rect(gx, gy, GPU_SWATCH_SIZE - 1, GPU_SWATCH_SIZE - 1, color_black);
            } else {
                fill_rect(gx, gy, GPU_SWATCH_SIZE - 1, GPU_SWATCH_SIZE - 1, color_gray_light);
                draw_rect(gx, gy, GPU_SWATCH_SIZE - 1, GPU_SWATCH_SIZE - 1, color_gray_dark);
                draw_pixel(gx + 1, gy + 1, color_red);
                draw_pixel(gx + GPU_SWATCH_SIZE - 3, gy + GPU_SWATCH_SIZE - 3, color_red);
                draw_pixel(gx + 1, gy + GPU_SWATCH_SIZE - 3, color_red);
                draw_pixel(gx + GPU_SWATCH_SIZE - 3, gy + 1, color_red);
            }
        }

        draw_button(scroll_x, grid_y - 2, 16, 16, "^", gpu_palette_scroll > 0 ? color_gray_light : color_gray, color_black, color_black);
        fill_rect(scroll_x, grid_y + 16, 16, GPU_GRID_H - 32, color_gray_light);
        draw_rect(scroll_x, grid_y + 16, 16, GPU_GRID_H - 32, color_gray_dark);
        if (total_gpu_entries > GPU_GRID_PAGE_ENTRIES) {
            int thumb_h = 20;
            int thumb_track = (GPU_GRID_H - 32) - thumb_h;
            int thumb_y = grid_y + 17 + (int)((gpu_palette_scroll * (uint32_t)thumb_track) / max_gpu_scroll);
            fill_rect(scroll_x + 2, thumb_y, 12, thumb_h, color_blue);
            draw_rect(scroll_x + 2, thumb_y, 12, thumb_h, color_black);
        } else {
            fill_rect(scroll_x + 2, grid_y + 18, 12, 20, color_gray);
            draw_rect(scroll_x + 2, grid_y + 18, 12, 20, color_black);
        }
        draw_button(scroll_x, grid_y + GPU_GRID_H - 16, 16, 16, "v", gpu_palette_scroll < max_gpu_scroll ? color_gray_light : color_gray, color_black, color_black);

        page_text[0] = '\0';
        {
            size_t len = 0;
            uint32_t current_page = (gpu_palette_scroll / GPU_GRID_PAGE_ENTRIES) + 1u;
            uint32_t page_count = (total_gpu_entries + GPU_GRID_PAGE_ENTRIES - 1u) / GPU_GRID_PAGE_ENTRIES;
            append_uint(page_text, &len, sizeof(page_text), current_page);
            append_char(page_text, &len, sizeof(page_text), '/');
            append_uint(page_text, &len, sizeof(page_text), page_count);
        }

        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), total_gpu_entries);
        }
        draw_text(grid_x + 2, grid_y + 150, "Total GPU Palettes:", color_black, color_white, true);
        draw_text(grid_x + 160, grid_y + 150, temp, color_blue_dark, color_white, true);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), fb.bpp);
        }
        draw_text(grid_x + 2, grid_y + 164, "Video Bit Depth   :", color_black, color_white, true);
        draw_text(grid_x + 160, grid_y + 164, temp, color_blue_dark, color_white, true);
        draw_text(grid_x + 2, grid_y + 178, "Palette Page      :", color_black, color_white, true);
        draw_text(grid_x + 160, grid_y + 178, page_text, color_blue_dark, color_white, true);
    }

    if (task_manager_confirm_kill) {
        int box_x = window->x + 90;
        int box_y = window->y + 92;
        fill_rect(box_x, box_y, 250, 96, color_gray_light);
        draw_rect(box_x, box_y, 250, 96, color_black);
        draw_text_center(box_x + 125, box_y + 18, "Kill selected process?", color_black, color_gray_light, true);
        draw_text_center(box_x + 125, box_y + 34, "This will close the window.", color_black, color_gray_light, true);
        draw_button(box_x + 34, box_y + 60, 78, 22, "Proceed", color_gray_light, color_black, color_black);
        draw_button(box_x + 138, box_y + 60, 78, 22, "Cancel", color_gray_light, color_black, color_black);
    }
}

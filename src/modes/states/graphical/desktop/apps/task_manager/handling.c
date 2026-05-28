static void task_manager_handle_mouse(void) {
    if (windows[APP_TASK_MANAGER].open && active_window == APP_TASK_MANAGER && mouse.left && !mouse.prev_left) {
        Window *window = &windows[APP_TASK_MANAGER];
        if (task_manager_confirm_kill) {
            int box_x = window->x + 90;
            int box_y = window->y + 92;
            if (button_clicked(box_x + 34, box_y + 60, 78, 22)) {
                if (task_manager_kill_target >= 0 && task_manager_kill_target < APP_COUNT) {
                    close_window((AppId)task_manager_kill_target);
                }
                task_manager_confirm_kill = false;
                task_manager_kill_target = -1;
                return;
            }
            if (button_clicked(box_x + 138, box_y + 60, 78, 22)) {
                task_manager_confirm_kill = false;
                task_manager_kill_target = -1;
                return;
            }
            return;
        }

        if (button_clicked(window->x + 14, window->y + 30, 84, 20)) {
            task_manager_tab = 0;
            task_manager_gpu_scroll_drag = false;
        } else if (button_clicked(window->x + 104, window->y + 30, 96, 20)) {
            task_manager_tab = 1;
            task_manager_gpu_scroll_drag = false;
        } else if (button_clicked(window->x + 206, window->y + 30, 54, 20)) {
            task_manager_tab = 2;
        } else if (task_manager_tab == 0) {
            int rows = task_manager_process_count();
            for (int i = 0; i < rows && i < 9; ++i) {
                int row_y = window->y + 86 + i * 15;
                if (button_clicked(window->x + 22, row_y - 2, window->w - 44, 13)) {
                    task_manager_selected_process = i;
                }
            }
            if (button_clicked(window->x + 18, window->y + 232, 100, 22)) {
                AppId kill_target;
                if (task_manager_selected_is_killable(&kill_target)) {
                    task_manager_confirm_kill = true;
                    task_manager_kill_target = kill_target;
                }
            }
        } else if (task_manager_tab == 2) {
            int scroll_x = window->x + 18 + GPU_GRID_W + 10;
            int track_y = window->y + 80;
            int thumb_y;
            int thumb_h;
            int thumb_track;

            gpu_palette_scroll_geometry(track_y, &thumb_y, &thumb_h, &thumb_track);

            if (button_clicked(scroll_x, window->y + 62, 16, 16) && gpu_palette_scroll >= GPU_GRID_COLS) {
                gpu_palette_scroll -= GPU_GRID_COLS;
            } else if (button_clicked(scroll_x, window->y + 64 + GPU_GRID_H - 16, 16, 16) && gpu_palette_scroll < gpu_palette_max_scroll()) {
                gpu_palette_scroll += GPU_GRID_COLS;
                if (gpu_palette_scroll > gpu_palette_max_scroll()) {
                    gpu_palette_scroll = gpu_palette_max_scroll();
                }
            } else if (button_clicked(scroll_x + 2, thumb_y, 12, thumb_h)) {
                task_manager_gpu_scroll_drag = true;
                task_manager_gpu_scroll_drag_offset = mouse.y - thumb_y;
            } else if (button_clicked(scroll_x, track_y + 16, 16, GPU_GRID_H - 32)) {
                if (mouse.y < thumb_y && gpu_palette_scroll >= GPU_GRID_PAGE_ENTRIES) {
                    gpu_palette_scroll -= GPU_GRID_PAGE_ENTRIES;
                } else if (mouse.y > thumb_y + thumb_h && gpu_palette_scroll < gpu_palette_max_scroll()) {
                    gpu_palette_scroll += GPU_GRID_PAGE_ENTRIES;
                    if (gpu_palette_scroll > gpu_palette_max_scroll()) {
                        gpu_palette_scroll = gpu_palette_max_scroll();
                    }
                }
            }
        }
    }

    if (task_manager_gpu_scroll_drag && (!mouse.left || !windows[APP_TASK_MANAGER].open || active_window != APP_TASK_MANAGER || task_manager_tab != 2)) {
        task_manager_gpu_scroll_drag = false;
    } else if (task_manager_gpu_scroll_drag && mouse.left) {
        Window *window = &windows[APP_TASK_MANAGER];
        int track_y = window->y + 80;
        int thumb_y;
        int thumb_h;
        int thumb_track;
        int thumb_min = track_y + 1;
        int desired_y;
        uint32_t max_scroll = gpu_palette_max_scroll();

        gpu_palette_scroll_geometry(track_y, &thumb_y, &thumb_h, &thumb_track);
        desired_y = clampi(mouse.y - task_manager_gpu_scroll_drag_offset, thumb_min, thumb_min + thumb_track);
        if (max_scroll != 0) {
            uint32_t raw_scroll = (uint32_t)((desired_y - thumb_min) * (int)max_scroll / thumb_track);
            gpu_palette_scroll = (raw_scroll / GPU_GRID_COLS) * GPU_GRID_COLS;
            if (gpu_palette_scroll > max_scroll) {
                gpu_palette_scroll = max_scroll;
            }
        }
    }
}

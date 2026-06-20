// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: system_api.c, task manager system API.

// This repository is licensed under the GNU General Public License.

static int open_window_count(void) {
    int count = 0;
    for (int app = 0; app < APP_COUNT; ++app) {
        if (windows[app].open) {
            ++count;
        }
    }
    return count;
}

static int task_manager_process_count(void) {
    return 3 + open_window_count();
}

static bool task_manager_process_info(int index, const char **name_out, const char **type_out, AppId *app_out) {
    static const char *system_names[] = {"HaloxOS Kernel", "Timer Service", "Input Service"};
    static const char *system_types[] = {"System", "System", "System"};
    int app_index = 0;

    if (index < 0) {
        return false;
    }

    if (index < 3) {
        *name_out = system_names[index];
        *type_out = system_types[index];
        *app_out = APP_NOTEPAD;
        return false;
    }

    for (int app = 0; app < APP_COUNT; ++app) {
        if (!windows[app].open) {
            continue;
        }
        if (app_index == index - 3) {
            *name_out = windows[app].title != NULL ? windows[app].title : app_titles[app];
            *type_out = app == active_window ? "Window (Active)" : "Window";
            *app_out = (AppId)app;
            return true;
        }
        ++app_index;
    }

    return false;
}

static bool task_manager_selected_is_killable(AppId *app_out) {
    const char *name;
    const char *type;
    AppId app = APP_NOTEPAD;
    bool killable = task_manager_process_info(task_manager_selected_process, &name, &type, &app);

    if (app_out != NULL) {
        *app_out = app;
    }
    return killable;
}

static uint32_t gpu_palette_entry_count(void) {
    if (fb.bpp == 8) {
        return settings_applied.palette_mode == 1 ? 16u : 256u;
    }
    if (fb.bpp >= 16) {
        return 65536u;
    }
    return 0;
}

static uint8_t gpu_palette_entry_color(uint32_t index) {
    if (fb.bpp == 8) {
        if (index >= gpu_palette_entry_count()) {
            return color_gray_light;
        }
        return (uint8_t)index;
    }

    if (fb.bpp == 16) {
        uint8_t r = (uint8_t)(((index >> 11) & 0x1Fu) * 255u / 31u);
        uint8_t g = (uint8_t)(((index >> 5) & 0x3Fu) * 255u / 63u);
        uint8_t b = (uint8_t)((index & 0x1Fu) * 255u / 31u);
        return nearest_color(r, g, b);
    }

    return nearest_color((uint8_t)((index >> 11) & 0xF8u),
                         (uint8_t)((index >> 5) & 0xFCu),
                         (uint8_t)((index << 3) & 0xF8u));
}

static uint32_t gpu_palette_max_scroll(void) {
    uint32_t total = gpu_palette_entry_count();

    if (total <= GPU_GRID_PAGE_ENTRIES) {
        return 0;
    }
    return total - GPU_GRID_PAGE_ENTRIES;
}

static void gpu_palette_scroll_geometry(int track_y,
                                        int *thumb_y_out,
                                        int *thumb_h_out,
                                        int *thumb_track_out) {
    uint32_t max_scroll = gpu_palette_max_scroll();
    int thumb_h = 20;
    int thumb_track = (GPU_GRID_H - 32) - thumb_h;
    int thumb_y = track_y + 1;

    if (max_scroll != 0) {
        thumb_y += (int)((gpu_palette_scroll * (uint32_t)thumb_track) / max_scroll);
    }

    if (thumb_y_out != NULL) {
        *thumb_y_out = thumb_y;
    }
    if (thumb_h_out != NULL) {
        *thumb_h_out = thumb_h;
    }
    if (thumb_track_out != NULL) {
        *thumb_track_out = thumb_track;
    }
}

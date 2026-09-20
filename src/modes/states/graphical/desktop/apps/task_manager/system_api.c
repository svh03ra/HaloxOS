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
    if (fb.bpp == 4) {
        return 16u;
    }
    if (fb.bpp == 8) {
        return settings_applied.palette_mode == 1 ? 16u : 256u;
    }
    if (fb.bpp >= 16) {
        return 65536u;
    }
    return 0;
}

static uint8_t gpu_palette_entry_color(uint32_t index) {
    if (fb.bpp == 4) {
        if (index >= 16u) {
            return color_gray_light;
        }
        return nearest_color(vga_ega16_colors[index].r,
                             vga_ega16_colors[index].g,
                             vga_ega16_colors[index].b);
    }

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

/*
 * Palette scrolling is row-aligned.
 *
 * A row of the swatch grid is GPU_GRID_COLS consecutive palettes, so every
 * scroll offset is a multiple of it - otherwise the strip reflows and the
 * on-screen rows no longer line up with palette rows (a step of one row
 * shifts every swatch sideways instead of downward).
 *
 * The furthest offset is the top row that still keeps the final palette row
 * on screen, which is NOT total - GPU_GRID_PAGE_ENTRIES: 65536 - 288 = 65248
 * and 65248 % 24 == 16. That one-off-by-a-partial-row caused both reported
 * symptoms:
 *
 *   - dragging the thumb to the bottom snapped to 65232, so the last 16
 *     palettes (65520..65535) were unreachable - the view "does not come in
 *     the very last palette" - and the page counter stuck at 227/228;
 *   - the down arrow clamped to 65248, i.e. 16 entries past a row boundary,
 *     so instead of scrolling one row the whole strip slid two thirds of a
 *     row sideways. 65256 (= 2719 rows) is the smallest row-aligned offset
 *     that shows entry 65535, so the tail row ends with a few no-entry cells
 *     - which is the honest picture: the palette ends mid-row.
 */
static uint32_t gpu_palette_max_scroll(void) {
    uint32_t total = gpu_palette_entry_count();

    if (total <= GPU_GRID_PAGE_ENTRIES) {
        return 0;
    }
    return ((total - 1u) / GPU_GRID_COLS + 1u - GPU_GRID_ROWS) * GPU_GRID_COLS;
}

/* Snap any offset to a whole row and keep it inside the palette. */
static uint32_t gpu_palette_clamp_scroll(uint32_t scroll) {
    uint32_t max_scroll = gpu_palette_max_scroll();

    scroll -= scroll % GPU_GRID_COLS;
    return scroll > max_scroll ? max_scroll : scroll;
}

/* Move the view by whole rows; negative scrolls up, and the ends clamp. */
static void gpu_palette_scroll_rows(int rows) {
    uint32_t step = (uint32_t)(rows < 0 ? -rows : rows) * GPU_GRID_COLS;

    if (rows < 0) {
        gpu_palette_scroll = step > gpu_palette_scroll ? 0u : gpu_palette_scroll - step;
    } else {
        gpu_palette_scroll = gpu_palette_clamp_scroll(gpu_palette_scroll + step);
    }
}

static uint32_t gpu_palette_page_count(void) {
    uint32_t total = gpu_palette_entry_count();

    return (total + GPU_GRID_PAGE_ENTRIES - 1u) / GPU_GRID_PAGE_ENTRIES;
}

/*
 * The page holding the last entry on screen.
 *
 * 65536 is not a multiple of the 288-entry page (227 pages plus 160
 * entries), so the final page is short. A counter derived from the TOP row
 * therefore stops one page early - it read 227/228 even scrolled all the way
 * down; counting from the bottom row reaches page_count/page_count at the
 * end.
 */
static uint32_t gpu_palette_visible_page(void) {
    uint32_t total = gpu_palette_entry_count();
    uint32_t last_visible = gpu_palette_scroll + GPU_GRID_PAGE_ENTRIES - 1u;
    uint32_t page_count = gpu_palette_page_count();
    uint32_t page;

    if (page_count == 0) {
        return 0;
    }
    if (last_visible >= total) {
        last_visible = total - 1u;
    }
    page = (last_visible / GPU_GRID_PAGE_ENTRIES) + 1u;
    return page > page_count ? page_count : page;
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

static void settings_handle_mouse(void) {
    if (!windows[APP_SETTINGS].open || active_window != APP_SETTINGS || !mouse.left || mouse.prev_left) {
        return;
    }

    Window *window = &windows[APP_SETTINGS];
    if (button_clicked(window->x + 14, window->y + 56, 70, 20)) {
        settings_tab = 0;
    } else if (button_clicked(window->x + 90, window->y + 56, 80, 20)) {
        settings_tab = 1;
    } else if (settings_tab == 0) {
        if (button_clicked(window->x + 178, window->y + 86, 20, 18) && live_palette_supported((uint8_t)((settings_pending.palette_mode + 2) % 3))) {
            settings_pending.palette_mode = (settings_pending.palette_mode + 2) % 3;
        } else if (button_clicked(window->x + 346, window->y + 86, 20, 18) && live_palette_supported((uint8_t)((settings_pending.palette_mode + 1) % 3))) {
            settings_pending.palette_mode = (settings_pending.palette_mode + 1) % 3;
        } else if (button_clicked(window->x + 178, window->y + 116, 20, 18) && live_resolution_supported((uint8_t)((settings_pending.resolution_mode + 4) % 5))) {
            settings_pending.resolution_mode = (settings_pending.resolution_mode + 4) % 5;
        } else if (button_clicked(window->x + 346, window->y + 116, 20, 18) && live_resolution_supported((uint8_t)((settings_pending.resolution_mode + 1) % 5))) {
            settings_pending.resolution_mode = (settings_pending.resolution_mode + 1) % 5;
        } else if (button_clicked(window->x + 20, window->y + 148, 12, 12)) {
            settings_pending.widescreen = !settings_pending.widescreen;
        }
    } else {
        if (button_clicked(window->x + 170, window->y + 86, 20, 18)) {
            settings_pending.background_mode = (settings_pending.background_mode + 9) % 10;
        } else if (button_clicked(window->x + 358, window->y + 86, 20, 18)) {
            settings_pending.background_mode = (settings_pending.background_mode + 1) % 10;
        }
    }

    if (settings_dirty()) {
        if (button_clicked(window->x + 120, window->y + window->h - 36, 60, 22)) {
            settings_pending = settings_applied;
            close_window(APP_SETTINGS);
        } else if (button_clicked(window->x + 188, window->y + window->h - 36, 60, 22)) {
            apply_settings();
        } else if (button_clicked(window->x + 256, window->y + window->h - 36, 60, 22)) {
            apply_settings();
            close_window(APP_SETTINGS);
        }
    }
}

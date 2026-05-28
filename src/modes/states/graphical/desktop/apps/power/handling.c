static bool power_handle_overlay_mouse(void) {
    if (!power_menu_open || !mouse.left || mouse.prev_left) {
        return false;
    }

    int x = 240;
    int y = 184;
    if (point_in_rect(mouse.x, mouse.y, x, y, 160, 24)) {
        shutdown_system();
    } else if (point_in_rect(mouse.x, mouse.y, x, y + 32, 160, 24)) {
        restart_system();
    } else if (point_in_rect(mouse.x, mouse.y, x, y + 64, 160, 24)) {
        serial_trace("INFO", "power menu halt requested");
        cpu_halted_overlay = true;
    } else if (point_in_rect(mouse.x, mouse.y, x, y + 92, 160, 20) ||
               !point_in_rect(mouse.x, mouse.y, 212, 150, 216, 158)) {
        power_menu_open = false;
    }
    return true;
}

static void power_handle_mouse(void) {
    if (!windows[APP_POWER].open || active_window != APP_POWER || !mouse.left || mouse.prev_left) {
        return;
    }

    Window *window = &windows[APP_POWER];
    int x = window->x + 20;
    int y = window->y + 36;
    if (point_in_rect(mouse.x, mouse.y, x, y, 160, 24)) {
        shutdown_system();
    } else if (point_in_rect(mouse.x, mouse.y, x, y + 32, 160, 24)) {
        restart_system();
    } else if (point_in_rect(mouse.x, mouse.y, x, y + 64, 160, 24)) {
        serial_trace("INFO", "power window halt requested");
        cpu_halted_overlay = true;
    } else if (point_in_rect(mouse.x, mouse.y, x, y + 96, 160, 24)) {
        close_window(APP_POWER);
    }
}

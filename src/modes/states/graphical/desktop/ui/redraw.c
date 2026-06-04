// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: redraw.c, desktop redraw timing logic.

// This repository is licensed under the GNU General Public License.

static bool desktop_should_redraw(void) {
    uint32_t second = timer_ticks / TIMER_HZ;
    uint32_t perf_phase = timer_ticks / PERF_UPDATE_TICKS;
    uint32_t blink_phase = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;

    if (last_desktop_redraw_input_tick != last_input_tick) {
        return true;
    }
    if (last_desktop_redraw_second != second) {
        return true;
    }
    if (windows[APP_TASK_MANAGER].open && task_manager_tab == 1 && last_desktop_redraw_perf_phase != perf_phase) {
        return true;
    }
    if (windows[APP_CMD].open && last_desktop_redraw_terminal_blink != blink_phase) {
        return true;
    }
    if (debug_overlay_open && last_desktop_redraw_terminal_blink != blink_phase) {
        return true;
    }
    if (windows[APP_TASK_MANAGER].open && task_manager_tab == 2 && task_manager_gpu_scroll_drag) {
        return true;
    }
    if (windows[APP_SNAKE].open && last_desktop_redraw_snake_tick != snake_last_step_tick) {
        return true;
    }
    if (menu_open || context_menu_open || desktop_icon_menu_open || start_app_menu_open || power_menu_open ||
        (windows[APP_GAME_CENTER].open && active_window == APP_GAME_CENTER)) {
        return true;
    }
    return false;
}

static void mark_desktop_redrawn(void) {
    last_desktop_redraw_input_tick = last_input_tick;
    last_desktop_redraw_second = timer_ticks / TIMER_HZ;
    last_desktop_redraw_perf_phase = timer_ticks / PERF_UPDATE_TICKS;
    last_desktop_redraw_terminal_blink = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;
    last_desktop_redraw_snake_tick = snake_last_step_tick;
}

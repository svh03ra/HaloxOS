// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: redraw.c, desktop redraw timing logic.

// This repository is licensed under the GNU General Public License.

static bool cursor_capture_valid(void);
static void cursor_render_motion_only(void);

/*
 * "Something other than input changed" test.
 *
 * Split out of desktop_should_redraw() so the pointer-only fast path can
 * ask "is the desktop image still valid?" without the input-tick term:
 * a moving pointer IS input, and treating it like a wallpaper repaint is
 * exactly the cost this avoids.
 */
static bool desktop_dirty_without_input(void) {
    uint32_t second = timer_ticks / TIMER_HZ;
    uint32_t perf_phase = timer_ticks / PERF_UPDATE_TICKS;
    uint32_t blink_phase = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;

    if (window_fade_active || desktop_select_dragging) {
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
    /* Live animated apps redraw every tick so they stay smooth with
     * the cursor idle - not only while the mouse moves. */
    if ((windows[APP_3D_BOX].open || windows[APP_FIRECRACKER].open ||
         (windows[APP_RUN_GAME].open && run_state < 3) ||
         doom_app_needs_redraw()) &&
        last_desktop_redraw_demos_tick != timer_ticks) {
        return true;
    }
    if (menu_open || context_menu_open || desktop_icon_menu_open || start_app_menu_open || power_menu_open ||
        (windows[APP_GAME_CENTER].open && active_window == APP_GAME_CENTER)) {
        return true;
    }
    return false;
}

/* Does the desktop need a repaint this frame at all? */
static bool desktop_should_redraw(void) {
    if (last_desktop_redraw_input_tick != last_input_tick) {
        return true;
    }
    return desktop_dirty_without_input();
}

/*
 * Should this frame repaint the whole desktop, or is the pointer the only
 * thing that moved? A pointer-only frame restores the saved background,
 * redraws the pointer and scans out two small boxes instead of the whole
 * screen - the difference between a cursor that glides and one that steps
 * on a 486-class machine or a cycle-accurate emulator.
 *
 * Every condition below is a reason a partial repaint would be wrong:
 *   - the desktop itself changed (animation, blink, second tick, menus),
 *   - a key was handled this tick (text, focus or menu state may differ),
 *   - a button is down (drag, rubber-band select, click highlight),
 *   - the pointer did not actually move,
 *   - the saved background is missing or belongs to another shape.
 */
static bool desktop_cursor_only_redraw(void) {
    if (last_desktop_redraw_input_tick == last_input_tick) {
        return false;
    }
    if (last_key_input_tick == timer_ticks) {
        return false;
    }
    if (mouse.left || mouse.right || mouse.middle) {
        return false;
    }
    if (mouse.x == last_redraw_mouse_x && mouse.y == last_redraw_mouse_y) {
        return false;
    }
    if (desktop_dirty_without_input()) {
        return false;
    }
    return cursor_capture_valid();
}

/*
 * Cheap repaint: only the pointer moved.
 */
static void desktop_cursor_only_frame(void) {
    cursor_render_motion_only();
}

static void mark_desktop_redrawn(void) {
    last_desktop_redraw_input_tick = last_input_tick;
    last_redraw_mouse_x = mouse.x;
    last_redraw_mouse_y = mouse.y;
    last_desktop_redraw_second = timer_ticks / TIMER_HZ;
    last_desktop_redraw_perf_phase = timer_ticks / PERF_UPDATE_TICKS;
    last_desktop_redraw_terminal_blink = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;
    last_desktop_redraw_snake_tick = snake_last_step_tick;
    last_desktop_redraw_demos_tick = timer_ticks;
    doom_app_mark_presented();
}

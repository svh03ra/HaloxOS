// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: redraw.c, desktop redraw timing logic.

// This repository is licensed under the GNU General Public License.

static bool cursor_capture_valid(void);
static void cursor_render_motion_only(void);
static void cursor_render_clock_only(void);

/*
 * "Something other than input or the clock changed" test.
 *
 * Split out of desktop_should_redraw() so the two cheap paths below can
 * ask "is the desktop image still valid?" without the input-tick term:
 * a moving pointer IS input, and the clock ticks on its own once a second -
 * treating either of them like a wallpaper repaint is exactly the cost this
 * avoids. The clock is deliberately not part of this test: it has its own
 * box-sized path (desktop_clock_only_redraw), so its tick never lands here.
 */
static bool desktop_dirty_without_input(void) {
    uint32_t perf_phase = timer_ticks / PERF_UPDATE_TICKS;
    uint32_t blink_phase = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;

    /* A button hover fade is an animation that runs while the pointer is
     * perfectly still - the exact case this gate was written to skip.
     * draw_hover_fade_rect raises the flag on every frame it paints a
     * partial dither, and init.c clears it just before each full repaint,
     * so the flag means "the frame just painted was mid-fade": keep
     * scheduling full repaints until the fade completes. Without this the
     * dither froze at whatever density the pointer-entry frame painted. */
    if (ui_hover_fade_active) {
        return true;
    }
    if (window_fade_active || desktop_select_dragging) {
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

/*
 * Does the desktop need a repaint this frame at all?
 *
 * "Input changed" is a flag set by the input driver for every real change
 * and cleared only by mark_desktop_redrawn(), not a comparison of tick
 * stamps. Comparing timer_ticks could not tell two edges inside the same
 * 16.6ms tick apart, so the second one - typically a button release - was
 * silently dropped and its pressed look stayed on screen.
 */
static bool desktop_should_redraw(void) {
    if (ui_input_dirty) {
        return true;
    }
    /* Second net for motion: the pointer position is compared with the one
     * the last painted frame ended at, so a packet whose flag write raced
     * with the end of that frame still gets its repaint. */
    if (mouse.x != last_redraw_mouse_x || mouse.y != last_redraw_mouse_y) {
        return true;
    }
    /* Third net: the bookkeeping above records the LIVE pointer, but the
     * pixels on screen come from render_cursor()'s own snapshot. IRQ12 can
     * move the pointer between that snapshot and the end of the frame, and
     * the write-back below then agrees with a cursor that was never painted
     * at the new spot - the on-screen pointer freezes until the next input.
     * Comparing the PAINTED position refuses to book such a frame as done. */
    if (mouse.x != cursor_painted_x || mouse.y != cursor_painted_y) {
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
 *   - the desktop itself changed (animation, blink, clock tick, menus),
 *   - a key was handled this tick (text, focus or menu state may differ),
 *   - a button is down (drag, rubber-band select, click highlight),
 *   - the pointer did not actually move,
 *   - the saved background is missing or belongs to another shape.
 */
static bool desktop_cursor_only_redraw(void) {
    if (!ui_input_dirty &&
        mouse.x == last_redraw_mouse_x && mouse.y == last_redraw_mouse_y) {
        return false;
    }
    if (last_key_input_tick == timer_ticks) {
        return false;
    }
    /* A press or a release changes what the widgets look like: whatever is
     * on screen was painted for the other button state. Refusing only
     * while a button is *down* let the release frame take this path, which
     * is how a released button (and a Start menu closed by that click)
     * stayed painted until some unrelated input forced a full repaint. */
    if (ui_pointer_state_changed) {
        return false;
    }
    if (mouse.left || mouse.right || mouse.middle) {
        return false;
    }
    if (mouse.x == last_redraw_mouse_x && mouse.y == last_redraw_mouse_y) {
        return false;
    }
    /* The cursor on screen may not sit where the live pointer is (IRQ12
     * raced the previous paint). A pointer-only frame would then restore
     * and present the wrong boxes and never repaint the visible cursor. */
    if (mouse.x != cursor_painted_x || mouse.y != cursor_painted_y) {
        return false;
    }
    if (desktop_dirty_without_input()) {
        return false;
    }
    /* The saved background is only the truth if the pointer stayed off
     * every pointer-reactive widget: entering or leaving one changes what
     * it draws (hover fill, dithered fade, hand cursor), which only a full
     * repaint can show. */
    if (hover_widget_hit(last_redraw_mouse_x, last_redraw_mouse_y) ||
        hover_widget_hit(mouse.x, mouse.y)) {
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

/*
 * Clock-only repaint.
 *
 * The taskbar clock reads hh:mm:ss, so something has to touch it once a
 * second - but a whole desktop recomposite plus a 640x480 scanout every
 * second is a lot of work for two lines of text, and it is exactly what a
 * potato (or a cycle-accurate emulator) feels. This path is taken only when
 * nothing else changed, no key was handled this tick, no button is down and
 * no overlay owns the screen; anything else wins and falls back to a full
 * repaint.
 */
static bool desktop_clock_only_redraw(void) {
    if (last_desktop_redraw_second == timer_ticks / TIMER_HZ) {
        return false;
    }
    if (last_key_input_tick == timer_ticks) {
        return false;
    }
    if (mouse.left || mouse.right || mouse.middle || debug_overlay_open) {
        return false;
    }
    if (desktop_dirty_without_input()) {
        return false;
    }
    return true;
}

static void desktop_clock_only_frame(void) {
    cursor_render_clock_only();
}

/*
 * Bookkeeping after a frame.
 *
 * clock_painted distinguishes the frames that repaint the clock box from
 * the pointer-only frame, which does not. A pointer frame must NOT claim
 * the clock's second: the next tick would then see the second as already
 * handled and skip it, so a second that ticked while the pointer was moving
 * would never be drawn - and with the pointer moving constantly the clock
 * would stand still for as long as it kept moving.
 */
static void mark_desktop_redrawn(bool clock_painted) {
    ui_input_dirty = false;
    ui_pointer_state_changed = false;
    last_redraw_mouse_x = mouse.x;
    last_redraw_mouse_y = mouse.y;
    if (clock_painted) {
        last_desktop_redraw_second = timer_ticks / TIMER_HZ;
    }
    last_desktop_redraw_perf_phase = timer_ticks / PERF_UPDATE_TICKS;
    last_desktop_redraw_terminal_blink = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;
    last_desktop_redraw_snake_tick = snake_last_step_tick;
    last_desktop_redraw_demos_tick = timer_ticks;
    doom_app_mark_presented();
}

// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: redraw.c, desktop redraw timing logic.

// This repository is licensed under the GNU General Public License.

static bool cursor_capture_valid(void);
static void cursor_render_motion_only(void);
static void cursor_render_clock_only(void);
/* Defined in render.c / shell.c, both included after this file in the
 * kernel amalgamation. */
static void render_fps_overlay(void);
static void debug_fps_overlay_rect(int *x, int *y, int *w, int *h);
static void render_taskbar_clock(void);

static bool desktop_rect_overlaps(int ax, int ay, int aw, int ah,
                                  int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

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
    if (windows[APP_STRESS_TEST].open && stress_field_focus >= 0 &&
        last_desktop_redraw_terminal_blink != blink_phase) {
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
    /* The Stress Test countdown changes once per second and GPU mode is a
     * live animation, so repaint while it runs even with the pointer idle.
     * GPU noise needs 30 Hz to look like load; the other modes only need the
     * once-a-second countdown. */
    if (windows[APP_STRESS_TEST].open && stress_test_running &&
        last_desktop_redraw_stress_phase !=
            timer_ticks / (stress_test_mode == 2u ? 2u : 6u)) {
        return true;
    }
    /* Live animated apps redraw every tick so they stay smooth with
     * the cursor idle - not only while the mouse moves. */
    if ((windows[APP_3D_BOX].open || windows[APP_FIRECRACKER].open ||
         windows[APP_COIN_RUN].open || doom_app_needs_redraw()) &&
        last_desktop_redraw_demos_tick != timer_ticks) {
        return true;
    }
    /* Menus used to force a full repaint on every frame they stayed open,
     * even with a still pointer.  That is a 60 Hz whole-desktop recomposite
     * plus scanout just to keep a static menu on screen, and it made the
     * Task Manager report the menu itself as heavy CPU/GPU load.  A menu
     * repaints once when it opens (ui_input_dirty), on pointer motion, and
     * while its hover fade is still animating (ui_hover_fade_active), so
     * the unconditional term is gone.  The Game Center keeps its animation
     * clock. */
    if (windows[APP_GAME_CENTER].open && active_window == APP_GAME_CENTER) {
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
    if (desktop_dirty_without_input()) {
        return false;
    }
    /* The saved background is only the truth while the pointer stays over
     * the same pointer-reactive widget: entering or leaving one changes
     * what it draws (hover fill, dithered fade, hand cursor), and only a
     * full repaint can show that. Moving WITHIN one widget changes
     * nothing - its hover fill is already painted - so the fast path is
     * allowed when both positions resolve to the same widget. The fade
     * itself is excluded by desktop_dirty_without_input() above, which
     * sees ui_hover_fade_active. */
    {
        int old_hover = hover_widget_index_at(last_redraw_mouse_x,
                                              last_redraw_mouse_y);
        int new_hover = hover_widget_index_at(mouse.x, mouse.y);

        if (old_hover != new_hover) {
            return false;
        }
    }
    /* Note for the position test that used to sit here: the live pointer
     * differs from cursor_painted_x/y by definition once it has moved, so
     * refusing that case disabled the pointer-only path for every real
     * motion and every move became a full desktop recomposite - the 57%
     * CPU reading while the cursor moves. cursor_render_motion_only()
     * restores the save-under box at the PAINTED position and redraws the
     * pointer at the live one, so a paint that raced IRQ12 is handled by
     * the fast path itself and is not a reason to fall back to a full
     * repaint. */
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
 * DOOM-only repaint.
 *
 * While the game is captured the pointer is hidden and the engine produces
 * a new 320x200 frame at 35 Hz.  The old path answered every one of those
 * frames with a complete desktop recomposite (wallpaper, icons, every
 * window, taskbar) plus a full 640x480 scan-out - work that cannot change
 * the game pixels but dominates the frame budget on a slow machine.
 *
 * This path repaints just the DOOM window and scans out just its rectangle.
 * It is deliberately conservative: any real input, clock tick, menu,
 * animation, debug overlay or overlapping test window falls back to the
 * full repaint.  Pointer motion is consumed by the game's pointer lock
 * (update_doom clears the desktop dirty flag for it), so moving the mouse
 * does not defeat the fast path.
 */
static bool desktop_doom_only_redraw(void) {
    if (system_state != STATE_DESKTOP || debug_overlay_open) {
        return false;
    }
    if (!windows[APP_DOOM].open || active_window != APP_DOOM) {
        return false;
    }
    if (!doom_pointer_locked() || !doom_app_needs_redraw()) {
        return false;
    }
    /* While the pointer is locked, keyboard and mouse belong to the game:
     * a held movement key, a fire click or a mouse twitch changes DOOM's
     * state, not the desktop.  These used to force a full desktop
     * recomposite for every repeat - exactly the stutter felt while
     * playing.  A middle-click unlock clears the lock, and any shortcut
     * that opens the debugger or another window fails the gates below. */
    if (ui_pointer_state_changed) {
        return false;
    }
    /* The taskbar clock and the terminal cursor blink used to force a full
     * desktop recomposite here.  The clock now gets its own box repaint in
     * desktop_doom_only_frame(), and a terminal that is not open has no
     * blink to show - forcing a full frame for it cost two repaints a
     * second in the middle of the game for nothing. */
    if (windows[APP_CMD].open &&
        last_desktop_redraw_terminal_blink != timer_ticks / TERMINAL_CURSOR_BLINK_TICKS) {
        return false;
    }
    if (windows[APP_TASK_MANAGER].open &&
        last_desktop_redraw_perf_phase != timer_ticks / PERF_UPDATE_TICKS) {
        return false;
    }
    if (ui_hover_fade_active || window_fade_active || desktop_select_dragging) {
        return false;
    }
    if (menu_open || context_menu_open || desktop_icon_menu_open ||
        start_app_menu_open || power_menu_open || taskbar_menu_open) {
        return false;
    }
    if (count_open_test_windows() > 0) {
        return false;
    }
    /* Other live animations may be visible around the DOOM window. */
    if (windows[APP_3D_BOX].open || windows[APP_FIRECRACKER].open ||
        windows[APP_SNAKE].open || windows[APP_COIN_RUN].open ||
        (windows[APP_STRESS_TEST].open && stress_test_running)) {
        return false;
    }
    if (settings_applied.window_trails) {
        for (int i = 0; i < TRAIL_COUNT; ++i) {
            if (trail_w[i] > 0 && (uint32_t)(timer_ticks - trail_tick[i]) < 6u) {
                return false;
            }
        }
    }
    return true;
}

static void desktop_doom_only_frame(void) {
    int clock_x = clock_widget_x();
    int clock_y = clock_widget_y();
    bool clock_painted = false;
    int fps_x = 0;
    int fps_y = 0;
    int fps_w = 0;
    int fps_h = 0;

    render_app_window(APP_DOOM);

    /* A breakpoint catch can open the debugger while the game frame was
     * being drawn.  The overlay needs the full-screen present then. */
    if (debug_overlay_open) {
        render_debug_overlay();
        present();
        return;
    }

    /* The FPS box is an overlay, not part of the game frame: draw it after
     * the game and scan out only its rectangle.  Keeping it visible no
     * longer disables this whole fast path (the old gate did, so measuring
     * the frame rate was itself the reason the frame rate fell). */
    if (fps_overlay_on) {
        render_fps_overlay();
        debug_fps_overlay_rect(&fps_x, &fps_y, &fps_w, &fps_h);
    }

    /* The taskbar clock ticks once a second.  Painting only its box keeps
     * that tick from forcing a full desktop recomposite in the middle of
     * the game.  When the game window covers the box there is nothing to
     * paint, but the second is still booked so the check does not repeat. */
    if (last_desktop_redraw_second != timer_ticks / TIMER_HZ) {
        if (!desktop_rect_overlaps(windows[APP_DOOM].x, windows[APP_DOOM].y,
                                   windows[APP_DOOM].w, windows[APP_DOOM].h,
                                   clock_x, clock_y, CLOCK_BOX_W, CLOCK_BOX_H)) {
            render_taskbar_clock();
            clock_painted = true;
        }
        last_desktop_redraw_second = timer_ticks / TIMER_HZ;
    }

    cursor_invalidate_capture();
    render_cursor();
    present_rect(windows[APP_DOOM].x, windows[APP_DOOM].y,
                 windows[APP_DOOM].w, windows[APP_DOOM].h);
    if (fps_overlay_on) {
        present_rect(fps_x, fps_y, fps_w, fps_h);
    }
    if (clock_painted) {
        present_rect(clock_x, clock_y, CLOCK_BOX_W, CLOCK_BOX_H);
    }
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
    last_desktop_redraw_stress_phase = timer_ticks / 6u;
    doom_app_mark_presented();
}

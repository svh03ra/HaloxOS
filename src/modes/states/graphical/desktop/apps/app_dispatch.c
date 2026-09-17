// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: app_dispatch.c, all of dispatches handling.

// This repository is licensed under the GNU General Public License.

static void render_app_window(AppId app) {
    Window *window = &windows[app];
    if (!window->open) {
        return;
    }

    /* Debugger breakpoint: AUTO-CONTINUE mode. Once armed (after
     * 'continue'), every rendered frame that touches a targeted app
     * appends a live trace to the debugger terminal - at most one
     * catch per timer frame. The system never pauses. Frame numbers
     * count from the FIRST catch (frame #1), not from when the
     * breakpoint was typed: each catch's frame number is relative to
     * the previous one, so the log reads #1, #2, #3... as new catches
     * fire. */
    if (debug_bp_armed && debug_bp_mask != 0 &&
        (debug_bp_mask & 0x8000u || (debug_bp_mask & (uint16_t)(1u << app)) != 0) &&
        debug_bp_last_catch_tick != timer_ticks) {
        if (debug_bp_frame_base == 0) {
            debug_bp_frame_base = fps_frames_total - 1; /* first catch = #1 */
        }
        debug_bp_catch(app, fps_frames_total - debug_bp_frame_base);
    }

    draw_window_chrome(window);

    /* Clip every app's drawing to this window's client area so scenes
     * (Title Run!, firecracker, 3D box) can never bleed outside. */
    set_window_clip(window->x + 1, window->y + 19, window->w - 2, window->h - 20);

    switch (app) {
        case APP_NOTEPAD: render_notepad(window); break;
        case APP_CMD: render_terminal(&cmd_term, window->x + 8, window->y + 26, window->w - 16, window->h - 34); break;
        case APP_PAINT: render_paint(window); break;
        case APP_EXPLORER: render_explorer(window); break;
        case APP_SNAKE: render_snake(window); break;
        case APP_GUESS: render_guess(window); break;
        case APP_MINES: render_mines(window); break;
        case APP_GAME_CENTER: render_game_center(window); break;
        case APP_POWER: render_power(window); break;
        case APP_SETTINGS: render_settings(window); break;
        case APP_TASK_MANAGER: render_task_manager(window); break;
        case APP_DEMO_CENTER: render_demo_center(window); break;
        case APP_3D_BOX: render_3d_box(window); break;
        case APP_FIRECRACKER: render_firecracker(window); break;
        case APP_RUN_GAME: render_run(window); break;
        case APP_DOOM: render_doom(window); break;
    }

    clear_window_clip();
}

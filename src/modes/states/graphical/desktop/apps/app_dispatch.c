// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: app_dispatch.c, all of dispatches handling.

// This repository is licensed under the GNU General Public License.

static void render_app_window(AppId app) {
    Window *window = &windows[app];
    if (!window->open) {
        return;
    }

    /* Debugger breakpoint: AUTO-CONTINUE mode. Once armed (after
     * 'continue'), the next rendered frame that touches a targeted app
     * appends a live trace to the debugger terminal. debug_bp_caught is
     * a one-catch-per-frame latch: the debugger's own blink repaints
     * must not re-catch the frame that already opened the overlay. A
     * real desktop input (key or mouse click) clears the latch, so the
     * next frame - for example the frame produced by clicking a
     * Minesweeper cell - catches once and logs the next frame number. */
    if (debug_bp_armed && !debug_bp_caught && !debug_overlay_open &&
        debug_bp_mask != 0 &&
        (debug_bp_mask & 0x80000000u || (debug_bp_mask & (uint32_t)(1u << app)) != 0)) {
        debug_bp_catch(app);
    }

    /* Only the topmost window under the pointer reacts to it: this window's
     * buttons, rows and close box must not light the hand cursor or show a
     * pressed fill "through" the window that is painted above them. */
    ui_window_interactive = desktop_window_owns_pointer(window);
    if ((int)app == active_window) {
        draw_window_drop_shadow(window);
    }
    draw_window_chrome(window);

    /* Clip every app's drawing to this window's client area so scenes
     * (firecracker, 3D box) can never bleed outside. */
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
        case APP_DOOM: render_doom(window); break;
        case APP_COIN_RUN: render_coin_run(window); break;
        case APP_STRESS_TEST: render_stress(window); break;
    }

    clear_window_clip();
    ui_window_interactive = true;
}

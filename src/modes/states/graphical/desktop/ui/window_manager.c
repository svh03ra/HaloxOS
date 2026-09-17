// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: window_manager.c, window management logic.

// This repository is licensed under the GNU General Public License.

static int app_window_width(AppId app) {
    switch (app) {
        case APP_SETTINGS: return 400;
        case APP_TASK_MANAGER: return 440;
        case APP_POWER: return 220;
        case APP_GAME_CENTER: return 360;
        case APP_DEMO_CENTER: return 360;
        case APP_3D_BOX: return 300;
        case APP_FIRECRACKER: return 340;
        case APP_RUN_GAME: return 420;
        case APP_PAINT: return 368;
        case APP_EXPLORER: return 336;
        case APP_CMD: return 420;
        case APP_DOOM: return 336;   /* client 320x240: DOOM's 320x200 at the exact DOS 4:3 */
        default: return 300;
    }
}

static int app_window_height(AppId app) {
    switch (app) {
        case APP_SETTINGS: return 260;
        case APP_TASK_MANAGER: return 360;
        case APP_POWER: return 160;
        case APP_GAME_CENTER: return 306;
        case APP_DEMO_CENTER: return 230;
        case APP_3D_BOX: return 280;
        case APP_FIRECRACKER: return 260;
        case APP_RUN_GAME: return 300;
        case APP_MINES: return 250;
        case APP_PAINT: return 290;
        case APP_EXPLORER: return 220;
        case APP_DOOM: return 272;   /* 24 + 240 + 8: client = 320x240 */
        default: return 200;
    }
}

/*
 * Keep the button of the app that was just opened or switched to on the
 * taskbar.
 *
 * The row shows TASKBAR_APP_SLOTS buttons starting at taskbar_scroll, so an
 * app activated from the Start menu, by a keyboard shortcut or by another
 * app's own logic can fall outside that window - leaving a running app with
 * no visible button and no obvious sign of which app you are in.
 *
 * When that happens the window slides BACK so the active app takes the last
 * slot. Sliding it forward instead (taskbar_scroll = app) would be a one-line
 * fix, but with a late app - 3D Box, Demo Center, DOOM - it would leave the
 * row showing that single button and four empty slots while five other apps
 * are still open. Sliding back keeps every visible slot filled.
 *
 * Scrolling with the overflow arrows does NOT re-reveal: you can scroll away
 * from the active app and stay there until you activate something again.
 */
static void taskbar_reveal_app(int app) {
    int idx = taskbar_scroll;
    int slot = 0;
    int start;
    int filled;

    if (app < 0 || app >= APP_COUNT) {
        return;
    }

    /* Already inside the visible window? Leave the scroll alone. */
    for (; slot < TASKBAR_APP_SLOTS && idx < APP_COUNT; ++idx) {
        if (!windows[idx].open) continue;
        if (idx == app) {
            return;
        }
        ++slot;
    }

    start = app;
    filled = 0;
    for (int i = app; i >= 0 && filled < TASKBAR_APP_SLOTS; --i) {
        if (!windows[i].open) continue;
        start = i;
        ++filled;
    }
    taskbar_scroll = start;
}

static void open_window(AppId app) {
    Window *window = &windows[app];
    menu_open = false;
    context_menu_open = false;
    desktop_icon_menu_open = false;
    start_app_menu_open = false;
    task_manager_confirm_kill = false;
    if (app == APP_POWER) {
        power_menu_open = true;
        active_window = -1;
        serial_trace("INFO", "Power option menu opened");
        return;
    }
    if (!window->open) {
        window->open = true;
        window->title = app == APP_GAME_CENTER ? "Game Center" :
                        (app == APP_TASK_MANAGER ? "Task Manager" :
                        (app == APP_DEMO_CENTER ? "Demo Center" :
                        (app == APP_3D_BOX ? "3D Box" :
                        (app == APP_FIRECRACKER ? "Firecracker" :
                        (app == APP_RUN_GAME ? "Run! Run" :
                        (app == APP_DOOM ? "DOOM" : app_titles[app]))))));
        window->w = app_window_width(app);
        window->h = app_window_height(app);
        window->x = 70 + app * 18;
        window->y = 40 + app * 12;
        if (window->x + window->w > OS_WIDTH - 10) {
            window->x = 20;
        }
        if (window->y + window->h > OS_HEIGHT - TASKBAR_H - 10) {
            window->y = 40;
        }
    }

    if (app == APP_SNAKE) {
        reset_snake();
    } else if (app == APP_GUESS) {
        reset_guess();
    } else if (app == APP_MINES) {
        mines_place();
    } else if (app == APP_SETTINGS) {
        settings_pending = settings_applied;
        settings_tab = 0;
    } else if (app == APP_TASK_MANAGER) {
        task_manager_tab = 0;
        task_manager_selected_process = 0;
        task_manager_confirm_kill = false;
        task_manager_kill_target = -1;
    } else if (app == APP_RUN_GAME) {
        reset_run();
    } else if (app == APP_3D_BOX) {
        box3d_mode = 0;
        box3d_angle = 0;
        box3d_angle_x = 0;
        box3d_last_tick = timer_ticks;
    } else if (app == APP_FIRECRACKER) {
        for (int i = 0; i < FIRE_PARTICLES; ++i) {
            fire_alive[i] = false;
        }
        fire_last_tick = timer_ticks;
    } else if (app == APP_DOOM) {
        /* Boot is driven by the DOOM renderer's first frame (doom_app.c),
         * NOT here: the engine must never be started twice, and the
         * render-side path is the single owner of the boot trigger. */
    }

    active_window = app;
    taskbar_reveal_app((int)app);
    serial_trace_concat("INFO", "Application Opened - ", app_titles[app]);
}

static void set_active_window(AppId app) {
    active_window = app;
    taskbar_reveal_app((int)app);
    menu_open = false;
    context_menu_open = false;
    desktop_icon_menu_open = false;
    start_app_menu_open = false;
}

static void close_window(AppId app) {
    if (app == APP_POWER) {
        power_menu_open = false;
        return;
    }
    if (windows[app].open && settings_applied.window_fade && app != APP_SETTINGS) {
        window_fade_active = true;
        window_fade_app = app;
        window_fade_tick = timer_ticks;
        window_fade_x = windows[app].x;
        window_fade_y = windows[app].y;
        window_fade_w = windows[app].w;
        window_fade_h = windows[app].h;
    }
    if (windows[app].open) {
        serial_trace_concat("INFO", "Application Closed - ", app_titles[app]);
    }
    if (app == APP_DOOM) {
        /* Shut the engine down so reopening re-probes the WAD. */
        extern void doom_shutdown(void);
        extern void doom_app_reset(void);
        doom_shutdown();
        doom_app_reset();
    }
    windows[app].open = false;
    if (app == APP_TASK_MANAGER) {
        task_manager_confirm_kill = false;
        task_manager_kill_target = -1;
        task_manager_gpu_scroll_drag = false;
    }
    if (active_window == (int)app) {
        active_window = -1;
        for (int i = APP_COUNT - 1; i >= 0; --i) {
            if (windows[i].open) {
                active_window = i;
                break;
            }
        }
        /* The app that just became active must not be hidden by the row's
         * scroll position either. */
        taskbar_reveal_app(active_window);
    }
}

static void open_desktop(void) {
    boot_text_mode = false;
    update_present_maps();
    system_state = STATE_DESKTOP;
    menu_open = false;
    context_menu_open = false;
    cpu_halted_overlay = false;
    shutdown_pending = false;
    active_window = -1;
    task_manager_gpu_scroll_drag = false;
    last_desktop_redraw_input_tick = 0xFFFFFFFFu;
    last_desktop_redraw_second = 0xFFFFFFFFu;
    last_desktop_redraw_perf_phase = 0xFFFFFFFFu;
    last_desktop_redraw_terminal_blink = 0xFFFFFFFFu;
    last_desktop_redraw_snake_tick = 0xFFFFFFFFu;
}

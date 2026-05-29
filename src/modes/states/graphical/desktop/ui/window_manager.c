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
                        (app == APP_TASK_MANAGER ? "Task Manager" : app_titles[app]);
        window->w = (app == APP_SETTINGS) ? 400 :
                    (app == APP_TASK_MANAGER ? 440 :
                    (app == APP_POWER ? 220 :
                    (app == APP_GAME_CENTER ? 360 :
                    (app == APP_PAINT ? 368 :
                    (app == APP_EXPLORER ? 336 :
                    (app == APP_CMD ? 420 : 300))))));
        window->h = (app == APP_SETTINGS) ? 260 :
                    (app == APP_TASK_MANAGER ? 360 :
                    (app == APP_POWER ? 160 :
                    (app == APP_GAME_CENTER ? 230 :
                    (app == APP_MINES ? 250 :
                    (app == APP_PAINT ? 290 :
                    (app == APP_EXPLORER ? 220 : 200))))));
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
    }

    active_window = app;
    serial_trace_concat("INFO", "Application Opened - ", app_titles[app]);
}

static void set_active_window(AppId app) {
    active_window = app;
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

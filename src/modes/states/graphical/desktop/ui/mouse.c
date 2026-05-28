static void handle_desktop_mouse(void) {
    bool pointer_over_window = false;
    bool clicked = mouse.left && !mouse.prev_left;

    if (debug_overlay_open) {
        return;
    }

    if (button_clicked(4, OS_HEIGHT - 24, 46, 18)) {
        menu_open = !menu_open;
        context_menu_open = false;
        desktop_icon_menu_open = false;
        start_app_menu_open = false;
        return;
    }

    for (int app = 0, tab_x = 58; app < APP_COUNT; ++app) {
        if (!windows[app].open) {
            continue;
        }
        if (button_clicked(tab_x, OS_HEIGHT - 24, 84, 18)) {
            set_active_window((AppId)app);
            return;
        }
        tab_x += 88;
    }

    if (power_handle_overlay_mouse()) {
        return;
    }

    if (desktop_icon_menu_open) {
        handle_desktop_icon_menu_click();
        return;
    }
    if (start_app_menu_open) {
        handle_start_app_menu_click();
        return;
    }
    if (menu_open) {
        handle_start_menu_click();
        return;
    }
    if (context_menu_open) {
        handle_context_menu_click();
        return;
    }

    if (mouse.left && drag_desktop_icon >= 0 && drag_desktop_icon < DESKTOP_ICON_COUNT) {
        int box_x;
        int box_y;
        int box_w;
        int box_h;

        desktop_icon_bounds(0, 0, desktop_icon_image(drag_desktop_icon), desktop_icon_label(drag_desktop_icon),
                            &box_x, &box_y, &box_w, &box_h);
        desktop_icons[drag_desktop_icon].x = clampi(mouse.x - desktop_icon_drag_offset_x, 0, OS_WIDTH - box_w);
        desktop_icons[drag_desktop_icon].y = clampi(mouse.y - desktop_icon_drag_offset_y, 0, OS_HEIGHT - TASKBAR_H - box_h);
        return;
    }

    if (mouse.left && drag_window >= 0 && drag_window < APP_COUNT && windows[drag_window].open) {
        Window *window = &windows[drag_window];
        window->x = clampi(mouse.x - drag_offset_x, 0, OS_WIDTH - window->w);
        window->y = clampi(mouse.y - drag_offset_y, 0, OS_HEIGHT - TASKBAR_H - window->h);
        return;
    }

    if (!mouse.left) {
        drag_window = -1;
        if (drag_desktop_icon >= 0 && desktop_icon_drag_moved) {
            save_desktop_icon_positions();
        } else if (desktop_icon_press >= 0 && desktop_icon_press_was_selected) {
            open_window(desktop_icon_app(desktop_icon_press));
            desktop_icon_press = -1;
            drag_desktop_icon = -1;
            desktop_icon_drag_moved = false;
            desktop_icon_press_was_selected = false;
            return;
        }
        desktop_icon_press = -1;
        drag_desktop_icon = -1;
        desktop_icon_drag_moved = false;
        desktop_icon_press_was_selected = false;
    }

    {
        int first = active_window;
        for (int pass = 0; pass <= APP_COUNT; ++pass) {
            int app = pass == 0 ? first : (APP_COUNT - pass);
            Window *window;

            if (app < 0 || app >= APP_COUNT) {
                continue;
            }
            if (pass > 0 && app == first) {
                continue;
            }

            window = &windows[app];
            if (!window->open) {
                continue;
            }
            if (!point_in_rect(mouse.x, mouse.y, window->x, window->y, window->w, window->h)) {
                continue;
            }

            pointer_over_window = true;
            if (clicked) {
                if (point_in_rect(mouse.x, mouse.y, window->x + window->w - 18, window->y + 3, 12, 12)) {
                    close_window((AppId)app);
                    return;
                }
                if (point_in_rect(mouse.x, mouse.y, window->x, window->y, window->w, 18)) {
                    set_active_window((AppId)app);
                    drag_window = app;
                    drag_offset_x = mouse.x - window->x;
                    drag_offset_y = mouse.y - window->y;
                    return;
                }
                if (active_window != app) {
                    set_active_window((AppId)app);
                    return;
                }
            }
            break;
        }
    }

    if (!pointer_over_window && button_right_clicked(0, 0, OS_WIDTH, OS_HEIGHT - TASKBAR_H)) {
        int hit = desktop_icon_hit_test(mouse.x, mouse.y);
        if (hit >= 0) {
            selected_desktop_icon = hit;
            desktop_icon_menu_open = true;
            desktop_icon_menu_target = hit;
            desktop_icon_menu_x = clampi(mouse.x, 0, OS_WIDTH - 120);
            desktop_icon_menu_y = clampi(mouse.y, 0, OS_HEIGHT - TASKBAR_H - 84);
            menu_open = false;
            context_menu_open = false;
            return;
        }
        context_menu_open = true;
        menu_open = false;
        desktop_icon_menu_open = false;
        start_app_menu_open = false;
        context_menu_x = clampi(mouse.x, 0, OS_WIDTH - 120);
        context_menu_y = clampi(mouse.y, 0, OS_HEIGHT - TASKBAR_H - 60);
        return;
    }

    if (!pointer_over_window) {
        int hit = desktop_icon_hit_test(mouse.x, mouse.y);

        if (clicked && hit >= 0) {
            desktop_icon_press = hit;
            desktop_icon_press_x = mouse.x;
            desktop_icon_press_y = mouse.y;
            desktop_icon_drag_offset_x = mouse.x - desktop_icons[hit].x;
            desktop_icon_drag_offset_y = mouse.y - desktop_icons[hit].y;
            desktop_icon_drag_moved = false;
            desktop_icon_press_was_selected = selected_desktop_icon == hit;
            selected_desktop_icon = hit;
            return;
        }

        if (mouse.left && desktop_icon_press >= 0) {
            int dx = mouse.x - desktop_icon_press_x;
            int dy = mouse.y - desktop_icon_press_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > 2 || dy > 2) {
                drag_desktop_icon = desktop_icon_press;
                desktop_icon_drag_moved = true;
            }
            if (drag_desktop_icon >= 0) {
                int box_x;
                int box_y;
                int box_w;
                int box_h;

                desktop_icon_bounds(0, 0, desktop_icon_image(drag_desktop_icon), desktop_icon_label(drag_desktop_icon),
                                    &box_x, &box_y, &box_w, &box_h);
                desktop_icons[drag_desktop_icon].x = clampi(mouse.x - desktop_icon_drag_offset_x, 0, OS_WIDTH - box_w);
                desktop_icons[drag_desktop_icon].y = clampi(mouse.y - desktop_icon_drag_offset_y, 0, OS_HEIGHT - TASKBAR_H - box_h);
                return;
            }
        } else if (clicked && hit < 0) {
            selected_desktop_icon = -1;
            return;
        }
    }

    paint_handle_mouse();
    explorer_handle_mouse();
    mines_handle_mouse();
    game_center_handle_mouse();
    power_handle_mouse();
    settings_handle_mouse();
    task_manager_handle_mouse();
}

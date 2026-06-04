// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: mouse.c, desktop mouse input handling.

// This repository is licensed under the GNU General Public License.

static void handle_desktop_mouse(void) {
    bool pointer_over_window = false;
    bool clicked = mouse.left && !mouse.prev_left;

    if (debug_overlay_open) {
        return;
    }

    if (clicked && desktop_rename_active) {
        int hit = desktop_icon_hit_test(mouse.x, mouse.y);
        if (hit != desktop_rename_icon) {
            desktop_finish_rename(true);
        }
    }

    if (button_clicked(4, OS_HEIGHT - 24, 46, 18)) {
        menu_open = !menu_open;
        context_menu_open = false;
        desktop_icon_menu_open = false;
        start_app_menu_open = false;
        return;
    }

    {
        char dt[40] = {0};
        read_datetime(dt, sizeof(dt));
        int clock_x = OS_WIDTH - (int)strlen_local(dt) * 8 - 8;
        int total_open = 0;
        for (int i = 0; i < APP_COUNT; ++i) {
            if (windows[i].open) ++total_open;
        }
        int open_test = count_open_test_windows();
        if (open_test > 0) ++total_open;
        if (total_open > 4) {
            int arrow_x = clock_x - 4 - 28;
            bool can_left = false;
            for (int i = taskbar_scroll - 1; i >= 0; --i) {
                if (windows[i].open) { can_left = true; break; }
            }
            if (!can_left && taskbar_scroll > APP_COUNT) can_left = true;
            bool can_right = false;
            int scan = taskbar_scroll;
            int slot = 0;
            for (; slot < 4 && scan < APP_COUNT; ++scan) {
                if (!windows[scan].open) continue;
                ++slot;
            }
            if (slot < 4 && open_test > 0) {
                ++slot;
                ++scan;
            }
            for (int i = scan; i < APP_COUNT; ++i) {
                if (windows[i].open) { can_right = true; break; }
            }
            if (!can_right && scan <= APP_COUNT && open_test > 0) can_right = true;
            if (button_clicked(arrow_x, OS_HEIGHT - 24, 12, 18) && can_left) {
                --taskbar_scroll;
                while (taskbar_scroll > 0 && !windows[taskbar_scroll].open) --taskbar_scroll;
                return;
            }
            if (button_clicked(arrow_x + 16, OS_HEIGHT - 24, 12, 18) && can_right) {
                ++taskbar_scroll;
                while (taskbar_scroll < APP_COUNT && !windows[taskbar_scroll].open) ++taskbar_scroll;
                return;
            }
        }
    }
    {
        int idx = taskbar_scroll;
        int slot = 0;
        for (; slot < 4 && idx < APP_COUNT; ++idx) {
            if (!windows[idx].open) continue;
            if (button_clicked(58 + slot * 88, OS_HEIGHT - 24, 84, 18)) {
                set_active_window((AppId)idx);
                return;
            }
            ++slot;
        }
        if (slot < 4 && idx >= APP_COUNT && count_open_test_windows() > 0) {
            if (button_clicked(58 + slot * 88, OS_HEIGHT - 24, 84, 18)) {
                for (int i = test_window_count - 1; i >= 0; --i) {
                    if (test_windows[i].open) { active_test_window = i; break; }
                }
                return;
            }
        }
    }

    if (button_right_clicked(0, OS_HEIGHT - TASKBAR_H, OS_WIDTH, TASKBAR_H)) {
        bool on_button = false;
        if (point_in_rect(mouse.x, mouse.y, 4, OS_HEIGHT - 24, 46, 18)) on_button = true;
        if (!on_button) {
            int idx = taskbar_scroll;
            int slot = 0;
            for (; slot < 4 && idx < APP_COUNT; ++idx) {
                if (!windows[idx].open) continue;
                if (point_in_rect(mouse.x, mouse.y, 58 + slot * 88, OS_HEIGHT - 24, 84, 18)) {
                    on_button = true; break;
                }
                ++slot;
            }
            if (!on_button && slot < 4 && idx >= APP_COUNT && count_open_test_windows() > 0) {
                if (point_in_rect(mouse.x, mouse.y, 58 + slot * 88, OS_HEIGHT - 24, 84, 18)) {
                    on_button = true;
                }
            }
        }
        if (!on_button) {
            char dt[40] = {0};
            read_datetime(dt, sizeof(dt));
            int clock_x = OS_WIDTH - (int)strlen_local(dt) * 8 - 8;
            int total_open = 0;
            for (int i = 0; i < APP_COUNT; ++i) {
                if (windows[i].open) ++total_open;
            }
            int right_zone = total_open > 4 ? clock_x - 32 : clock_x;
            if (mouse.x >= right_zone) on_button = true;
        }
        if (!on_button) {
            taskbar_menu_open = true;
            taskbar_menu_x = clampi(mouse.x, 0, OS_WIDTH - 150);
            taskbar_menu_y = clampi(mouse.y - 16, OS_HEIGHT - TASKBAR_H - 16, OS_HEIGHT - 16);
            menu_open = false;
            context_menu_open = false;
            desktop_icon_menu_open = false;
            start_app_menu_open = false;
        }
        return;
    }

    if (power_handle_overlay_mouse()) {
        return;
    }

    if (taskbar_menu_open) {
        handle_taskbar_menu_click();
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

    if (desktop_select_dragging) {
        if (mouse.left) {
            desktop_select_x2 = mouse.x;
            desktop_select_y2 = mouse.y;
            int rx = desktop_select_x1 < desktop_select_x2 ? desktop_select_x1 : desktop_select_x2;
            int ry = desktop_select_y1 < desktop_select_y2 ? desktop_select_y1 : desktop_select_y2;
            int rw = (desktop_select_x1 > desktop_select_x2 ? desktop_select_x1 : desktop_select_x2) - rx;
            int rh = (desktop_select_y1 > desktop_select_y2 ? desktop_select_y1 : desktop_select_y2) - ry;
            for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                desktop_icon_multi_selected[i] = desktop_icon_intersects_rect(i, rx, ry, rw, rh);
            }
        } else {
            desktop_select_dragging = false;
            int dx = desktop_select_x2 - desktop_select_x1;
            int dy = desktop_select_y2 - desktop_select_y1;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx <= 2 && dy <= 2) {
                for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                    desktop_icon_multi_selected[i] = false;
                }
            }
        }
        return;
    }

    if (mouse.left && drag_desktop_icon >= 0 && drag_desktop_icon < DESKTOP_ICON_COUNT) {
        int dx = mouse.x - desktop_icon_press_x;
        int dy = mouse.y - desktop_icon_press_y;
        for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
            if (i == drag_desktop_icon || desktop_icon_multi_selected[i]) {
                int bx, by, bw, bh;
                desktop_icon_bounds(0, 0, desktop_icon_image(i), desktop_icon_label(i), &bx, &by, &bw, &bh);
                desktop_icons[i].x = clampi(desktop_icon_drag_start_x[i] + dx, 0, OS_WIDTH - bw);
                desktop_icons[i].y = clampi(desktop_icon_drag_start_y[i] + dy, 0, OS_HEIGHT - TASKBAR_H - bh);
            }
        }
        return;
    }

    if (mouse.left && drag_window >= 0 && drag_window < APP_COUNT && windows[drag_window].open) {
        Window *window = &windows[drag_window];
        if (settings_applied.window_trails) {
            int idx = trail_head % TRAIL_COUNT;
            trail_x[idx] = window->x;
            trail_y[idx] = window->y;
            trail_w[idx] = window->w;
            trail_h[idx] = window->h;
            trail_tick[idx] = timer_ticks;
            ++trail_head;
        }
        window->x = clampi(mouse.x - drag_offset_x, 0, OS_WIDTH - window->w);
        window->y = clampi(mouse.y - drag_offset_y, 0, OS_HEIGHT - TASKBAR_H - window->h);
        return;
    }

    if (mouse.left && drag_test_window >= 0 && drag_test_window < test_window_count && test_windows[drag_test_window].open) {
        Window *window = &test_windows[drag_test_window];
        window->x = clampi(mouse.x - drag_test_offset_x, 0, OS_WIDTH - window->w);
        window->y = clampi(mouse.y - drag_test_offset_y, 0, OS_HEIGHT - TASKBAR_H - window->h);
        return;
    }

    if (!mouse.left) {
        drag_window = -1;
        drag_test_window = -1;
        if (drag_desktop_icon >= 0 && desktop_icon_drag_moved) {
            save_desktop_icon_positions();
            desktop_auto_grid_icons();
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
                    drag_anim_tick = timer_ticks;
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

    if (!pointer_over_window) {
        int first_test = active_test_window;
        for (int pass = 0; pass <= test_window_count; ++pass) {
            int i = pass == 0 ? first_test : (test_window_count - pass);
            if (i < 0 || i >= test_window_count) continue;
            if (pass > 0 && i == first_test) continue;
            if (!test_windows[i].open) continue;
            if (!point_in_rect(mouse.x, mouse.y, test_windows[i].x, test_windows[i].y, test_windows[i].w, test_windows[i].h)) continue;
            pointer_over_window = true;
            if (clicked) {
                if (point_in_rect(mouse.x, mouse.y, test_windows[i].x + test_windows[i].w - 18, test_windows[i].y + 3, 12, 12)) {
                    test_windows[i].open = false;
                    if (active_test_window == i) active_test_window = -1;
                    return;
                }
                if (point_in_rect(mouse.x, mouse.y, test_windows[i].x, test_windows[i].y, test_windows[i].w, 18)) {
                    active_test_window = i;
                    drag_test_window = i;
                    drag_anim_tick = timer_ticks;
                    drag_test_offset_x = mouse.x - test_windows[i].x;
                    drag_test_offset_y = mouse.y - test_windows[i].y;
                    return;
                }
                active_test_window = i;
                return;
            }
            break;
        }
    }

    if (!pointer_over_window && button_right_clicked(0, 0, OS_WIDTH, OS_HEIGHT - TASKBAR_H)) {
        int hit = desktop_icon_hit_test(mouse.x, mouse.y);
        if (hit >= 0) {
            if (!desktop_icon_multi_selected[hit]) {
                memset_local(desktop_icon_multi_selected, 0, sizeof(desktop_icon_multi_selected));
            }
            selected_desktop_icon = hit;
            desktop_icon_menu_open = true;
            desktop_icon_menu_target = hit;
            desktop_icon_menu_x = clampi(mouse.x, 0, OS_WIDTH - 120);
            desktop_icon_menu_y = clampi(mouse.y, 0, OS_HEIGHT - TASKBAR_H - 75);
            menu_open = false;
            context_menu_open = false;
            return;
        }
        context_menu_open = true;
        menu_open = false;
        desktop_icon_menu_open = false;
        start_app_menu_open = false;
        context_menu_x = clampi(mouse.x, 0, OS_WIDTH - 120);
        context_menu_y = clampi(mouse.y, 0, OS_HEIGHT - TASKBAR_H - 64);
        return;
    }

    if (!pointer_over_window) {
        int hit = desktop_icon_hit_test(mouse.x, mouse.y);

        if (clicked && hit >= 0) {
            if (!desktop_icon_multi_selected[hit]) {
                for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                    desktop_icon_multi_selected[i] = false;
                }
            }
            desktop_icon_press = hit;
            desktop_icon_press_x = mouse.x;
            desktop_icon_press_y = mouse.y;
            desktop_icon_drag_offset_x = mouse.x - desktop_icons[hit].x;
            desktop_icon_drag_offset_y = mouse.y - desktop_icons[hit].y;
            desktop_icon_drag_moved = false;
            desktop_icon_press_was_selected = selected_desktop_icon == hit;
            selected_desktop_icon = hit;
            for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                if (i == hit || desktop_icon_multi_selected[i]) {
                    desktop_icon_drag_start_x[i] = desktop_icons[i].x;
                    desktop_icon_drag_start_y[i] = desktop_icons[i].y;
                }
            }
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
                int dx = mouse.x - desktop_icon_press_x;
                int dy = mouse.y - desktop_icon_press_y;
                for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                    if (i == drag_desktop_icon || desktop_icon_multi_selected[i]) {
                        int bx, by, bw, bh;
                        desktop_icon_bounds(0, 0, desktop_icon_image(i), desktop_icon_label(i), &bx, &by, &bw, &bh);
                        desktop_icons[i].x = clampi(desktop_icon_drag_start_x[i] + dx, 0, OS_WIDTH - bw);
                        desktop_icons[i].y = clampi(desktop_icon_drag_start_y[i] + dy, 0, OS_HEIGHT - TASKBAR_H - bh);
                    }
                }
                return;
            }
        } else if (clicked && hit < 0) {
            selected_desktop_icon = -1;
            for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                desktop_icon_multi_selected[i] = false;
            }
            desktop_select_dragging = true;
            desktop_select_x1 = mouse.x;
            desktop_select_y1 = mouse.y;
            desktop_select_x2 = mouse.x;
            desktop_select_y2 = mouse.y;
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

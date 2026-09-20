// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: mouse.c, desktop mouse input handling.

// This repository is licensed under the GNU General Public License.

static void handle_desktop_mouse(void) {
    bool pointer_over_window = false;
    bool clicked = mouse.left && !mouse.prev_left;

    if (debug_overlay_open) {
        return;
    }

    /* DOOM has an explicit mouse-capture gesture.  Middle-click always
     * toggles capture while DOOM is focused; when released, a left click
     * inside the actual 320x200 client frame captures the pointer again.
     * That capture click is consumed and is NEVER forwarded as a DOOM
     * fire/use action. */
    if (active_window == APP_DOOM && windows[APP_DOOM].open) {
        bool middle_clicked = mouse.middle && !mouse.prev_middle;
        if (middle_clicked) {
            doom_toggle_pointer_lock();
            return;
        }
        if (doom_pointer_locked()) {
            return;
        }
        if (clicked && doom_point_in_client(mouse.x, mouse.y)) {
            doom_set_pointer_lock(true);
            return;
        }
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
        /* The clock's clickable zone comes from the widget geometry the
         * taskbar draws with, so the two can never drift apart. */
        int clock_x = clock_widget_x();
        int total_open = 0;
        for (int i = 0; i < APP_COUNT; ++i) {
            if (windows[i].open) ++total_open;
        }
        int open_test = count_open_test_windows();
        if (open_test > 0) ++total_open;
        if (total_open > TASKBAR_APP_SLOTS) {
            int arrow_x = clock_x - 4 - 28;
            bool can_left = false;
            for (int i = taskbar_scroll - 1; i >= 0; --i) {
                if (windows[i].open) { can_left = true; break; }
            }
            if (!can_left && taskbar_scroll > APP_COUNT) can_left = true;
            bool can_right = false;
            int scan = taskbar_scroll;
            int slot = 0;
            for (; slot < TASKBAR_APP_SLOTS && scan < APP_COUNT; ++scan) {
                if (!windows[scan].open) continue;
                ++slot;
            }
            if (slot < TASKBAR_APP_SLOTS && open_test > 0) {
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
        for (; slot < TASKBAR_APP_SLOTS && idx < APP_COUNT; ++idx) {
            if (!windows[idx].open) continue;
            if (button_clicked(58 + slot * 88, OS_HEIGHT - 24, 84, 18)) {
                set_active_window((AppId)idx);
                return;
            }
            ++slot;
        }
        if (slot < TASKBAR_APP_SLOTS && idx >= APP_COUNT && count_open_test_windows() > 0) {
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
            for (; slot < TASKBAR_APP_SLOTS && idx < APP_COUNT; ++idx) {
                if (!windows[idx].open) continue;
                if (point_in_rect(mouse.x, mouse.y, 58 + slot * 88, OS_HEIGHT - 24, 84, 18)) {
                    on_button = true; break;
                }
                ++slot;
            }
            if (!on_button && slot < TASKBAR_APP_SLOTS && idx >= APP_COUNT && count_open_test_windows() > 0) {
                if (point_in_rect(mouse.x, mouse.y, 58 + slot * 88, OS_HEIGHT - 24, 84, 18)) {
                    on_button = true;
                }
            }
        }
        if (!on_button) {
            int clock_x = clock_widget_x();
            int total_open = 0;
            for (int i = 0; i < APP_COUNT; ++i) {
                if (windows[i].open) ++total_open;
            }
            int right_zone = total_open > TASKBAR_APP_SLOTS ? clock_x - 32 : clock_x;
            if (mouse.x >= right_zone) on_button = true;
        }
        if (!on_button) {
            taskbar_menu_open = true;
            taskbar_menu_x = clampi(mouse.x, 0, OS_WIDTH - 150);
            /* Always float the menu fully above the taskbar: clamping to
             * the click Y used to let it overlap the bar's START/window
             * buttons, so hovering or closing it touched the very button
             * it was opened from. */
            taskbar_menu_y = OS_HEIGHT - TASKBAR_H - 16;
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
        if (mouse.right && !mouse.prev_right) {
            /* Right-click while band-selecting: cancel the selection
             * outright instead of letting the click fall through to the
             * context-menu path. Rapid right-clicks during a drag used
             * to freeze the rubber band because the menu state machine
             * and the drag release fought over the same frames. */
            desktop_select_dragging = false;
            for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                desktop_icon_multi_selected[i] = false;
            }
            return;
        }
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
        /* Snap to the 4x4 grid and keep the icon off every open window
         * (see desktop_icon_drag_to): the icon slides along a window's
         * edge rather than across its face. */
        desktop_icon_drag_to(desktop_icon_drag_start_x[drag_desktop_icon] + dx,
                             desktop_icon_drag_start_y[drag_desktop_icon] + dy);
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
        /* Whoever paints last owns those pixels, so it also owns the click.
         * desktop_top_window_at() scans in exactly render_desktop's order
         * (test windows paint above app windows; the active window of each
         * group paints on top of its group), so a click can only ever reach
         * the window the user actually sees under the pointer. The two
         * hand-rolled passes this replaces scanned app windows first and
         * gated the test windows on "no app window here", which handed the
         * click to a covered app window whenever a test window sat on it. */
        Window *hit = (Window *)desktop_top_window_at(mouse.x, mouse.y);

        if (hit != 0) {
            /* Which array does the hit belong to? This MUST be a range
             * membership test, never a bare "hit >= test_windows": the
             * linker is free to place .bss objects in any order, and in
             * the current build windows[] sits ABOVE test_windows[], so
             * every app window satisfied "hit >= test_windows" and was
             * misclassified as a test window - title-bar drags set a
             * garbage index the drag guard rejected (windows stopped
             * moving) and client clicks were swallowed by the test-window
             * branch before any app handler could run (all buttons dead)
             * while hover, which compares pointer identity, kept working. */
            bool is_test = hit >= test_windows && hit < test_windows + MAX_TEST_WINDOWS;
            int index = is_test ? (int)(hit - test_windows) : (int)(hit - windows);

            pointer_over_window = true;
            if (clicked) {
                if (point_in_rect(mouse.x, mouse.y, hit->x + hit->w - 18, hit->y + 3, 12, 12)) {
                    if (is_test) {
                        hit->open = false;
                        if (active_test_window == index) active_test_window = -1;
                    } else {
                        close_window((AppId)index);
                    }
                    return;
                }
                if (point_in_rect(mouse.x, mouse.y, hit->x, hit->y, hit->w, 18)) {
                    drag_anim_tick = timer_ticks;
                    if (is_test) {
                        active_test_window = index;
                        drag_test_window = index;
                        drag_test_offset_x = mouse.x - hit->x;
                        drag_test_offset_y = mouse.y - hit->y;
                    } else {
                        set_active_window((AppId)index);
                        drag_window = index;
                        drag_offset_x = mouse.x - hit->x;
                        drag_offset_y = mouse.y - hit->y;
                    }
                    return;
                }
                if (is_test) {
                    /* A test window always consumes a client click, so it
                     * can never leak to the icons or the selection below. */
                    active_test_window = index;
                    return;
                }
                if (active_window != index) {
                    set_active_window((AppId)index);
                    return;
                }
                /* Already-active window: fall through so the app's own
                 * mouse handler (below) receives this in-window click.
                 * Desktop icons and the rubber-band selector stay safe:
                 * they only run when !pointer_over_window. */
            }
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
                /* Same snapped, window-aware move as the settled drag path
                 * above. The deltas above were made absolute for the
                 * threshold test, so the signed ones are derived again. */
                desktop_icon_drag_to(desktop_icon_drag_start_x[drag_desktop_icon] + (mouse.x - desktop_icon_press_x),
                                     desktop_icon_drag_start_y[drag_desktop_icon] + (mouse.y - desktop_icon_press_y));
                return;
            }
        } else if (clicked && hit < 0) {
            /* A click on bare desktop drops activation from the front
             * window, exactly like clicking away in a window manager:
             * the click is OUTSIDE every window here (the topmost-window
             * pass above consumed any window hit), so no app can lose a
             * click it should have owned. set_active_window also closes
             * any straggler menus, and -1 is a legal resting state: the
             * keyboard router and taskbar treat it as "no active app"
             * (Ctrl+Z / Ctrl+A select-all still work on the icons). */
            if (active_window == APP_DOOM) {
                /* Never leave DOOM's pointer lock behind: the unlock
                 * gesture (middle-click) needs DOOM active, so a lock
                 * surviving deactivation would hide the cursor with no
                 * way to bring it back. */
                doom_set_pointer_lock(false);
            }
            set_active_window(-1);
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

    /*
     * Hand this frame's mouse state to exactly one app: the owner of the
     * pointer.
     *
     * Calling every handler used to be the norm, each one guarding itself
     * with its own rectangle - but those rectangles overlap freely between
     * windows, so a click inside the window in front satisfied the guards
     * of whatever window was behind it as well. Routing on the topmost
     * window removes the whole class: with no window under the pointer, no
     * app handler runs at all, and a test window on top swallows the click
     * instead of an app window underneath reacting to it.
     */
    switch (desktop_pointer_owner_app()) {
        case APP_PAINT: paint_handle_mouse(); break;
        case APP_EXPLORER: explorer_handle_mouse(); break;
        case APP_MINES: mines_handle_mouse(); break;
        case APP_GAME_CENTER: game_center_handle_mouse(); break;
        case APP_DEMO_CENTER: demo_center_handle_mouse(); break;
        case APP_FIRECRACKER: firecracker_handle_mouse(); break;
        case APP_POWER: power_handle_mouse(); break;
        case APP_SETTINGS: settings_handle_mouse(); break;
        case APP_TASK_MANAGER: task_manager_handle_mouse(); break;
        default: break;
    }
}

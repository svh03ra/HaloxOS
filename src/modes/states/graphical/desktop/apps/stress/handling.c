// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, Stress Test input handling.

// This repository is licensed under the GNU General Public License.

static void stress_handle_mouse(void) {
    Window *window = &windows[APP_STRESS_TEST];
    int x;
    int y;

    if (!window->open || active_window != APP_STRESS_TEST ||
        !mouse.left || mouse.prev_left) {
        return;
    }
    /* Clicks outside this window belong to the window that was hit first;
     * an overlap must not press these controls. */
    if (!point_in_rect(mouse.x, mouse.y, window->x, window->y,
                       window->w, window->h)) {
        return;
    }

    x = window->x;
    y = window->y;

    /* Single-choice mode circles.  The hit box covers the circle and its
     * label, the same rectangle the renderer paints. */
    for (int mode = 0; mode < 4; ++mode) {
        int rx = x + stress_radio_x((uint8_t)mode);

        if (button_clicked(rx - 4, y + STRESS_ROW_RADIO - 2, 60, 14)) {
            if (stress_test_running && stress_test_mode != (uint8_t)mode) {
                /* Changing the target while loaded would leave the old
                 * mode's real working set behind; stop cleanly so the next
                 * Trigger reserves the right one. */
                stress_stop();
            }
            stress_test_mode = (uint8_t)mode;
            ui_input_dirty = true;
            return;
        }
    }

    if (button_clicked(x + STRESS_BOX_X, y + STRESS_BOX_Y_INTENSITY,
                       STRESS_BOX_W, STRESS_BOX_H)) {
        stress_focus_field(0);
        return;
    }
    if (button_clicked(x + STRESS_BOX_X, y + STRESS_BOX_Y_INTERVAL,
                       STRESS_BOX_W, STRESS_BOX_H)) {
        stress_focus_field(1);
        return;
    }

    if (button_clicked(x + STRESS_TRIGGER_X, y + STRESS_ROW_BUTTONS,
                       STRESS_BUTTON_W, STRESS_BUTTON_H)) {
        stress_apply_values();
        stress_start();
        return;
    }
    if (stress_test_running &&
        button_clicked(x + STRESS_STOP_X, y + STRESS_ROW_BUTTONS,
                       STRESS_BUTTON_W, STRESS_BUTTON_H)) {
        stress_stop();
        return;
    }

    /* A click anywhere else drops text focus. */
    stress_field_focus = -1;
    ui_input_dirty = true;
}

static void stress_handle_key(KeyEvent event) {
    int which = stress_field_focus;
    char *text;
    int *len;
    int max_len;

    if (event.code == KEY_TAB) {
        if (which < 0) {
            stress_focus_field(0);
        } else {
            stress_apply_values();
            stress_focus_field(which == 0 ? 1 : 0);
        }
        return;
    }
    if (event.code == KEY_ENTER) {
        if (which >= 0) {
            stress_apply_values();
            stress_field_focus = -1;
            stress_field_fresh = false;
            ui_input_dirty = true;
        }
        return;
    }
    if (which < 0) {
        return;
    }

    text = which == 0 ? stress_intensity_text : stress_interval_text;
    len = which == 0 ? &stress_intensity_len : &stress_interval_len;
    max_len = which == 0 ? 5 : 6;

    if (event.code == KEY_BACKSPACE) {
        stress_field_fresh = false;
        if (*len > 0) {
            --*len;
            text[*len] = '\0';
        }
        ui_input_dirty = true;
        return;
    }

    if (event.ch >= '0' && event.ch <= '9') {
        /* The first digit after focusing replaces the seeded value, the
         * same way the debugger's bitmap-size boxes behave. */
        if (stress_field_fresh) {
            *len = 0;
            text[0] = '\0';
            stress_field_fresh = false;
        }
        if (*len < max_len) {
            text[*len] = event.ch;
            ++*len;
            text[*len] = '\0';
        }
        ui_input_dirty = true;
    }
}

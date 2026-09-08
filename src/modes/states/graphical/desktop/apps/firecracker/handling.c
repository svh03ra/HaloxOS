// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, 3D box + firecracker input handling.

// This repository is licensed under the GNU General Public License.

static void box3d_handle_key(KeyEvent event) {
    if (event.code == KEY_LEFT) {
        box3d_mode = 0;
    } else if (event.code == KEY_RIGHT) {
        box3d_mode = 1;
    }
}

static void firecracker_handle_mouse(void) {
    if (!windows[APP_FIRECRACKER].open || active_window != APP_FIRECRACKER ||
        !mouse.left || mouse.prev_left) {
        return;
    }

    Window *window = &windows[APP_FIRECRACKER];
    /* click must land inside this window's client area */
    if (!point_in_rect(mouse.x, mouse.y, window->x, window->y, window->w, window->h - 18)) {
        return;
    }

    /* spawn a burst of particles at the click point in random colors */
    int spawned = 0;
    for (int i = 0; i < FIRE_PARTICLES && spawned < 28; ++i) {
        if (fire_alive[i]) {
            continue;
        }
        int angle = rand_range(360);
        int speed = 30 + rand_range(50);

        fire_alive[i] = true;
        fire_x[i] = mouse.x;
        fire_y[i] = mouse.y;
        fire_vx[i] = box3d_cos(angle) * speed / 256;
        fire_vy[i] = box3d_sin(angle) * speed / 256;
        /* random bright palette color */
        switch (rand_range(6)) {
            case 0: fire_color[i] = color_red; break;
            case 1: fire_color[i] = color_orange; break;
            case 2: fire_color[i] = color_yellow; break;
            case 3: fire_color[i] = color_green; break;
            case 4: fire_color[i] = color_pink; break;
            default: fire_color[i] = color_white; break;
        }
        ++spawned;
    }
}

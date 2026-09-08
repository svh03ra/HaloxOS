// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: firecracker.c, firecracker particle demo rendering.

// This repository is licensed under the GNU General Public License.

static void update_demos(void) {
    /* 3D box: tumble on both axes - yaw 4 deg / 2 ticks, pitch 3 deg */
    if (windows[APP_3D_BOX].open && !cpu_halted_overlay && !debug_overlay_open) {
        if (timer_ticks - box3d_last_tick >= 2u) {
            box3d_last_tick = timer_ticks;
            box3d_angle += 4;
            if (box3d_angle >= 360) {
                box3d_angle -= 360;
            }
            box3d_angle_x += 3;
            if (box3d_angle_x >= 360) {
                box3d_angle_x -= 360;
            }
        }
    }

    /* firecracker particles: physics every tick */
    if (windows[APP_FIRECRACKER].open && !cpu_halted_overlay && !debug_overlay_open) {
        if (timer_ticks != fire_last_tick) {
            fire_last_tick = timer_ticks;
            for (int i = 0; i < FIRE_PARTICLES; ++i) {
                if (!fire_alive[i]) {
                    continue;
                }
                fire_vy[i] += 4;            /* gravity */
                fire_x[i] += fire_vx[i] / 16;
                fire_y[i] += fire_vy[i] / 16;
                /* fade out after leaving the window area */
                if (fire_y[i] > OS_HEIGHT || fire_x[i] < 0 || fire_x[i] > OS_WIDTH || fire_vy[i] > 200) {
                    fire_alive[i] = false;
                }
            }
        }
    }
}

static void render_firecracker(const Window *window) {
    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_black);
    draw_text(window->x + 12, window->y + 30, "Click anywhere to shoot!", color_gray_light, color_black, true);

    for (int i = 0; i < FIRE_PARTICLES; ++i) {
        if (fire_alive[i]) {
            draw_pixel(fire_x[i], fire_y[i], fire_color[i]);
            /* 2x2 chunky pixel so bursts read at small sizes */
            draw_pixel(fire_x[i] + 1, fire_y[i], fire_color[i]);
            draw_pixel(fire_x[i], fire_y[i] + 1, fire_color[i]);
            draw_pixel(fire_x[i] + 1, fire_y[i] + 1, fire_color[i]);
        }
    }
}

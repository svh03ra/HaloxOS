// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: handling.c, snake input handling.

// This repository is licensed under the GNU General Public License.

static void snake_spawn_food(void) {
    bool occupied = true;
    while (occupied) {
        occupied = false;
        snake_food_x = rand_range(20);
        snake_food_y = rand_range(14);
        for (int i = 0; i < snake_length; ++i) {
            if (snake_x[i] == snake_food_x && snake_y[i] == snake_food_y) {
                occupied = true;
                break;
            }
        }
    }
}

static void update_snake(void) {
    if (!windows[APP_SNAKE].open || snake_dead || cpu_halted_overlay || debug_overlay_open) {
        return;
    }

    if (timer_ticks - snake_last_step_tick < SNAKE_STEP_TICKS) {
        return;
    }
    snake_last_step_tick = timer_ticks;

    if ((snake_dir == 0 && snake_next_dir != 2) || (snake_dir == 2 && snake_next_dir != 0) ||
        (snake_dir == 1 && snake_next_dir != 3) || (snake_dir == 3 && snake_next_dir != 1)) {
        snake_dir = snake_next_dir;
    }

    int next_x = snake_x[0];
    int next_y = snake_y[0];
    if (snake_dir == 0) --next_y;
    if (snake_dir == 1) ++next_x;
    if (snake_dir == 2) ++next_y;
    if (snake_dir == 3) --next_x;

    if (next_x < 0 || next_y < 0 || next_x >= 20 || next_y >= 14) {
        snake_dead = true;
        return;
    }

    for (int i = 0; i < snake_length; ++i) {
        if (snake_x[i] == next_x && snake_y[i] == next_y) {
            snake_dead = true;
            return;
        }
    }

    for (int i = snake_length; i > 0; --i) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }
    snake_x[0] = next_x;
    snake_y[0] = next_y;

    if (next_x == snake_food_x && next_y == snake_food_y) {
        if (snake_length < SNAKE_MAX_SEGMENTS - 1) {
            ++snake_length;
        }
        snake_score += 10;
        snake_spawn_food();
    }
}

static void reset_snake(void) {
    snake_length = 4;
    snake_dir = 1;
    snake_next_dir = 1;
    snake_last_step_tick = timer_ticks;
    snake_score = 0;
    snake_dead = false;
    for (int i = 0; i < snake_length; ++i) {
        snake_x[i] = 5 - i;
        snake_y[i] = 5;
    }
    snake_food_x = 12;
    snake_food_y = 7;
}

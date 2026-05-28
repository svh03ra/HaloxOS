static void mines_place(void) {
    memset_local(mines_value, 0, sizeof(mines_value));
    memset_local(mines_revealed, 0, sizeof(mines_revealed));
    memset_local(mines_flagged, 0, sizeof(mines_flagged));
    mines_lost = false;
    mines_won = false;

    int placed = 0;
    while (placed < MINES_COUNT) {
        int x = rand_range(MINES_SIZE);
        int y = rand_range(MINES_SIZE);
        if (mines_value[y][x] == 9) {
            continue;
        }
        mines_value[y][x] = 9;
        ++placed;
    }

    for (int y = 0; y < MINES_SIZE; ++y) {
        for (int x = 0; x < MINES_SIZE; ++x) {
            if (mines_value[y][x] == 9) {
                continue;
            }
            uint8_t count = 0;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    int nx = x + ox;
                    int ny = y + oy;
                    if (nx >= 0 && ny >= 0 && nx < MINES_SIZE && ny < MINES_SIZE && mines_value[ny][nx] == 9) {
                        ++count;
                    }
                }
            }
            mines_value[y][x] = count;
        }
    }
}

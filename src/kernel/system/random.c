// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: random.c, random core.

// This repository is licensed under the GNU General Public License.

static uint32_t rand_next(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static int rand_range(int max_value) {
    if (max_value <= 0) {
        return 0;
    }
    return (int)((rand_next() >> 16) % (uint32_t)max_value);
}

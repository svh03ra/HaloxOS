// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: timer.c, timer logic.

// This repository is licensed under the GNU General Public License.

void timer_tick_from_isr(void) {
    ++timer_ticks;
}

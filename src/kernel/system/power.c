// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: power.c, power management.

// This repository is licensed under the GNU General Public License.

static void attempt_poweroff(void) {
    serial_trace("INFO", "ACPI poweroff I/O sequence");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
}

static void shutdown_system(void) {
    serial_trace("INFO", "shutdown requested");
    shutdown_pending = true;
    system_state = STATE_SHUTDOWN;
}

static void restart_system(void) {
    serial_trace("INFO", "restart requested through keyboard controller");
    while (inb(0x64) & 0x02) {
    }
    outb(0x64, 0xFE);
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

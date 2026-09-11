// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: keyboard_mouse.c, keyboard and mouse input driver.

// This repository is licensed under the GNU General Public License.

static void enqueue_key(KeyCode code, char ch) {
    int next = (key_tail + 1) % 64;
    if (next == key_head) {
        return;
    }
    last_input_tick = timer_ticks;
    key_queue[key_tail].code = code;
    key_queue[key_tail].ch = ch;
    key_tail = next;
}

static bool dequeue_key(KeyEvent *event) {
    if (key_head == key_tail) {
        return false;
    }
    *event = key_queue[key_head];
    key_head = (key_head + 1) % 64;
    return true;
}

static char scancode_to_char(uint8_t scancode, bool shifted) {
    static const char normal[] =
        "\0\0331234567890-=\0\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shifted_map[] =
        "\0\033!@#$%^&*()_+\0\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

    if (scancode >= sizeof(normal) - 1) {
        return 0;
    }
    return shifted ? shifted_map[scancode] : normal[scancode];
}

static void handle_scancode(uint8_t code) {
    if (code == 0xE0) {
        keyboard_extended = true;
        return;
    }

    if (code == 0x2A || code == 0x36) {
        keyboard_shift = true;
        return;
    }

    if (code == 0x38) {
        keyboard_alt = true;
        return;
    }

    if (code == 0x1D) {
        keyboard_ctrl = true;
        return;
    }

    if (code == 0xAA || code == 0xB6) {
        keyboard_shift = false;
        return;
    }

    if (code == 0xB8) {
        keyboard_alt = false;
        return;
    }

    if (code == 0x9D) {
        keyboard_ctrl = false;
        return;
    }

    if (code & 0x80) {
        keyboard_extended = false;
        return;
    }

    if (keyboard_extended) {
        keyboard_extended = false;
        switch (code) {
            case 0x48: enqueue_key(KEY_UP, 0); return;
            case 0x50: enqueue_key(KEY_DOWN, 0); return;
            case 0x4B: enqueue_key(KEY_LEFT, 0); return;
            case 0x4D: enqueue_key(KEY_RIGHT, 0); return;
            case 0x47: enqueue_key(KEY_HOME, 0); return;
            case 0x4F: enqueue_key(KEY_END, 0); return;
            case 0x53: enqueue_key(KEY_DEL, 0); return;
            default: return;
        }
    }

    switch (code) {
        case 0x01: enqueue_key(KEY_ESC, 0); return;
        case 0x0E: enqueue_key(KEY_BACKSPACE, 0); return;
        case 0x0F: enqueue_key(KEY_TAB, 0); return;
        case 0x1C: enqueue_key(KEY_ENTER, '\n'); return;
        case 0x3B: enqueue_key(KEY_F1, 0); return;
        case 0x3C: enqueue_key(KEY_F2, 0); return;
        case 0x3E: enqueue_key(KEY_F4, 0); return;
        case 0x53: enqueue_key(KEY_DEL, 0); return;
        default: {
            char ch = scancode_to_char(code, keyboard_shift);
            if (ch) {
                enqueue_key(KEY_NONE, ch);
            }
            return;
        }
    }
}

static bool ps2_wait_write(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((inb(0x64) & 0x02) == 0) {
            return true;
        }
    }
    return false;
}

static bool ps2_wait_read(void) {
    for (int i = 0; i < 100000; ++i) {
        if (inb(0x64) & 0x01) {
            return true;
        }
    }
    return false;
}

static void ps2_write_mouse(uint8_t value) {
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x64, 0xD4);
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x60, value);
}

static void init_mouse(void) {
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x64, 0xA8);
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x64, 0x20);
    if (!ps2_wait_read()) {
        return;
    }
    uint8_t status = inb(0x60);
    status |= 0x02;
    status &= (uint8_t)~0x20;
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x64, 0x60);
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x60, status);

    ps2_write_mouse(0xF6);
    if (ps2_wait_read()) {
        inb(0x60);
    }
    ps2_write_mouse(0xF4);
    if (ps2_wait_read()) {
        inb(0x60);
    }
}

/*
 * Real-time mouse input.
 *
 * Packets are consumed by IRQ12 the instant they arrive (cursor moves
 * with interrupt latency, ~1ms, instead of waiting for the next 60Hz
 * frame poll - the old path batch-applied ~120ms of movement on heavy
 * emulator frames and the cursor teleported / felt desynced).
 *
 * Split state model:
 *  - mouse.x / mouse.y / mouse.middle: written live by the ISR (real
 *    time position, handlers read them mid-frame).
 *  - mouse.left / mouse.right / prev_*: shaped once per frame in
 *    poll_input from the physical hw state. Buttons are frame-granular
 *    so each physical press produces exactly one "clicked" edge, even
 *    when a press+release completes inside a single slow frame.
 *  - press latch counters: the ISR counts release->press edges; poll
 *    replays one pending edge per frame. Without this, a fast click
 *    that fully lands between two frames would be lost.
 */
static bool mouse_hw_left = false;
static bool mouse_hw_right = false;
static uint8_t mouse_press_latch_left = 0;
static uint8_t mouse_press_latch_right = 0;
static bool mouse_click_replay_left = false;
static bool mouse_click_replay_right = false;

static void mouse_apply_byte(uint8_t data) {
    /* Byte 0 must have bit 3 set; else the stream desynced - resync. */
    if (mouse_packet_index == 0 && (data & 0x08) == 0) {
        return;
    }

    mouse_packet[mouse_packet_index++] = data;
    if (mouse_packet_index != 3) {
        return;
    }
    mouse_packet_index = 0;

    {
        int dx = (mouse_packet[0] & 0x10) ? (int)mouse_packet[1] - 256 : (int)mouse_packet[1];
        int dy = (mouse_packet[0] & 0x20) ? (int)mouse_packet[2] - 256 : (int)mouse_packet[2];
        int nx = mouse.x + dx;
        int ny = mouse.y - dy;
        bool new_left = (mouse_packet[0] & 0x01) != 0;
        bool new_right = (mouse_packet[0] & 0x02) != 0;
        bool new_middle = (mouse_packet[0] & 0x04) != 0;

        if (nx < 0) nx = 0;
        else if (nx > OS_WIDTH - 1) nx = OS_WIDTH - 1;
        if (ny < 0) ny = 0;
        else if (ny > OS_HEIGHT - 1) ny = OS_HEIGHT - 1;

        if (nx != mouse.x || ny != mouse.y || new_left != mouse_hw_left ||
            new_right != mouse_hw_right || new_middle != mouse.middle) {
            last_input_tick = timer_ticks;
        }

        if (new_left && !mouse_hw_left && mouse_press_latch_left < 255) {
            ++mouse_press_latch_left;
        }
        if (new_right && !mouse_hw_right && mouse_press_latch_right < 255) {
            ++mouse_press_latch_right;
        }
        mouse_hw_left = new_left;
        mouse_hw_right = new_right;

        mouse.x = nx;
        mouse.y = ny;
        mouse.middle = new_middle;
    }
}

/* IRQ12 (PS/2 mouse): drain everything the 8042 queued while the CPU was
 * busy. This is what kills the ~120ms batch-apply lag on heavy emulators.
 * Stray keyboard bytes share the same output queue and MUST be routed to
 * the scancode handler here, otherwise a mouse burst would eat them. */
void mouse_packet_from_isr(void) {
    for (;;) {
        uint8_t status = inb(0x64);
        if ((status & 0x01) == 0) {
            return;
        }
        uint8_t data = inb(0x60);
        if (status & 0x20) {
            mouse_apply_byte(data);
        } else {
            handle_scancode(data);
        }
    }
}

static void poll_input(void) {
    /* End a replayed one-frame click hold: restore physical state. */
    if (mouse_click_replay_left) {
        mouse.left = mouse_hw_left;
        mouse_click_replay_left = false;
    }
    if (mouse_click_replay_right) {
        mouse.right = mouse_hw_right;
        mouse_click_replay_right = false;
    }

    mouse.prev_left = mouse.left;
    mouse.prev_right = mouse.right;
    mouse.prev_middle = mouse.middle;

    mouse.left = mouse_hw_left;
    mouse.right = mouse_hw_right;

    /* Deliver one pending press edge per frame. When the physical button
     * is already held, just expose the edge (prev=false). When the click
     * completed entirely inside the last frame, hold the button for this
     * single frame so click handlers see it exactly once. */
    if (mouse_press_latch_left != 0) {
        --mouse_press_latch_left;
        mouse.prev_left = false;
        if (!mouse.left) {
            mouse.left = true;
            mouse_click_replay_left = true;
        }
    }
    if (mouse_press_latch_right != 0) {
        --mouse_press_latch_right;
        mouse.prev_right = false;
        if (!mouse.right) {
            mouse.right = true;
            mouse_click_replay_right = true;
        }
    }

    /* 8042 drain: keyboard bytes always, mouse bytes as a fallback for
     * boxes where IRQ12 never fires. Runs with IRQs off so the ISR and
     * this loop can never interleave a packet byte pair. */
    __asm__ volatile ("cli");
    for (;;) {
        uint8_t status = inb(0x64);
        if ((status & 0x01) == 0) {
            break;
        }
        uint8_t data = inb(0x60);
        if (status & 0x20) {
            mouse_apply_byte(data);
        } else {
            handle_scancode(data);
        }
    }
    __asm__ volatile ("sti");
}

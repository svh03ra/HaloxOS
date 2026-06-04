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

static void poll_input(void) {
    mouse.prev_left = mouse.left;
    mouse.prev_right = mouse.right;
    mouse.prev_middle = mouse.middle;

    while (inb(0x64) & 0x01) {
        uint8_t status = inb(0x64);
        uint8_t data = inb(0x60);

        if (status & 0x20) {
            if (mouse_packet_index == 0 && (data & 0x08) == 0) {
                continue;
            }

            mouse_packet[mouse_packet_index++] = data;
            if (mouse_packet_index == 3) {
                int dx = (mouse_packet[0] & 0x10) ? (int)mouse_packet[1] - 256 : (int)mouse_packet[1];
                int dy = (mouse_packet[0] & 0x20) ? (int)mouse_packet[2] - 256 : (int)mouse_packet[2];
                int next_x = clampi(mouse.x + dx, 0, OS_WIDTH - 1);
                int next_y = clampi(mouse.y - dy, 0, OS_HEIGHT - 1);
                bool next_left = (mouse_packet[0] & 0x01) != 0;
                bool next_right = (mouse_packet[0] & 0x02) != 0;
                bool next_middle = (mouse_packet[0] & 0x04) != 0;
                if (next_x != mouse.x || next_y != mouse.y ||
                    next_left != mouse.left || next_right != mouse.right || next_middle != mouse.middle) {
                    last_input_tick = timer_ticks;
                }
                mouse.x = next_x;
                mouse.y = next_y;
                mouse.left = next_left;
                mouse.right = next_right;
                mouse.middle = next_middle;
                mouse_packet_index = 0;
            }
        } else {
            handle_scancode(data);
        }
    }
}

// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: serial.c, serial port (COM1) handling for debug only.

// This repository is licensed under the GNU General Public License.

static void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static bool serial_ready(void) {
    return (inb(0x3F8 + 5) & 0x20u) != 0;
}

static void serial_write_char(char ch) {
    for (int i = 0; i < 100000 && !serial_ready(); ++i) {
    }
    outb(0x3F8, (uint8_t)ch);
}

static void serial_write_string(const char *text) {
    if (!debug) {
        return;
    }
    while (*text) {
        if (*text == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*text++);
    }
}

static void serial_trace(const char *level, const char *text) {
    if (!debug) {
        return;
    }
    serial_write_char('[');
    serial_write_string(level);
    serial_write_string("]: ");
    serial_write_string(text);
    serial_write_string("\n");
}

static void serial_write_hex8(uint8_t value) {
    static const char hex[] = "0123456789ABCDEF";
    serial_write_char(hex[(value >> 4) & 0x0F]);
    serial_write_char(hex[value & 0x0F]);
}

static void serial_write_hex32(uint32_t value) {
    serial_write_string("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        static const char hex[] = "0123456789ABCDEF";
        serial_write_char(hex[(value >> shift) & 0x0Fu]);
    }
}

static void serial_write_uint(uint32_t value) {
    char buffer[16];
    int pos = 0;

    if (value == 0) {
        serial_write_char('0');
        return;
    }

    while (value > 0 && pos < (int)sizeof(buffer)) {
        buffer[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (pos > 0) {
        serial_write_char(buffer[--pos]);
    }
}

static void serial_trace_begin(const char *level) {
    serial_write_char('[');
    serial_write_string(level);
    serial_write_string("]: ");
}

static void serial_trace_hex_value(const char *level, const char *label, uint32_t value) {
    if (!debug) {
        return;
    }
    serial_trace_begin(level);
    serial_write_string(label);
    serial_write_string(": ");
    serial_write_hex32(value);
    serial_write_string("\n");
}

static void serial_trace_uint_value(const char *level, const char *label, uint32_t value) {
    if (!debug) {
        return;
    }
    serial_trace_begin(level);
    serial_write_string(label);
    serial_write_string(": ");
    serial_write_uint(value);
    serial_write_string("\n");
}

static void serial_trace_concat(const char *level, const char *left, const char *right) {
    if (!debug) {
        return;
    }
    serial_trace_begin(level);
    serial_write_string(left);
    serial_write_string(right);
    serial_write_string("\n");
}


// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: serial.c, serial port (COM1) handling for debug only.

// This repository is licensed under the GNU General Public License.

#if HALOXOS_CONFIG_DEBUG
/*
 * RAM mirror of the COM1 trace (debug builds only).
 *
 * Modern machines have no serial port for the QEMU-style host capture, so
 * the trace is also recorded in RAM and flushed onto the boot medium at
 * shutdown/restart/halt and from the crash handler (bootlog.c): that
 * flushed copy is the real-hardware bios-serial.log.
 *
 * Overflow keeps the FIRST 64 KiB: early-boot lines are what a failed
 * boot on real hardware is diagnosed from, and the tail is still on the
 * wire for anyone with a capture box.
 */
#define SERIAL_LOG_BUFFER_BYTES 65536u

static char serial_log_buffer[SERIAL_LOG_BUFFER_BYTES];
static uint32_t serial_log_used;

static void serial_log_append(char ch) {
    if (!debug || serial_log_used >= sizeof(serial_log_buffer)) {
        return;
    }
    serial_log_buffer[serial_log_used++] = ch;
}

static const char *serial_log_data(void) {
    return serial_log_buffer;
}

static uint32_t serial_log_length(void) {
    return serial_log_used;
}

/*
 * The zstd loader runs before the kernel decompresses, so its [LDR]
 * lines can only survive inside the loader's own RAM mirror (which
 * lives below the kernel and outlives the handoff). This copies that
 * range in as the prefix of the kernel mirror, so a flushed log starts
 * with the pre-compression boot story exactly like the QEMU capture.
 * Called once, right after serial_init(), before any kernel trace line.
 */
static void serial_log_load_prefix(const volatile char *src, uint32_t len) {
    if (serial_log_used != 0 || src == NULL || len == 0 ||
        len > SERIAL_LOG_BUFFER_BYTES) {
        return;
    }
    while (len-- > 0) {
        char ch = *src++;
        if (!debug) {
            return;
        }
        if (serial_log_used < sizeof(serial_log_buffer)) {
            serial_log_buffer[serial_log_used++] = ch;
        }
    }
}
#else
/* Release builds keep no mirror and no prefix. */
static void serial_log_load_prefix(const volatile char *src, uint32_t len) {
    (void)src;
    (void)len;
}
#endif

/* 115200 8N1, the same speed the zstd loader opens COM1 with (divisor 1).
 * One fixed baud for the whole boot is what makes a single physical
 * capture work: a terminal or `make capture` box recording the COM1
 * cable gets the loader's [LDR] lines and the kernel traces without
 * reconfiguring mid-boot. QEMU ignores the divisor, so emulator runs
 * are unaffected. */
static void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x01);
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
#if HALOXOS_CONFIG_DEBUG
    serial_log_append(ch);
#endif
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


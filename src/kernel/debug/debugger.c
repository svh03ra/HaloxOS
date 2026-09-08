// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: debugger.c, debugger graphical core.

// This repository is licensed under the GNU General Public License.

static void debug_enter(void) {
    if (!debug || debug_overlay_open) {
        return;
    }
    debug_overlay_open = true;
    debug_memory_view_open = false;
    debug_memory_edit_nibble = -1;
    debug_log_scroll_x = 0;
    terminal_reset(&debug_term);
    terminal_add_line(&debug_term, "Welcome to HaloxOS Debugger!");
    terminal_add_line(&debug_term, "Type 'help' for show all comannds to use.");
    debug_history_cursor = debug_history_count;
    serial_trace("INFO", "Debugger opened");
}

static char ascii_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static void normalize_command(char *command) {
    for (int i = 0; command[i] != '\0'; ++i) {
        command[i] = ascii_lower(command[i]);
    }
}

static const char *skip_spaces(const char *text) {
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    return text;
}

static const char *read_token(const char *text, char *token, size_t max_len) {
    size_t len = 0;

    text = skip_spaces(text);
    while (*text != '\0' && *text != ' ' && *text != '\t') {
        if (len + 1 < max_len) {
            token[len++] = *text;
        }
        ++text;
    }
    token[len] = '\0';
    return text;
}

static bool token_is(const char *token, const char *a, const char *b, const char *c, const char *d) {
    return streq(token, a) ||
           (b != NULL && streq(token, b)) ||
           (c != NULL && streq(token, c)) ||
           (d != NULL && streq(token, d));
}

static int parse_hex_digit_char(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool parse_bpp_token(const char *token, uint32_t *bpp_out) {
    uint32_t value = 0;
    if (!parse_uint_decimal(token, &value)) {
        return false;
    }
    if (value != 4 && value != 8 && value != 16) {
        return false;
    }
    *bpp_out = value;
    return true;
}

static bool parse_resolution_token(const char *token, uint32_t *width_out, uint32_t *height_out) {
    uint32_t width = 0;
    uint32_t height = 0;
    bool any_width = false;
    bool any_height = false;

    while (*token >= '0' && *token <= '9') {
        width = width * 10u + (uint32_t)(*token - '0');
        any_width = true;
        ++token;
    }
    if (*token != 'x' && *token != 'X') {
        return false;
    }
    ++token;
    while (*token >= '0' && *token <= '9') {
        height = height * 10u + (uint32_t)(*token - '0');
        any_height = true;
        ++token;
    }
    if (!any_width || !any_height || *token != '\0') {
        return false;
    }
    if (width == 0 || height == 0 || width > MAX_OUTPUT_WIDTH || height > MAX_OUTPUT_HEIGHT) {
        return false;
    }
    *width_out = width;
    *height_out = height;
    return true;
}

static bool parse_hex_pattern(const char *token, uint8_t *bytes, int *count_out) {
    char digits[8];
    int digit_count = 0;
    int byte_count;

    if (starts_with(token, "0x")) {
        token += 2;
    }

    while (*token != '\0') {
        if (parse_hex_digit_char(*token) < 0 || digit_count >= 8) {
            return false;
        }
        digits[digit_count++] = *token++;
    }

    if (digit_count == 0) {
        return false;
    }

    byte_count = (digit_count + 1) / 2;
    for (int i = 0; i < byte_count; ++i) {
        int digit_index = digit_count - (byte_count - i) * 2;
        int high = 0;
        int low;
        if (digit_index < 0) {
            low = parse_hex_digit_char(digits[0]);
        } else {
            high = parse_hex_digit_char(digits[digit_index]);
            low = parse_hex_digit_char(digits[digit_index + 1]);
        }
        bytes[i] = (uint8_t)((high << 4) | low);
    }

    *count_out = byte_count;
    return true;
}

static uint32_t debug_memory_limit(void) {
    if (ram_total_bytes >= 1024u * 1024u) {
        return ram_total_bytes;
    }
    return 32u * 1024u * 1024u;
}

static bool debug_memory_address_available(uint32_t address) {
    return address < debug_memory_limit();
}

static uint8_t debug_memory_read_byte(uint32_t address) {
    if (!debug_memory_address_available(address)) {
        return 0;
    }
    return *(volatile uint8_t *)(uintptr_t)address;
}

static void debug_memory_write_byte(uint32_t address, uint8_t value) {
    if (!debug_memory_address_available(address)) {
        return;
    }
    *(volatile uint8_t *)(uintptr_t)address = value;
}

static uint32_t debug_range_end(uint32_t start, uint32_t length) {
    uint32_t end = start + length;
    if (end < start) {
        return 0xFFFFFFFFu;
    }
    return end;
}

static void debug_mark_memory_edited(uint32_t address, uint32_t length) {
    uint32_t end;

    if (length == 0) {
        return;
    }

    end = debug_range_end(address, length);
    for (int i = 0; i < debug_edited_range_count; ++i) {
        uint32_t range_start = debug_edited_ranges[i].start;
        uint32_t range_end = debug_range_end(range_start, debug_edited_ranges[i].length);
        if (address <= range_end && end >= range_start) {
            uint32_t merged_start = address < range_start ? address : range_start;
            uint32_t merged_end = end > range_end ? end : range_end;
            debug_edited_ranges[i].start = merged_start;
            debug_edited_ranges[i].length = merged_end - merged_start;
            return;
        }
    }

    if (debug_edited_range_count < DEBUG_EDITED_RANGE_COUNT) {
        debug_edited_ranges[debug_edited_range_count].start = address;
        debug_edited_ranges[debug_edited_range_count].length = length;
        ++debug_edited_range_count;
    } else {
        debug_edited_ranges[DEBUG_EDITED_RANGE_COUNT - 1].start = address;
        debug_edited_ranges[DEBUG_EDITED_RANGE_COUNT - 1].length = length;
    }
}

static bool debug_memory_address_edited(uint32_t address) {
    for (int i = 0; i < debug_edited_range_count; ++i) {
        uint32_t start = debug_edited_ranges[i].start;
        uint32_t end = debug_range_end(start, debug_edited_ranges[i].length);
        if (address >= start && address < end) {
            return true;
        }
    }
    return false;
}

static int debug_memory_hex_rows(void) {
    return 30;
}

static void debug_align_memory_view(void) {
    uint32_t limit = debug_memory_limit();
    uint32_t visible_bytes = (uint32_t)debug_memory_hex_rows() * DEBUG_MEMORY_BYTES_PER_ROW;

    if (limit == 0) {
        debug_memory_base = 0;
        debug_memory_cursor = 0;
        return;
    }
    if (debug_memory_cursor >= limit) {
        debug_memory_cursor = limit - 1;
    }

    if (debug_memory_cursor < debug_memory_base) {
        debug_memory_base = debug_memory_cursor & ~(uint32_t)(DEBUG_MEMORY_BYTES_PER_ROW - 1);
    } else if (debug_memory_cursor >= debug_range_end(debug_memory_base, visible_bytes)) {
        debug_memory_base = (debug_memory_cursor - visible_bytes + DEBUG_MEMORY_BYTES_PER_ROW) &
                            ~(uint32_t)(DEBUG_MEMORY_BYTES_PER_ROW - 1);
    }
}

static void debug_move_memory_cursor(int delta) {
    uint32_t limit = debug_memory_limit();

    if (limit == 0) {
        return;
    }
    if (debug_memory_cursor >= limit) {
        debug_memory_cursor = limit - 1;
    }
    debug_memory_edit_nibble = -1;
    if (delta < 0) {
        uint32_t amount = (uint32_t)(-delta);
        debug_memory_cursor = debug_memory_cursor > amount ? debug_memory_cursor - amount : 0;
    } else if (delta > 0) {
        uint32_t amount = (uint32_t)delta;
        if (limit > 0 && amount >= limit - debug_memory_cursor) {
            debug_memory_cursor = limit - 1;
        } else {
            debug_memory_cursor += amount;
        }
    }
    debug_align_memory_view();
}

static void debug_open_memory_view(uint32_t address) {
    if (!debug_memory_address_available(address)) {
        address = 0;
    }
    debug_memory_cursor = address;
    debug_memory_base = address & ~(uint32_t)(DEBUG_MEMORY_BYTES_PER_ROW - 1);
    debug_memory_mode = DEBUG_MEMORY_MODE_HEX;
    debug_memory_view_open = true;
    debug_memory_edit_nibble = -1;
    debug_align_memory_view();
    serial_trace("INFO", "Debugger memory view opened");
}

static void debug_memory_handle_key(KeyEvent event) {
    if (event.code == KEY_ESC) {
        debug_memory_view_open = false;
        debug_memory_edit_nibble = -1;
        terminal_add_line(&debug_term, "Memory viewer closed.");
        return;
    }
    if (event.code == KEY_F1) {
        debug_memory_mode = DEBUG_MEMORY_MODE_HEX;
        debug_memory_edit_nibble = -1;
        return;
    }
    if (event.code == KEY_F2) {
        debug_memory_mode = DEBUG_MEMORY_MODE_VISUAL;
        debug_memory_edit_nibble = -1;
        return;
    }

    if (event.code == KEY_LEFT) {
        debug_move_memory_cursor(-1);
        return;
    }
    if (event.code == KEY_RIGHT) {
        debug_move_memory_cursor(1);
        return;
    }
    if (event.code == KEY_UP) {
        debug_move_memory_cursor(debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL ? -DEBUG_MEMORY_VISUAL_W : -DEBUG_MEMORY_BYTES_PER_ROW);
        return;
    }
    if (event.code == KEY_DOWN) {
        debug_move_memory_cursor(debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL ? DEBUG_MEMORY_VISUAL_W : DEBUG_MEMORY_BYTES_PER_ROW);
        return;
    }

    if (debug_memory_mode == DEBUG_MEMORY_MODE_HEX) {
        int digit = parse_hex_digit_char(event.ch);
        if (digit >= 0 && debug_memory_address_available(debug_memory_cursor)) {
            uint8_t current = debug_memory_read_byte(debug_memory_cursor);
            uint8_t next;
            if (debug_memory_edit_nibble < 0) {
                next = (uint8_t)(((uint8_t)digit << 4) | (current & 0x0Fu));
                debug_memory_edit_nibble = 0;
            } else {
                next = (uint8_t)((current & 0xF0u) | (uint8_t)digit);
                debug_memory_edit_nibble = -1;
            }
            debug_memory_write_byte(debug_memory_cursor, next);
            debug_mark_memory_edited(debug_memory_cursor, 1);
            if (debug_memory_edit_nibble < 0) {
                debug_move_memory_cursor(1);
            }
        }
    }
}

/* Breakpoint target lookup: name token -> AppId bit. */
static AppId app_index_from_bit(uint16_t bit) {
    for (int app = 0; app < APP_COUNT; ++app) {
        if (bit == (uint16_t)(1u << app)) {
            return (AppId)app;
        }
    }
    return APP_NOTEPAD;
}

static uint16_t debug_bp_target_bit(const char *token) {
    struct { const char *names; AppId app; } targets[] = {
        { "n|notepad|text|not",          APP_NOTEPAD },
        { "c|cmd|prompt|terminal|p|t",    APP_CMD },
        { "pa|paint|draw|pnt",           APP_PAINT },
        { "e|exp|explorer|files|fm",      APP_EXPLORER },
        { "s|snake|sn",                  APP_SNAKE },
        { "g|guess|guessnum|guess number|num", APP_GUESS },
        { "m|mines|minesweeper|minesw|mns", APP_MINES },
        { "gc|games|game|gamecenter|gcen", APP_GAME_CENTER },
        { "pow|power|power menu",         APP_POWER },
        { "set|settings|config|conf",    APP_SETTINGS },
        { "tm|taskmgr|task|taskmanager|tasks", APP_TASK_MANAGER },
    };

    if (token_is(token, "all", "everything", "every", "*")) {
        return 0x8000u;
    }
    for (int i = 0; i < (int)(sizeof(targets) / sizeof(targets[0])); ++i) {
        const char *rest = targets[i].names;
        for (;;) {
            const char *bar = rest;
            size_t len;

            while (*bar != '\0' && *bar != '|') {
                ++bar;
            }
            len = (size_t)(bar - rest);
            if (len == strlen_local(token) && len > 0) {
                bool match = true;
                for (size_t j = 0; j < len; ++j) {
                    if (token[j] != rest[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return (uint16_t)(1u << targets[i].app);
                }
            }
            if (*bar == '\0') {
                break;
            }
            rest = bar + 1;
        }
    }
    return 0;
}

/*
 * Breakpoint trace: symbol of the caller + decoded instructions at the
 * crash-symbols entry, rendered in the same "[0xADDR] name at
 * HaloxOS/src/...; line N" style as the graphical crash handler
 * backtrace, with the leading opcodes as real instruction text
 * (MOV EBP,ESP; PUSH EBX; ...) instead of raw hex bytes.
 */
static int debug_bp_decode_instruction(uint32_t addr, char *out, size_t out_max);

static void debug_bp_append_trace_line(char *line, size_t *len, size_t max_len, uint32_t addr) {
    const char *name = "unknown";
    const char *file = "?";
    const char *base = "?";
    int file_line = 0;
    uint32_t sym_offset = 0;
    uint32_t symbol_addr = addr;

    if (crash_symbol_lookup(addr, &name, &file, &file_line, &sym_offset)) {
        symbol_addr = addr - sym_offset;
        for (const char *p = file; *p != '\0'; ++p) {
            if (*p == '/' && *(p + 1) != '\0') {
                base = p + 1;
            }
        }
    } else {
        name = "unknown";
    }

    copy_string(line + *len, "[0x", max_len - *len);
    *len = strlen_local(line);
    /* NOTE: symbol address, the source line belongs to the symbol start */
    {
        static const char digits[] = "0123456789ABCDEF";

        for (int shift = 28; shift >= 0; shift -= 4) {
            if (*len + 1 >= max_len) {
                return;
            }
            line[(*len)++] = digits[(symbol_addr >> shift) & 0x0Fu];
        }
        line[*len] = '\0';
    }
    copy_string(line + *len, "] ", max_len - *len);
    *len = strlen_local(line);
    copy_string(line + *len, name, max_len - *len);
    *len = strlen_local(line);
    copy_string(line + *len, " at ", max_len - *len);
    *len = strlen_local(line);
    copy_string(line + *len, base, max_len - *len);
    *len = strlen_local(line);
    copy_string(line + *len, "; line ", max_len - *len);
    *len = strlen_local(line);
    {
        char digits[12];
        int dn = 0;
        uint32_t v = (uint32_t)file_line;

        if (v == 0) {
            digits[dn++] = '?';
        }
        while (v > 0 && dn < 11) {
            digits[dn++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (dn > 0 && *len + 1 < max_len) {
            line[(*len)++] = digits[--dn];
        }
        line[*len] = '\0';
    }

    /* decoded instruction at the symbol entry instead of raw hex:
     * decodes up to 3 consecutive instructions (MOV EBP,ESP style) */
    copy_string(line + *len, " (", max_len - *len);
    *len = strlen_local(line);
    {
        uint32_t ip = symbol_addr;
        int decoded = 0;

        while (decoded < 3 && *len + 2 < max_len) {
            char ins[24];
            int inst_len;

            if (decoded > 0) {
                if (*len + 1 < max_len) {
                    line[(*len)++] = ';';
                }
            }
            inst_len = debug_bp_decode_instruction(ip, ins, sizeof(ins));
            copy_string(line + *len, ins, max_len - *len);
            *len = strlen_local(line);
            if (inst_len <= 0) {
                break;
            }
            ip += (uint32_t)inst_len;
            ++decoded;
        }
    }
    if (*len + 1 < max_len) {
        line[(*len)++] = ')';
    }
    line[*len] = '\0';
}

/*
 * Minimal 32-bit x86 instruction decoder: decodes ONE instruction at
 * addr into text like "MOV EAX,[EBX+8]" and returns its length in
 * bytes. Covers the common kernel opcodes (moves, arithmetic, stack,
 * jumps, calls); anything else renders as "DB 0xNN" with length 1 so
 * the trace always advances.
 */
static int debug_bp_decode_instruction(uint32_t addr, char *out, size_t out_max) {
    uint8_t op;
    uint8_t modrm;
    uint8_t mod;
    uint8_t reg;
    uint8_t rm;
    int length = 1;
    bool has_modrm = false;
    char *p;

    if (addr >= debug_memory_limit()) {
        copy_string(out, "??", out_max);
        return 1;
    }
    op = *(volatile uint8_t *)(uintptr_t)addr;
    p = out;

    static const char *const regs32[8] = { "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI" };
    static const char *const regs8[8] = { "AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH" };

#define PUTS(s) do { for (const char *q_ = (s); *q_ != '\0'; ++q_) { if ((size_t)(p - out) + 1 < out_max) *p++ = *q_; } } while (0)
#define PUT_HEX8(v) do { static const char dg_[] = "0123456789ABCDEF"; \
    if ((size_t)(p - out) + 2 < out_max) { *p++ = dg_[((v) >> 4) & 0x0Fu]; *p++ = dg_[(v) & 0x0Fu]; } } while (0)

    /* skip common prefixes so the decode still lands on the real op */
    while (op == 0x2E || op == 0x36 || op == 0x3E || op == 0x26 ||
           op == 0x64 || op == 0x65 || op == 0x66 || op == 0x67 || op == 0xF0) {
        ++length;
        if (addr + (uint32_t)length >= debug_memory_limit()) {
            copy_string(out, "prefix?", out_max);
            return length;
        }
        op = *(volatile uint8_t *)(uintptr_t)(addr + (uint32_t)length);
    }

    modrm = 0;
    mod = 0;
    reg = 0;
    rm = 0;
    if (op <= 0x3F || (op >= 0x80 && op <= 0xBF) ||
        op == 0x8D || op == 0xC6 || op == 0xC7 || op == 0xF6 || op == 0xF7) {
        uint32_t modrm_addr = addr + (uint32_t)length;

        if (modrm_addr < debug_memory_limit()) {
            modrm = *(volatile uint8_t *)(uintptr_t)modrm_addr;
            mod = modrm >> 6;
            reg = (modrm >> 3) & 7u;
            rm = modrm & 7u;
            has_modrm = true;
        }
    }

    switch (op) {
        /* ALU r/m,r and r,r/m groups */
        case 0x00: case 0x01: case 0x02: case 0x03:
        case 0x08: case 0x09: case 0x0A: case 0x0B:
        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x18: case 0x19: case 0x1A: case 0x1B:
        case 0x20: case 0x21: case 0x22: case 0x23:
        case 0x28: case 0x29: case 0x2A: case 0x2B:
        case 0x30: case 0x31: case 0x32: case 0x33:
        case 0x38: case 0x39: case 0x3A: case 0x3B: {
            static const char *const mnemonics[8] = {
                "ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"
            };
            bool dir = (op & 0x02u) != 0;
            bool wide = (op & 0x01u) != 0;

            length += 1;
            PUTS(mnemonics[op >> 3]);
            PUTS(" ");
            if (!has_modrm) {
                PUTS("?");
                break;
            }
            /* mod=11 -> register operand; otherwise memory (approximate) */
            if (mod == 3) {
                if (dir) {
                    PUTS(wide ? regs32[reg] : regs8[reg]);
                    PUTS(",");
                    PUTS(wide ? regs32[rm] : regs8[rm]);
                } else {
                    PUTS(wide ? regs32[rm] : regs8[rm]);
                    PUTS(",");
                    PUTS(wide ? regs32[reg] : regs8[reg]);
                }
            } else {
                /* memory form: show [reg] + disp approximations */
                if (dir) {
                    PUTS(wide ? regs32[reg] : regs8[reg]);
                    PUTS(",[");
                    PUTS(regs32[rm]);
                    PUTS("]");
                } else {
                    PUTS("[");
                    PUTS(regs32[rm]);
                    PUTS("],");
                    PUTS(wide ? regs32[reg] : regs8[reg]);
                }
                length += (mod == 1) ? 1 : (mod == 2) ? 4 : 0;
                if (mod == 0 && rm == 5) {
                    length += 4;   /* disp32 only */
                }
            }
            break;
        }
        /* push/pop registers */
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            PUTS("PUSH ");
            PUTS(regs32[op & 7u]);
            break;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            PUTS("POP ");
            PUTS(regs32[op & 7u]);
            break;
        /* MOV group */
        case 0x88: case 0x89: case 0x8A: case 0x8B: {
            bool dir = (op & 0x02u) != 0;
            bool wide = (op & 0x01u) != 0;

            length += 1;
            PUTS("MOV ");
            if (!has_modrm) {
                PUTS("?");
                break;
            }
            if (mod == 3) {
                if (dir) {
                    PUTS(wide ? regs32[reg] : regs8[reg]);
                    PUTS(",");
                    PUTS(wide ? regs32[rm] : regs8[rm]);
                } else {
                    PUTS(wide ? regs32[rm] : regs8[rm]);
                    PUTS(",");
                    PUTS(wide ? regs32[reg] : regs8[reg]);
                }
            } else {
                if (dir) {
                    PUTS(wide ? regs32[reg] : regs8[reg]);
                    PUTS(",[");
                    PUTS(regs32[rm]);
                    PUTS("]");
                } else {
                    PUTS("[");
                    PUTS(regs32[rm]);
                    PUTS("],");
                    PUTS(wide ? regs32[reg] : regs8[reg]);
                }
                length += (mod == 1) ? 1 : (mod == 2) ? 4 : 0;
                if (mod == 0 && rm == 5) {
                    length += 4;
                }
            }
            break;
        }
        /* MOV r32, imm32 */
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            PUTS("MOV ");
            PUTS(regs32[op & 7u]);
            PUTS(",0x");
            {
                uint8_t ib[4];
                bool readable = true;

                for (int i = 0; i < 4; ++i) {
                    uint32_t imm_addr = addr + (uint32_t)length + (uint32_t)i;

                    if (imm_addr >= debug_memory_limit()) {
                        readable = false;
                        break;
                    }
                    ib[i] = *(volatile uint8_t *)(uintptr_t)imm_addr;
                }
                if (readable) {
                    for (int i = 3; i >= 0; --i) {
                        PUT_HEX8(ib[i]);
                    }
                } else {
                    PUTS("????");
                }
                length += 4;
            }
            break;
        /* LEA */
        case 0x8D:
            length += 1;
            PUTS("LEA ");
            if (has_modrm && mod != 3) {
                PUTS(regs32[reg]);
                PUTS(",[");
                PUTS(regs32[rm]);
                PUTS("]");
                length += (mod == 1) ? 1 : (mod == 2) ? 4 : 0;
            } else {
                PUTS("?");
            }
            break;
        /* group 80/81/83: ALU r/m, imm */
        case 0x80: case 0x81: case 0x83: {
            static const char *const mnemonics[8] = {
                "ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"
            };

            length += 1;
            PUTS(mnemonics[reg]);
            PUTS(" ");
            if (has_modrm) {
                if (mod == 3) {
                    PUTS(op == 0x81 ? regs32[rm] : regs8[rm]);
                } else {
                    PUTS("[");
                    PUTS(regs32[rm]);
                    PUTS("]");
                    length += (mod == 1) ? 1 : (mod == 2) ? 4 : 0;
                }
                PUTS(",0xIMM");
                length += (op == 0x81) ? 4 : 1;
            } else {
                PUTS("?");
            }
            break;
        }
        /* test/jmp/call/ret/etc: single-byte forms */
        case 0xE9: PUTS("JMP rel32"); length = 5; break;
        case 0xEB: PUTS("JMP rel8"); length = 2; break;
        case 0xE8: PUTS("CALL rel32"); length = 5; break;
        case 0xC3: PUTS("RET"); break;
        case 0xC2: PUTS("RET imm16"); length = 3; break;
        case 0x90: PUTS("NOP"); break;
        case 0xCC: PUTS("INT3"); break;
        case 0xCD: PUTS("INT imm8"); length = 2; break;
        case 0xF4: PUTS("HLT"); break;
        case 0xFA: PUTS("CLI"); break;
        case 0xFB: PUTS("STI"); break;
        case 0x85: PUTS("TEST rm,r"); length += 1; break;
        default:
            PUTS("DB 0x");
            PUT_HEX8(op);
            break;
    }
#undef PUT_HEX8
#undef PUTS

    if (p == out) {
        copy_string(out, "DB", out_max);
    } else if ((size_t)(p - out) < out_max) {
        *p = '\0';
    }
    return length;
}

/*
 * Breakpoint hit: called from render_app_window when a targeted app is
 * being drawn, once per rendered frame per target. AUTO-CONTINUE: the
 * catch appends its trace to the terminal log but does NOT hold the
 * system - the run keeps going, and the next frame that touches a
 * targeted app appends the next catch, so the terminal becomes a live
 * per-frame trace. Frame numbers are relative to the FIRST catch
 * (rebased there), so the log counts #1, #2, #3... from the first hit
 * instead of from the 'continue' that armed the breakpoint.
 */
static void debug_bp_catch(AppId app, uint32_t frame_no) {
    uint32_t ebp;

    /* AUTO-CONTINUE: log the catch, keep the run alive. The overlay
     * pops open (notification) but the system never waits for input
     * here - the next targeted frame fires the next catch. */
    ++debug_bp_catch_count;
    debug_bp_last_catch_tick = timer_ticks;
    if (!debug_overlay_open) {
        debug_overlay_open = true;
        debug_memory_view_open = false;
    }

    {
        char head[TERM_LINE_LEN];
        size_t len = 0;

        copy_string(head, "** BP frame #", sizeof(head));
        len = strlen_local(head);
        append_uint(head, &len, sizeof(head), frame_no);
        copy_string(head + len, " (#", sizeof(head) - len);
        len = strlen_local(head);
        append_uint(head, &len, sizeof(head), debug_bp_catch_count);
        copy_string(head + len, " hit):", sizeof(head) - len);
        terminal_add_line(&debug_term, head);
    }
    {
        char line[TERM_LINE_LEN];
        size_t len = 0;

        copy_string(line, "", sizeof(line));
        debug_bp_append_trace_line(line, &len, sizeof(line), (uint32_t)(uintptr_t)&debug_bp_catch);
        terminal_add_line(&debug_term, line);
    }
    {
        char label[TERM_LINE_LEN];
        size_t len = 0;

        copy_string(label, "app: ", sizeof(label));
        len = strlen_local(label);
        for (const char *p = app_titles[app]; *p != '\0'; ++p) {
            append_char(label, &len, sizeof(label), *p);
        }
        terminal_add_line(&debug_term, label);
    }

    /* current EBP chain like the crash handler, one line per frame */
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));
    for (int i = 0; i < 8; ++i) {
        uint32_t next;
        uint32_t ret;

        if (ebp < 0x10000u || ebp > 0x7F000000u || (ebp & 3u) != 0) {
            break;
        }
        next = *(volatile uint32_t *)ebp;
        ret = *(volatile uint32_t *)(ebp + 4u);
        if (next <= ebp || next > ebp + 0x4000u) {
            break;
        }
        if (ret < 0x200000u || ret > 0x780000u) {
            break;
        }
        {
            char line[TERM_LINE_LEN];
            size_t len = 0;

            debug_bp_append_trace_line(line, &len, sizeof(line), ret);
            terminal_add_line(&debug_term, line);
        }
        ebp = next;
    }

    serial_trace_concat("INFO", "Breakpoint caught on app id ", app_titles[app]);
}

/*
 * Power breakpoint: catches the destructive power actions (shutdown,
 * restart, halt) at their trigger sites - Power app buttons, the power
 * overlay menu, and the terminal commands - BEFORE the action runs, so
 * the debugger terminal can log and trace the trigger even though the
 * machine is about to go down/reboot. Only fires when a breakpoint is
 * armed on the Power app (or 'all'), auto-continues like every other
 * breakpoint catch: the power action itself still runs right after.
 */
static void debug_bp_catch_power(const char *action) {
    uint32_t ebp;

    if (!debug_bp_armed || debug_bp_mask == 0 ||
        ((debug_bp_mask & 0x8000u) == 0 && (debug_bp_mask & (uint16_t)(1u << APP_POWER)) == 0)) {
        return;
    }
    if (debug_bp_last_catch_tick == timer_ticks) {
        return;   /* one catch per timer frame, like app catches */
    }
    if (debug_bp_frame_base == 0) {
        debug_bp_frame_base = fps_frames_total - 1;   /* first catch = #1 */
    }

    ++debug_bp_catch_count;
    debug_bp_last_catch_tick = timer_ticks;
    if (!debug_overlay_open) {
        debug_overlay_open = true;
        debug_memory_view_open = false;
    }

    {
        char head[TERM_LINE_LEN];
        size_t len = 0;

        copy_string(head, "** BP frame #", sizeof(head));
        len = strlen_local(head);
        append_uint(head, &len, sizeof(head), fps_frames_total - debug_bp_frame_base);
        copy_string(head + len, " (#", sizeof(head) - len);
        len = strlen_local(head);
        append_uint(head, &len, sizeof(head), debug_bp_catch_count);
        copy_string(head + len, " hit):", sizeof(head) - len);
        terminal_add_line(&debug_term, head);
    }
    {
        char label[TERM_LINE_LEN];
        size_t len = 0;

        copy_string(label, "power trigger: ", sizeof(label));
        len = strlen_local(label);
        for (const char *p = action; *p != '\0'; ++p) {
            append_char(label, &len, sizeof(label), *p);
        }
        terminal_add_line(&debug_term, label);
    }
    {
        char line[TERM_LINE_LEN];
        size_t len = 0;

        debug_bp_append_trace_line(line, &len, sizeof(line), (uint32_t)(uintptr_t)&debug_bp_catch_power);
        terminal_add_line(&debug_term, line);
    }

    /* caller EBP chain: same walk as the app catch */
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));
    for (int i = 0; i < 8; ++i) {
        uint32_t next;
        uint32_t ret;

        if (ebp < 0x10000u || ebp > 0x7F000000u || (ebp & 3u) != 0) {
            break;
        }
        next = *(volatile uint32_t *)ebp;
        ret = *(volatile uint32_t *)(ebp + 4u);
        if (next <= ebp || next > ebp + 0x4000u) {
            break;
        }
        if (ret < 0x200000u || ret > 0x780000u) {
            break;
        }
        {
            char line[TERM_LINE_LEN];
            size_t len = 0;

            debug_bp_append_trace_line(line, &len, sizeof(line), ret);
            terminal_add_line(&debug_term, line);
        }
        ebp = next;
    }

    serial_trace_concat("INFO", "Breakpoint caught on power trigger: ", action);
}

static void debug_execute_pending(void) {
    DebugAction action = debug_pending_action;
    debug_pending_action = DEBUG_ACTION_NONE;
    if (action == DEBUG_ACTION_CRASH) {
        debug_forced_fault_reason = "debug crash command requested";
        serial_trace("ERROR", "debug crash command requested");
        __asm__ volatile(".byte 0x0F, 0x0B");
    } else if (action == DEBUG_ACTION_CRASH_VECTOR) {
        uint8_t vector = debug_crash_vector;
        debug_forced_fault_reason = "debug crash vector command requested";
        serial_trace_hex_value("ERROR", "debug crash vector", vector);
        switch (vector) {
            case 0x00: /* divide error through a real instruction */
                __asm__ volatile("xor %%edx, %%edx; xor %%ecx, %%ecx; div %%ecx" : : : "eax", "ecx", "edx");
                break;
            case 0x06: /* invalid opcode */
                __asm__ volatile(".byte 0x0F, 0x0B");
                break;
            /* x86 'int' only accepts an immediate vector, so every IDT
             * vector gets its own constant instruction here. */
            case 0x01: __asm__ volatile("int $0x01"); break;
            case 0x02: __asm__ volatile("int $0x02"); break;
            case 0x03: __asm__ volatile("int $0x03"); break;
            case 0x04: __asm__ volatile("int $0x04"); break;
            case 0x05: __asm__ volatile("int $0x05"); break;
            case 0x07: __asm__ volatile("int $0x07"); break;
            case 0x08: __asm__ volatile("int $0x08"); break;
            case 0x09: __asm__ volatile("int $0x09"); break;
            case 0x0A: __asm__ volatile("int $0x0A"); break;
            case 0x0B: __asm__ volatile("int $0x0B"); break;
            case 0x0C: __asm__ volatile("int $0x0C"); break;
            case 0x0D: __asm__ volatile("int $0x0D"); break;
            case 0x0E: __asm__ volatile("int $0x0E"); break;
            case 0x10: __asm__ volatile("int $0x10"); break;
            case 0x11: __asm__ volatile("int $0x11"); break;
            case 0x12: __asm__ volatile("int $0x12"); break;
            case 0x13: __asm__ volatile("int $0x13"); break;
            case 0x14: __asm__ volatile("int $0x14"); break;
            case 0x15: __asm__ volatile("int $0x15"); break;
            case 0x16: __asm__ volatile("int $0x16"); break;
            case 0x17: __asm__ volatile("int $0x17"); break;
            case 0x18: __asm__ volatile("int $0x18"); break;
            case 0x19: __asm__ volatile("int $0x19"); break;
            case 0x1A: __asm__ volatile("int $0x1A"); break;
            case 0x1B: __asm__ volatile("int $0x1B"); break;
            case 0x1C: __asm__ volatile("int $0x1C"); break;
            case 0x1D: __asm__ volatile("int $0x1D"); break;
            case 0x1E: __asm__ volatile("int $0x1E"); break;
            case 0x1F: __asm__ volatile("int $0x1F"); break;
            default: /* 0x0F is reserved: land on the invalid opcode path */
                __asm__ volatile(".byte 0x0F, 0x0B");
                break;
        }
    } else if (action == DEBUG_ACTION_HALT) {
        serial_trace("INFO", "debug halt command requested");
        __asm__ volatile("cli");
        for (;;) {
            __asm__ volatile("hlt");
        }
    } else if (action == DEBUG_ACTION_FAULT1) {
        debug_forced_fault_reason = "debug divide fault requested";
        serial_trace("ERROR", "debug divide fault requested");
        __asm__ volatile("xor %%edx, %%edx; xor %%ecx, %%ecx; div %%ecx" : : : "eax", "ecx", "edx");
    } else if (action == DEBUG_ACTION_FAULT2) {
        debug_forced_fault_reason = "debug invalid opcode requested";
        serial_trace("ERROR", "debug invalid opcode requested");
        __asm__ volatile(".byte 0x0F, 0x0B");
    } else if (action == DEBUG_ACTION_FAULT3) {
        debug_forced_fault_reason = "debug triple fault requested";
        serial_trace("ERROR", "debug triple fault requested");
        serial_dump_cpu_exception("Triple Fault", debug_forced_fault_reason, NULL);
        IdtPointer empty = {0, 0};
        idt_load(&empty);
        __asm__ volatile("int $3");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
}

static void debug_append_mode_line(uint16_t width, uint16_t height, uint16_t bpp) {
    char line[TERM_LINE_LEN] = {0};
    size_t len = 0;
    append_uint(line, &len, sizeof(line), width);
    append_char(line, &len, sizeof(line), 'x');
    append_uint(line, &len, sizeof(line), height);
    append_char(line, &len, sizeof(line), 'x');
    append_uint(line, &len, sizeof(line), bpp);
    terminal_add_line(&debug_term, line);
}

static void debug_apply_background(uint32_t background) {
    if (background != 1 && background != 2) {
        terminal_add_line(&debug_term, "Usage: change bg 1 | change bg 2");
        return;
    }
    settings_pending.background_mode = (uint8_t)(background - 1u);
    apply_settings();
    terminal_add_line(&debug_term, background == 1 ? "Background changed: theme1.png" : "Background changed: theme2.png");
    serial_trace_concat("INFO", "Screen Changed as background: ", background == 1 ? "theme1.png" : "theme2.png");
}

static void debug_apply_video(bool has_resolution,
                              uint32_t width,
                              uint32_t height,
                              bool has_bpp,
                              uint32_t requested_bpp) {
    uint16_t target_width = has_resolution ? (uint16_t)width : (uint16_t)fb.width;
    uint16_t target_height = has_resolution ? (uint16_t)height : (uint16_t)fb.height;
    uint16_t target_bpp = fb.bpp;
    uint8_t target_palette = settings_applied.palette_mode;

    if (has_bpp) {
        if (requested_bpp == 4) {
            target_palette = 1;
            target_bpp = 8;
        } else if (requested_bpp == 8) {
            target_palette = 0;
            target_bpp = 8;
        } else if (requested_bpp == 16) {
            target_palette = 2;
            target_bpp = 16;
        } else {
            terminal_add_line(&debug_term, "Usage: change vid bpp 4|8|16");
            return;
        }
    }

    if (!set_framebuffer_mode_raw(target_width, target_height, target_bpp)) {
        terminal_add_line(&debug_term, "ERROR: video mode switch unavailable.");
        serial_trace("ERROR", "debugger video mode switch failed");
        serial_trace_video_mode("Screen change failed");
        return;
    }

    settings_applied.palette_mode = target_palette;
    settings_pending.palette_mode = target_palette;
    program_vga_palette();
    terminal_add_line(&debug_term, "Screen changed:");
    debug_append_mode_line((uint16_t)fb.width, (uint16_t)fb.height, fb.bpp);
    serial_trace_video_mode("Screen Changed as resolution");
}

static void debug_change_command(const char *command) {
    char token[24];
    const char *cursor = command;

    cursor = read_token(cursor, token, sizeof(token));
    cursor = read_token(cursor, token, sizeof(token));
    if (token_is(token, "b", "bg", "bac", "background")) {
        uint32_t background = 0;
        cursor = read_token(cursor, token, sizeof(token));
        if (!parse_uint_decimal(token, &background)) {
            terminal_add_line(&debug_term, "Usage: change bg 1 | change bg 2");
            return;
        }
        debug_apply_background(background);
        return;
    }

    if (token_is(token, "v", "vid", "video", NULL)) {
        bool has_resolution = false;
        bool has_bpp = false;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t bpp = 0;

        for (;;) {
            cursor = read_token(cursor, token, sizeof(token));
            if (token[0] == '\0') {
                break;
            }
            if (token_is(token, "r", "res", "resolution", NULL)) {
                cursor = read_token(cursor, token, sizeof(token));
                if (!parse_resolution_token(token, &width, &height)) {
                    terminal_add_line(&debug_term, "Usage: change vid 800x600");
                    return;
                }
                has_resolution = true;
            } else if (token_is(token, "b", "bpp", NULL, NULL)) {
                cursor = read_token(cursor, token, sizeof(token));
                if (!parse_bpp_token(token, &bpp)) {
                    terminal_add_line(&debug_term, "Usage: change vid bpp 4|8|16");
                    return;
                }
                has_bpp = true;
            } else if (parse_resolution_token(token, &width, &height)) {
                has_resolution = true;
            } else {
                terminal_add_line(&debug_term, "Usage: change vid 800x600 bpp 16 | change bg 2");
                return;
            }
        }

        if (!has_resolution && !has_bpp) {
            terminal_add_line(&debug_term, "Usage: change vid 800x600 | change vid bpp 16");
            return;
        }
        debug_apply_video(has_resolution, width, height, has_bpp, bpp);
        return;
    }

    terminal_add_line(&debug_term, "Usage: change vid 800x600 bpp 16 | change bg 2");
}

static void debug_print_video_details(void) {
    char line[TERM_LINE_LEN] = {0};
    size_t len = 0;

    terminal_add_line(&debug_term, "Video:");
    copy_string(line, "Backend: ", sizeof(line));
    len = strlen_local(line);
    for (const char *name = video_backend_name(); *name != '\0'; ++name) {
        append_char(line, &len, sizeof(line), *name);
    }
    terminal_add_line(&debug_term, line);

    line[0] = '\0';
    len = 0;
    append_uint(line, &len, sizeof(line), fb.width);
    append_char(line, &len, sizeof(line), 'x');
    append_uint(line, &len, sizeof(line), fb.height);
    append_char(line, &len, sizeof(line), 'x');
    append_uint(line, &len, sizeof(line), fb.bpp);
    terminal_add_line(&debug_term, line);

    line[0] = '\0';
    len = 0;
    append_char(line, &len, sizeof(line), 'P');
    append_char(line, &len, sizeof(line), 'i');
    append_char(line, &len, sizeof(line), 't');
    append_char(line, &len, sizeof(line), 'c');
    append_char(line, &len, sizeof(line), 'h');
    append_char(line, &len, sizeof(line), ':');
    append_char(line, &len, sizeof(line), ' ');
    append_uint(line, &len, sizeof(line), fb.pitch);
    terminal_add_line(&debug_term, line);

    line[0] = '\0';
    len = 0;
    append_char(line, &len, sizeof(line), 'F');
    append_char(line, &len, sizeof(line), 'B');
    append_char(line, &len, sizeof(line), ':');
    append_char(line, &len, sizeof(line), ' ');
    append_hex32(line, &len, sizeof(line), (uint32_t)(uintptr_t)fb.address);
    terminal_add_line(&debug_term, line);
    terminal_add_line(&debug_term, video_mode_switch_available ? "Mode switch: available" : "Mode switch: unavailable");
}

static void debug_print_disk_details(void) {
    char line[TERM_LINE_LEN] = {0};
    size_t len = 0;

    terminal_add_line(&debug_term, "Disk:");
    copy_string(line, "Drive: ", sizeof(line));
    len = strlen_local(line);
    append_hex32(line, &len, sizeof(line), boot_drive_number);
    append_char(line, &len, sizeof(line), ' ');
    for (const char *name = disk_physical_type_label(); *name != '\0'; ++name) {
        append_char(line, &len, sizeof(line), *name);
    }
    terminal_add_line(&debug_term, line);

    if (boot_drive_info_available && boot_drive_storage_bytes != 0) {
        char storage[32] = {0};
        format_single_memory_amount(storage, sizeof(storage), boot_drive_storage_bytes);
        copy_string(line, "Storage: ", sizeof(line));
        len = strlen_local(line);
        for (int i = 0; storage[i] != '\0'; ++i) {
            append_char(line, &len, sizeof(line), storage[i]);
        }
        terminal_add_line(&debug_term, line);
    } else {
        terminal_add_line(&debug_term, "Storage: BIOS did not report geometry");
    }

    line[0] = '\0';
    len = 0;
    append_char(line, &len, sizeof(line), 'I');
    append_char(line, &len, sizeof(line), '/');
    append_char(line, &len, sizeof(line), 'O');
    append_char(line, &len, sizeof(line), ':');
    append_char(line, &len, sizeof(line), ' ');
    append_uint(line, &len, sizeof(line), disk_io_megabytes);
    append_char(line, &len, sizeof(line), ' ');
    append_char(line, &len, sizeof(line), 'M');
    append_char(line, &len, sizeof(line), 'B');
    terminal_add_line(&debug_term, line);
    terminal_add_line(&debug_term, desktop_icon_persistence_enabled ? "Desktop layout: writable" : "Desktop layout: read-only");
}

static void debug_view_command(const char *command) {
    char token[24];
    const char *cursor = command;

    cursor = read_token(cursor, token, sizeof(token));
    cursor = read_token(cursor, token, sizeof(token));
    if (token[0] == '\0' || token_is(token, "m", "mem", "memory", NULL)) {
        uint32_t address = 0;
        cursor = read_token(cursor, token, sizeof(token));
        if (token[0] != '\0') {
            if (!parse_uint_auto(token, &address)) {
                terminal_add_line(&debug_term, "Usage: view mem 0xADDR");
                return;
            }
        }
        debug_open_memory_view(address);
        return;
    }

    if (token_is(token, "v", "vid", "video", NULL)) {
        debug_print_video_details();
        return;
    }

    if (token_is(token, "d", "disk", "drive", NULL)) {
        debug_print_disk_details();
        return;
    }

    terminal_add_line(&debug_term, "Usage: view mem | view vid | view disk");
}

static void debug_edit_command(const char *command) {
    char token[24];
    const char *cursor = command;
    uint32_t address = 0;
    uint32_t length = 0;
    uint8_t pattern[4];
    int pattern_len = 0;

    cursor = read_token(cursor, token, sizeof(token));
    cursor = read_token(cursor, token, sizeof(token));
    if (!token_is(token, "m", "mem", "memory", NULL)) {
        terminal_add_line(&debug_term, "Usage: edit mem 0xADDR ff 0x100");
        return;
    }

    cursor = read_token(cursor, token, sizeof(token));
    if (!parse_uint_auto(token, &address)) {
        terminal_add_line(&debug_term, "Usage: edit mem 0xADDR ff 0x100");
        return;
    }

    cursor = read_token(cursor, token, sizeof(token));
    if (!parse_hex_pattern(token, pattern, &pattern_len)) {
        terminal_add_line(&debug_term, "Usage: edit mem 0xADDR ff 0x100");
        return;
    }

    cursor = read_token(cursor, token, sizeof(token));
    if (token[0] != '\0') {
        if (!parse_uint_auto(token, &length)) {
            terminal_add_line(&debug_term, "Usage: edit mem 0xADDR ff 0x100");
            return;
        }
    } else {
        length = (uint32_t)pattern_len;
    }

    if (length == 0) {
        terminal_add_line(&debug_term, "ERROR: edit length is zero.");
        return;
    }
    if (length > DEBUG_MEMORY_MAX_EDIT_LENGTH) {
        length = DEBUG_MEMORY_MAX_EDIT_LENGTH;
    }
    if (!debug_memory_address_available(address)) {
        terminal_add_line(&debug_term, "ERROR: address outside reported RAM.");
        return;
    }
    if (length > debug_memory_limit() - address) {
        length = debug_memory_limit() - address;
    }
    for (uint32_t i = 0; i < length; ++i) {
        debug_memory_write_byte(address + i, pattern[i % (uint32_t)pattern_len]);
    }
    debug_mark_memory_edited(address, length);
    terminal_add_line(&debug_term, "Memory bytes edited.");
    serial_trace_hex_value("INFO", "Debugger edited memory at", address);
    serial_trace_uint_value("INFO", "Debugger edited byte count", length);
}

static void debug_help_command(const char *command) {
    char token[24];
    const char *cursor = command;

    cursor = read_token(cursor, token, sizeof(token));
    cursor = read_token(cursor, token, sizeof(token));

    if (token[0] == '\0') {
        terminal_add_line(&debug_term, "There are many more commands that you can use freely:");
        terminal_add_line(&debug_term, "help edit | help exception | help fault | help power");
        terminal_add_line(&debug_term, "help breakpoint | help fps");
        terminal_add_line(&debug_term, "");
        terminal_add_line(&debug_term, "== ALL COMMANDS: ==");
        terminal_add_line(&debug_term, "edit view change continue test");
        terminal_add_line(&debug_term, "show hide breakpoint");
    } else if (token_is(token, "e", "edit", NULL, NULL)) {
        terminal_add_line(&debug_term, "edit mem 0xADDR ff 0x100");
        terminal_add_line(&debug_term, "view mem [0xADDR] | view vid | view disk");
        terminal_add_line(&debug_term, "change vid 800x600 bpp 16 | change bg 2");
    } else if (token_is(token, "ex", "except", "exception", "exceptions")) {
        terminal_add_line(&debug_term, "crash | halt | fault 1 | fault 2 | fault 3");
        terminal_add_line(&debug_term, "continue runs the crash, halt, or fault you picked.");
    } else if (token_is(token, "crash", NULL, NULL, NULL)) {
        terminal_add_line(&debug_term, "crash: crash the system on purpose, then continue.");
        terminal_add_line(&debug_term, "crash vector=XX: crash with a chosen CPU vector.");
        terminal_add_line(&debug_term, "vector aliases: vec= v= code= id=");
        terminal_add_line(&debug_term, "value: hex (0E, 0x0E) or decimal (14), range 0-31.");
        terminal_add_line(&debug_term, "examples: crash vector=0E | crash v=3 | crash code=1");
    } else if (token_is(token, "halt", NULL, NULL, NULL)) {
        terminal_add_line(&debug_term, "halt: arm silent CPU halt, then continue.");
    } else if (token_is(token, "f", "fault", NULL, NULL)) {
        terminal_add_line(&debug_term, "fault 1=divide, 2=invalid opcode, 3=triple fault");
    } else if (token_is(token, "shutdown", "poweroff", NULL, NULL)) {
        terminal_add_line(&debug_term, "shutdown: power off through ACPI S5 immediately.");
    } else if (token_is(token, "reboot", "restart", NULL, NULL)) {
        terminal_add_line(&debug_term, "reboot: reset the machine immediately.");
    } else if (token_is(token, "pow", "power", "p", NULL)) {
        terminal_add_line(&debug_term, "shutdown | poweroff: turn off the computer.");
        terminal_add_line(&debug_term, "reboot | restart: restart the computer.");
        terminal_add_line(&debug_term, "halt: stop the CPU until hard reset.");
    } else if (token_is(token, "view", "v", NULL, NULL)) {
        terminal_add_line(&debug_term, "view mem [0xADDR] opens hex/visual memory viewer.");
        terminal_add_line(&debug_term, "F1 hex, F2 visual, arrows move, ESC exits viewer.");
        terminal_add_line(&debug_term, "In the log: Ctrl+Left/Right scrolls long lines.");
        terminal_add_line(&debug_term, "<< and >> mark hidden text sides. Home/End jump.");
    } else if (token_is(token, "change", "ch", NULL, NULL)) {
        terminal_add_line(&debug_term, "change vid 800x600 | change vid bpp 4|8|16");
        terminal_add_line(&debug_term, "change bg 1 | change bg 2");
    } else if (token_is(token, "bp", "br", "breakpoint", NULL)) {
        terminal_add_line(&debug_term, "breakpoint all: catch every app on next frame.");
        terminal_add_line(&debug_term, "breakpoint <app>: catch one app when it updates.");
        terminal_add_line(&debug_term, "Auto-continues: each new catch re-opens this log.");
        terminal_add_line(&debug_term, "Frame #1 is the FIRST catch after continue.");
        terminal_add_line(&debug_term, "breakpoint stop | s | off | o: stop all.");
        terminal_add_line(&debug_term, "power: shutdown/restart/halt triggers log");
        terminal_add_line(&debug_term, "a trace before the action runs.");
        terminal_add_line(&debug_term, "apps: notepad prompt paint explorer snake guess");
        terminal_add_line(&debug_term, "mines games power settings taskmgr all");
    } else if (token_is(token, "fps", "show", "hide", NULL)) {
        terminal_add_line(&debug_term, "show fps | s fps: toggle FPS overlay top-right.");
        terminal_add_line(&debug_term, "hide fps | h fps: hide it. Red = frame lag blink.");
    } else if (token_is(token, "continue", "con", "c", NULL)) {
        terminal_add_line(&debug_term, "continue: close debugger and resume desktop.");
    } else if (token_is(token, "t", "test", NULL, NULL)) {
        terminal_add_line(&debug_term, "test window [amount]");
        terminal_add_line(&debug_term, "Creates cascading test windows on the desktop.");
        terminal_add_line(&debug_term, "amount: number of windows (max 1000)");
    } else {
        terminal_add_line(&debug_term, "No help for that command.");
    }
}

static void debug_add_history(const char *command) {
    if (debug_history_count < DEBUG_HISTORY_COUNT) {
        copy_string(debug_history[debug_history_count++], command, TERM_LINE_LEN);
    } else {
        for (int i = 1; i < DEBUG_HISTORY_COUNT; ++i) {
            copy_string(debug_history[i - 1], debug_history[i], TERM_LINE_LEN);
        }
        copy_string(debug_history[DEBUG_HISTORY_COUNT - 1], command, TERM_LINE_LEN);
    }
    debug_history_cursor = debug_history_count;
}

static void debug_execute_command(void) {
    char command[TERM_LINE_LEN];
    copy_string(command, debug_term.input, sizeof(command));
    normalize_command(command);
    debug_term.input_len = 0;
    debug_term.input[0] = '\0';
    if (command[0] == '\0') {
        return;
    }
    terminal_add_line(&debug_term, "");
    {
        char echo[TERM_LINE_LEN + 8];
        copy_string(echo, "DBG: ", sizeof(echo));
        copy_string(echo + 5, command, sizeof(echo) - 5);
        terminal_add_line(&debug_term, echo);
    }
    debug_add_history(command);
    serial_trace_concat("INFO", "DBG command: ", command);

    if (streq(command, "help") || starts_with(command, "help ")) {
        debug_help_command(command);
    } else if (streq(command, "c") || streq(command, "con") || streq(command, "continue")) {
        debug_overlay_open = false;
        debug_memory_view_open = false;
        /* Arm the breakpoint now: frame counting does NOT start here.
         * The base is captured at the FIRST catch, so the first hit
         * logs as frame #1 no matter how long the target app stayed
         * closed or unfocused after 'continue'. */
        if (debug_bp_mask != 0) {
            debug_bp_armed = true;
            debug_bp_caught = false;
            debug_bp_frame_base = 0;
            debug_bp_catch_count = 0;
            debug_bp_last_catch_tick = 0;
        }
        serial_trace("INFO", "Debugger continued");
        debug_execute_pending();
    } else if (streq(command, "crash") || starts_with(command, "crash ")) {
        uint32_t vector = 0;

        if (streq(command, "crash")) {
            debug_pending_action = DEBUG_ACTION_CRASH;
            terminal_add_line(&debug_term, "Crash set. Type continue.");
            return;
        }

        {
            const char *arg = skip_spaces(command + 6);
            static const char *const prefixes[] = {
                "vector=", "vec=", "v=", "code=", "id=", NULL
            };
            const char *value = NULL;
            int hex = 0;

            for (int i = 0; prefixes[i] != NULL; ++i) {
                size_t plen = strlen_local(prefixes[i]);
                if (starts_with(arg, prefixes[i])) {
                    value = arg + plen;
                    break;
                }
            }

            if (value == NULL) {
                terminal_add_line(&debug_term, "Usage: crash vector=XX (hex) | crash vector=13 (decimal)");
                return;
            }

            if (starts_with(value, "0x") || starts_with(value, "0X")) {
                value += 2;
                hex = 1;
            } else if (value[2] == '\0' && parse_hex_digit_char(value[0]) >= 0 &&
                       parse_hex_digit_char(value[1]) >= 0) {
                /* two bare hex-style digits like 0E or 1D */
                hex = 1;
            }

            if (hex) {
                int d0 = parse_hex_digit_char(value[0]);
                int d1 = value[1] != '\0' ? parse_hex_digit_char(value[1]) : 0;
                if (d0 < 0 || (value[1] != '\0' && d1 < 0) || value[2] != '\0') {
                    terminal_add_line(&debug_term, "Usage: crash vector=XX (hex) | crash vector=13 (decimal)");
                    return;
                }
                vector = (uint32_t)((d0 << 4) | d1);
            } else {
                if (!parse_uint_decimal(value, &vector)) {
                    terminal_add_line(&debug_term, "Usage: crash vector=XX (hex) | crash vector=13 (decimal)");
                    return;
                }
            }

            if (vector > 31) {
                terminal_add_line(&debug_term, "Vector range is 0..31.");
                return;
            }

            debug_crash_vector = (uint8_t)vector;
            debug_pending_action = DEBUG_ACTION_CRASH_VECTOR;
            {
                char msg[TERM_LINE_LEN];
                size_t len = 0;

                copy_string(msg, "Crash vector ", sizeof(msg));
                len = strlen_local(msg);
                msg[len++] = '0';
                msg[len++] = 'x';
                msg[len] = '\0';
                if (vector < 16) {
                    msg[len++] = '0';
                }
                {
                    static const char digits[] = "0123456789ABCDEF";
                    msg[len++] = digits[vector & 0x0Fu];
                }
                msg[len] = '\0';
                copy_string(msg + len, " set. Type continue.", sizeof(msg) - len);
                terminal_add_line(&debug_term, msg);
            }
        }
    } else if (streq(command, "halt")) {
        debug_pending_action = DEBUG_ACTION_HALT;
        terminal_add_line(&debug_term, "Halt set. Type continue.");
    } else if (streq(command, "f") || streq(command, "fault") || starts_with(command, "f ") || starts_with(command, "fault ")) {
        uint32_t fault = 1;
        if (starts_with(command, "f ") || starts_with(command, "fault ")) {
            const char *arg = starts_with(command, "f ") ? command + 2 : command + 6;
            if (!parse_uint_decimal(skip_spaces(arg), &fault) || fault < 1 || fault > 3) {
                terminal_add_line(&debug_term, "Usage: fault 1|2|3");
                return;
            }
        }
        debug_pending_action = fault == 3 ? DEBUG_ACTION_FAULT3 : (fault == 2 ? DEBUG_ACTION_FAULT2 : DEBUG_ACTION_FAULT1);
        terminal_add_line(&debug_term, "Fault set. Type continue.");
    } else if (streq(command, "shutdown") || streq(command, "poweroff")) {
        terminal_add_line(&debug_term, "Shutting down...");
        serial_trace("INFO", "debug shutdown command requested");
        shutdown_system();
    } else if (streq(command, "reboot") || streq(command, "restart")) {
        terminal_add_line(&debug_term, "Rebooting...");
        serial_trace("INFO", "debug reboot command requested");
        restart_system();
    } else if (starts_with(command, "change ") || starts_with(command, "ch ")) {
        debug_change_command(command);
    } else if (streq(command, "view") || streq(command, "v") || starts_with(command, "view ") || starts_with(command, "v ")) {
        debug_view_command(command);
    } else if (streq(command, "edit ") || starts_with(command, "edit ")) {
        debug_edit_command(command);
    } else if (starts_with(command, "show ") || starts_with(command, "s ")) {
        const char *arg = command[0] == 's' ? skip_spaces(command + 2) : skip_spaces(command + 5);
        if (token_is(arg, "fps", "frames", "frame", NULL)) {
            fps_overlay_on = true;
            terminal_add_line(&debug_term, "FPS overlay shown (top-right).");
        } else {
            terminal_add_line(&debug_term, "Usage: show fps");
        }
    } else if (starts_with(command, "hide ") || starts_with(command, "h ")) {
        const char *arg = command[0] == 'h' ? skip_spaces(command + 2) : skip_spaces(command + 5);
        if (token_is(arg, "fps", "frames", "frame", NULL)) {
            fps_overlay_on = false;
            terminal_add_line(&debug_term, "FPS overlay hidden.");
        } else {
            terminal_add_line(&debug_term, "Usage: hide fps");
        }
    } else if (streq(command, "bp") || streq(command, "br") || streq(command, "breakpoint") ||
               starts_with(command, "bp ") || starts_with(command, "br ") || starts_with(command, "breakpoint ")) {
        char token[24];
        const char *cursor = command;

        cursor = read_token(cursor, token, sizeof(token));
        cursor = read_token(cursor, token, sizeof(token));
        if (token[0] == '\0') {
            terminal_add_line(&debug_term, "Usage: breakpoint all|<app> | breakpoint stop");
            terminal_add_line(&debug_term, "apps: notepad prompt paint explorer snake");
            terminal_add_line(&debug_term, "guess mines games power settings taskmgr");
            return;
        }
        if (token_is(token, "stop", "s", "off", "o")) {
            debug_bp_mask = 0;
            debug_bp_caught = false;
            terminal_add_line(&debug_term, "Breakpoints cleared.");
            serial_trace("INFO", "debugger breakpoints cleared");
            return;
        }
        {
            uint16_t bit = debug_bp_target_bit(token);

            if (bit == 0) {
                terminal_add_line(&debug_term, "Unknown breakpoint target.");
                terminal_add_line(&debug_term, "apps: notepad prompt paint explorer snake");
                terminal_add_line(&debug_term, "guess mines games power settings taskmgr all");
                return;
            }
            debug_bp_mask |= bit;
            debug_bp_caught = false;
            debug_bp_catch_count = 0;
            debug_bp_armed = false;   /* counts start after 'continue' */
            if (bit == 0x8000u) {
                terminal_add_line(&debug_term, "Breakpoint set on ALL apps. Type continue.");
            } else {
                char msg[TERM_LINE_LEN];
                size_t len = 0;

                copy_string(msg, "Breakpoint set on ", sizeof(msg));
                len = strlen_local(msg);
                for (const char *p = app_titles[app_index_from_bit(bit)]; *p != '\0'; ++p) {
                    append_char(msg, &len, sizeof(msg), *p);
                }
                copy_string(msg + len, ". Type continue.", sizeof(msg) - len);
                terminal_add_line(&debug_term, msg);
            }
            serial_trace_concat("INFO", "breakpoint armed for ", command);
        }
    } else if (starts_with(command, "test ")) {
        char subcmd[24];
        const char *cursor = command;
        cursor = read_token(cursor, subcmd, sizeof(subcmd)); // consume open/test
        cursor = read_token(cursor, subcmd, sizeof(subcmd));
        if (token_is(subcmd, "w", "win", "window", NULL)) {
            uint32_t amount = 10;
            cursor = read_token(cursor, subcmd, sizeof(subcmd));
            if (subcmd[0] != '\0') {
                if (!parse_uint_decimal(subcmd, &amount) || amount > 65536) {
                    terminal_add_line(&debug_term, "Usage: test window [amount (max 65536)]");
                    return;
                }
            }
            if (amount > MAX_TEST_WINDOWS) amount = MAX_TEST_WINDOWS;
            test_window_count = (int)amount;
            active_test_window = -1;
            for (int i = 0; i < test_window_count; ++i) {
                test_windows[i].open = true;
                test_windows[i].x = 20 + (i % 30) * 6;
                test_windows[i].y = 30 + (i % 20) * 6;
                test_windows[i].w = 200;
                test_windows[i].h = 120;
                size_t nlen = 0;
                memcpy_local(test_window_titles[i], "Window ", 8);
                nlen = 7;
                append_uint(test_window_titles[i], &nlen, sizeof(test_window_titles[i]), (uint32_t)(i + 1));
                test_window_titles[i][nlen] = '\0';
                test_windows[i].title = test_window_titles[i];
            }
            {
                char msg[TERM_LINE_LEN];
                int len = 0;
                copy_string(msg, "Opened ", sizeof(msg));
                len = (int)strlen_local(msg);
                append_uint(msg, (size_t *)&len, sizeof(msg), amount);
                copy_string(msg + len, " test windows.", sizeof(msg) - (size_t)len);
                terminal_add_line(&debug_term, msg);
            }
        } else {
            terminal_add_line(&debug_term, "Usage: test window [amount]");
        }
    } else {
        terminal_add_line(&debug_term, "Unknown debugger command.");
    }
}

static void debug_handle_key(KeyEvent event) {
    if (debug_memory_view_open) {
        debug_memory_handle_key(event);
        return;
    }

    /* Extended horizontal log: Ctrl+Left/Right pans the log view so
     * long breakpoint trace lines can be inspected. Pan is clamped
     * in the renderer against the longest visible line. */
    if (keyboard_ctrl && event.code == KEY_LEFT) {
        if (debug_log_scroll_x > 0) {
            debug_log_scroll_x -= 8;
        }
        return;
    }
    if (keyboard_ctrl && event.code == KEY_RIGHT) {
        debug_log_scroll_x += 8;
        return;
    }
    if (event.code == KEY_HOME) {
        debug_log_scroll_x = 0;
        return;
    }
    if (event.code == KEY_END) {
        debug_log_scroll_x = DEBUG_LOG_SCROLL_MAX;
        return;
    }

    if (event.code == KEY_UP && debug_history_count > 0) {
        if (debug_history_cursor > 0) {
            --debug_history_cursor;
        }
        copy_string(debug_term.input, debug_history[debug_history_cursor], sizeof(debug_term.input));
        debug_term.input_len = (int)strlen_local(debug_term.input);
    } else if (event.code == KEY_DOWN && debug_history_count > 0) {
        if (debug_history_cursor + 1 < debug_history_count) {
            ++debug_history_cursor;
            copy_string(debug_term.input, debug_history[debug_history_cursor], sizeof(debug_term.input));
            debug_term.input_len = (int)strlen_local(debug_term.input);
        } else {
            debug_history_cursor = debug_history_count;
            debug_term.input[0] = '\0';
            debug_term.input_len = 0;
        }
    } else if (event.code == KEY_BACKSPACE) {
        if (debug_term.input_len > 0) {
            --debug_term.input_len;
            debug_term.input[debug_term.input_len] = '\0';
        }
    } else if (event.code == KEY_ENTER) {
        debug_execute_command();
    } else if (event.code == KEY_ESC) {
        debug_overlay_open = false;
    } else if (event.ch >= 32 && event.ch <= 126 && debug_term.input_len + 1 < TERM_LINE_LEN) {
        debug_term.input[debug_term.input_len++] = event.ch;
        debug_term.input[debug_term.input_len] = '\0';
    }
}

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

static void debug_execute_pending(void) {
    DebugAction action = debug_pending_action;
    debug_pending_action = DEBUG_ACTION_NONE;
    if (action == DEBUG_ACTION_CRASH) {
        debug_forced_fault_reason = "debug crash command requested";
        serial_trace("ERROR", "debug crash command requested");
        __asm__ volatile(".byte 0x0F, 0x0B");
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
        terminal_add_line(&debug_term, "help edit | help exception | help fault");
        terminal_add_line(&debug_term, "edit view change crash halt fault continue");
    } else if (token_is(token, "e", "edit", NULL, NULL)) {
        terminal_add_line(&debug_term, "edit mem 0xADDR ff 0x100");
        terminal_add_line(&debug_term, "view mem [0xADDR] | view vid | view disk");
        terminal_add_line(&debug_term, "change vid 800x600 bpp 16 | change bg 2");
    } else if (token_is(token, "ex", "except", "exception", "exceptions")) {
        terminal_add_line(&debug_term, "crash | halt | fault 1 | fault 2 | fault 3");
        terminal_add_line(&debug_term, "continue runs armed crash/halt/fault actions.");
    } else if (token_is(token, "crash", NULL, NULL, NULL)) {
        terminal_add_line(&debug_term, "crash: arm a real kernel crash, then continue.");
    } else if (token_is(token, "halt", NULL, NULL, NULL)) {
        terminal_add_line(&debug_term, "halt: arm silent CPU halt, then continue.");
    } else if (token_is(token, "f", "fault", NULL, NULL)) {
        terminal_add_line(&debug_term, "fault 1=divide, 2=invalid opcode, 3=triple fault");
    } else if (token_is(token, "view", "v", NULL, NULL)) {
        terminal_add_line(&debug_term, "view mem [0xADDR] opens hex/visual memory viewer.");
        terminal_add_line(&debug_term, "F1 hex, F2 visual, arrows move, ESC exits viewer.");
    } else if (token_is(token, "change", "ch", NULL, NULL)) {
        terminal_add_line(&debug_term, "change vid 800x600 | change vid bpp 4|8|16");
        terminal_add_line(&debug_term, "change bg 1 | change bg 2");
    } else if (token_is(token, "continue", "con", "c", NULL)) {
        terminal_add_line(&debug_term, "continue: close debugger and resume desktop.");
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
    terminal_add_line(&debug_term, command);
    debug_add_history(command);
    serial_trace_concat("INFO", "DBG command: ", command);

    if (streq(command, "help") || starts_with(command, "help ")) {
        debug_help_command(command);
    } else if (streq(command, "c") || streq(command, "con") || streq(command, "continue")) {
        debug_overlay_open = false;
        debug_memory_view_open = false;
        serial_trace("INFO", "Debugger continued");
        debug_execute_pending();
    } else if (streq(command, "crash")) {
        debug_pending_action = DEBUG_ACTION_CRASH;
        terminal_add_line(&debug_term, "Crash armed. Type continue.");
    } else if (streq(command, "halt")) {
        debug_pending_action = DEBUG_ACTION_HALT;
        terminal_add_line(&debug_term, "Halt armed. Type continue.");
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
        terminal_add_line(&debug_term, "Fault armed. Type continue.");
    } else if (starts_with(command, "change ") || starts_with(command, "ch ")) {
        debug_change_command(command);
    } else if (streq(command, "view") || streq(command, "v") || starts_with(command, "view ") || starts_with(command, "v ")) {
        debug_view_command(command);
    } else if (starts_with(command, "edit ")) {
        debug_edit_command(command);
    } else {
        terminal_add_line(&debug_term, "Unknown debugger command.");
    }
}

static void debug_handle_key(KeyEvent event) {
    if (debug_memory_view_open) {
        debug_memory_handle_key(event);
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

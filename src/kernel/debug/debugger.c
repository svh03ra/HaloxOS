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
    debug_memory_scroll_drag = false;
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

/* Geometry shared by the memory renderer and the scrollbar hit test, so
 * the drawn track and the clickable track can never disagree. */
static int debug_memory_view_x(void) {
    return 16;
}

static int debug_memory_view_y(void) {
    return 26;
}

static int debug_memory_view_w(void) {
    return OS_WIDTH - 32;
}

static int debug_memory_view_h(void) {
    return OS_HEIGHT - 54;
}

/*
 * Live bitmap geometry, one memory byte per pixel.  These used to be the
 * fixed 430x240 macros; the Width/Height boxes in the viewer now change
 * them at run time so the bitmap can use the rest of the screen.
 */
static int debug_memory_visual_w(void) {
    return (int)debug_memory_visual_width;
}

static int debug_memory_visual_h(void) {
    return (int)debug_memory_visual_height;
}

#define DEBUG_MEMORY_VISUAL_MIN 16
#define DEBUG_MEMORY_VISUAL_TOP 54
#define DEBUG_MEMORY_SIZE_ROW_H 48
#define DEBUG_MEMORY_SIZE_BOX_W 52
#define DEBUG_MEMORY_SIZE_BOX_H 14

/* Largest bitmap that still leaves room for the scrollbar on the right and
 * the Width/Height row plus the footer below it.  This is the extra space a
 * bigger screen unlocks for the bitmap. */
static int debug_memory_visual_max_w(void) {
    int max = debug_memory_view_w() - 64;

    return max < DEBUG_MEMORY_VISUAL_MIN ? DEBUG_MEMORY_VISUAL_MIN : max;
}

static int debug_memory_visual_max_h(void) {
    int max = debug_memory_view_h() - DEBUG_MEMORY_VISUAL_TOP - DEBUG_MEMORY_SIZE_ROW_H - 10;

    return max < DEBUG_MEMORY_VISUAL_MIN ? DEBUG_MEMORY_VISUAL_MIN : max;
}

static int debug_memory_visual_x(void) {
    return debug_memory_view_x() + (debug_memory_view_w() - debug_memory_visual_w()) / 2;
}

static int debug_memory_visual_y(void) {
    return debug_memory_view_y() + DEBUG_MEMORY_VISUAL_TOP;
}

static int debug_memory_scroll_x(void) {
    return debug_memory_visual_x() + debug_memory_visual_w() + 10;
}

/* The Width/Height row sits just above the Memory Bytes footer, clear of
 * the header and of the tallest bitmap. */
static int debug_memory_size_row_y(void) {
    return debug_memory_view_y() + debug_memory_view_h() - DEBUG_MEMORY_SIZE_ROW_H;
}

static int debug_memory_size_box_x(int which) {
    return debug_memory_view_x() + (which == 0 ? 58 : 180);
}

static int debug_memory_size_field_at(int x, int y) {
    for (int which = 0; which < 2; ++which) {
        if (point_in_rect(x, y, debug_memory_size_box_x(which) - 2,
                          debug_memory_size_row_y() - 2,
                          DEBUG_MEMORY_SIZE_BOX_W + 4,
                          DEBUG_MEMORY_SIZE_BOX_H + 4)) {
            return which;
        }
    }
    return -1;
}

/* The pointer is drawn only over the bitmap memory viewer's box: that is
 * the one debugger screen with pointer-reactive controls (the scrollbar and
 * the Width/Height boxes).  The hex screen is keyboard-only, so F1 hides
 * the pointer and F2 brings it back; the terminal overlay hides it as well.
 * Outside the box the overlay is the only thing on screen, and a stamp in
 * the wallpaper strip around it just left a frozen arrow that nothing
 * repainted. */
static bool debug_memory_pointer_visible(void) {
    if (!debug_overlay_open || !debug_memory_view_open ||
        debug_memory_mode != DEBUG_MEMORY_MODE_VISUAL) {
        return false;
    }
    return point_in_rect(mouse.x, mouse.y, debug_memory_view_x(),
                         debug_memory_view_y(), debug_memory_view_w(),
                         debug_memory_view_h());
}

/* Bytes visible in one screen of the current mode. */
static uint32_t debug_memory_visible_bytes(void) {
    if (debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL) {
        return (uint32_t)debug_memory_visual_w() * (uint32_t)debug_memory_visual_h();
    }
    return (uint32_t)debug_memory_hex_rows() * DEBUG_MEMORY_BYTES_PER_ROW;
}

/* Byte distance of one scroll step (one hex row or one bitmap row). */
static uint32_t debug_memory_row_bytes(void) {
    return debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL
        ? (uint32_t)debug_memory_visual_w()
        : (uint32_t)DEBUG_MEMORY_BYTES_PER_ROW;
}

/* Furthest row-aligned base that still reaches the final byte. Rounding
 * up (instead of down) keeps the last partial row reachable; the unused
 * tail of that row renders as black, the same way the Task Manager's
 * palette grid shows its honest tail row. */
static uint32_t debug_memory_max_base(void) {
    uint32_t limit = debug_memory_limit();
    uint32_t visible = debug_memory_visible_bytes();
    uint32_t row = debug_memory_row_bytes();
    uint32_t need;
    uint32_t rem;

    if (limit <= visible || row == 0) {
        return 0;
    }
    need = limit - visible;
    rem = need % row;
    if (rem != 0) {
        uint32_t pad = row - rem;

        if (need > 0xFFFFFFFFu - pad) {
            return need;
        }
        need += pad;
    }
    return need;
}

static void debug_memory_clamp_cursor_to_view(void) {
    uint32_t visible = debug_memory_visible_bytes();
    uint32_t limit = debug_memory_limit();
    uint32_t end = debug_range_end(debug_memory_base, visible);

    if (debug_memory_cursor < debug_memory_base) {
        debug_memory_cursor = debug_memory_base;
    } else if (debug_memory_cursor >= end) {
        debug_memory_cursor = end > debug_memory_base ? end - 1u : debug_memory_base;
    }
    if (limit != 0 && debug_memory_cursor >= limit) {
        debug_memory_cursor = limit - 1u;
    }
}

static void debug_align_memory_view(void) {
    uint32_t limit = debug_memory_limit();
    uint32_t visible_bytes = debug_memory_visible_bytes();
    uint32_t row = debug_memory_row_bytes();
    uint32_t max_base = debug_memory_max_base();

    if (limit == 0 || row == 0) {
        debug_memory_base = 0;
        debug_memory_cursor = 0;
        return;
    }
    if (debug_memory_cursor >= limit) {
        debug_memory_cursor = limit - 1;
    }

    if (debug_memory_cursor < debug_memory_base) {
        debug_memory_base = debug_memory_cursor - (debug_memory_cursor % row);
    } else if (debug_memory_cursor >= debug_range_end(debug_memory_base, visible_bytes)) {
        uint32_t want = debug_memory_cursor >= visible_bytes
            ? debug_memory_cursor - visible_bytes + 1u
            : 0u;

        debug_memory_base = want - (want % row);
    }
    if (debug_memory_base > max_base) {
        debug_memory_base = max_base;
    }
    debug_memory_clamp_cursor_to_view();
}

/*
 * Bitmap Width/Height boxes.
 *
 * The buffer always holds what the box shows; it is seeded from the
 * applied size when a box takes focus and written back after a clamp, so
 * typing can never leave the box disagreeing with the bitmap on screen.
 */
static void debug_memory_size_set_text(int which, uint32_t value) {
    size_t len = 0;

    debug_memory_size_text[which][0] = '\0';
    append_uint(debug_memory_size_text[which], &len,
                sizeof(debug_memory_size_text[which]), value);
    debug_memory_size_len[which] = (int)len;
}

static uint32_t debug_memory_size_parse(int which) {
    uint32_t value = 0;

    for (int i = 0; i < debug_memory_size_len[which]; ++i) {
        value = value * 10u + (uint32_t)(debug_memory_size_text[which][i] - '0');
        /* Nothing above the largest screen matters; stop before overflow. */
        if (value > 4096u) {
            return 4096u;
        }
    }
    return value;
}

/* Clamp both boxes to what this screen can show, then resize the bitmap.
 * Applying is explicit (Enter, Tab or a click elsewhere) so a half-typed
 * number never redraws the view at a nonsense size. */
static void debug_memory_apply_visual_size(void) {
    uint32_t width = debug_memory_size_parse(0);
    uint32_t height = debug_memory_size_parse(1);
    uint32_t max_w = (uint32_t)debug_memory_visual_max_w();
    uint32_t max_h = (uint32_t)debug_memory_visual_max_h();

    if (width < DEBUG_MEMORY_VISUAL_MIN) {
        width = DEBUG_MEMORY_VISUAL_MIN;
    }
    if (height < DEBUG_MEMORY_VISUAL_MIN) {
        height = DEBUG_MEMORY_VISUAL_MIN;
    }
    if (width > max_w) {
        width = max_w;
    }
    if (height > max_h) {
        height = max_h;
    }

    debug_memory_visual_width = width;
    debug_memory_visual_height = height;
    debug_memory_size_set_text(0, width);
    debug_memory_size_set_text(1, height);
    debug_memory_size_fresh = false;
    if (debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL) {
        debug_memory_base -= debug_memory_base % width;
    }
    debug_align_memory_view();
}

static void debug_memory_size_focus_field(int which) {
    if (which == debug_memory_size_focus) {
        return;
    }
    debug_memory_size_focus = which;
    debug_memory_size_fresh = which >= 0;
    if (which >= 0) {
        debug_memory_size_set_text(which, which == 0
            ? debug_memory_visual_width
            : debug_memory_visual_height);
    }
}

/* Seed the boxes from the live size, shrinking a size that no longer fits
 * the current screen (a resolution change while the viewer was closed). */
static void debug_memory_size_reset_text(void) {
    uint32_t max_w = (uint32_t)debug_memory_visual_max_w();
    uint32_t max_h = (uint32_t)debug_memory_visual_max_h();

    if (debug_memory_visual_width < DEBUG_MEMORY_VISUAL_MIN) {
        debug_memory_visual_width = DEBUG_MEMORY_VISUAL_MIN;
    }
    if (debug_memory_visual_height < DEBUG_MEMORY_VISUAL_MIN) {
        debug_memory_visual_height = DEBUG_MEMORY_VISUAL_MIN;
    }
    if (debug_memory_visual_width > max_w) {
        debug_memory_visual_width = max_w;
    }
    if (debug_memory_visual_height > max_h) {
        debug_memory_visual_height = max_h;
    }
    debug_memory_size_set_text(0, debug_memory_visual_width);
    debug_memory_size_set_text(1, debug_memory_visual_height);
    debug_memory_size_focus = -1;
    debug_memory_size_fresh = false;
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

/* Scroll the viewport by whole rows without moving the selection away
 * from the visible window. Used by the scrollbar and Ctrl+Up/Down. */
static void debug_memory_scroll_rows(int rows) {
    uint32_t row = debug_memory_row_bytes();
    uint32_t max_base = debug_memory_max_base();
    uint32_t base = debug_memory_base;

    if (row == 0 || rows == 0) {
        return;
    }
    if (base > max_base) {
        base = max_base;
    }
    if (rows < 0) {
        uint32_t step = (uint32_t)(-rows) * row;

        base = step > base ? 0u : base - step;
    } else {
        uint32_t step = (uint32_t)rows * row;

        if (step >= max_base - base) {
            base = max_base;
        } else {
            base += step;
        }
    }
    debug_memory_base = base - (base % row);
    debug_memory_edit_nibble = -1;
    debug_memory_clamp_cursor_to_view();
}

/* Scrollbar thumb math without a 64-bit divide (the freestanding kernel
 * has no libgcc __udivdi3). unit = floor(max_base / track), so
 * offset * unit can never exceed max_base and the whole mapping stays
 * in 32-bit arithmetic. The error is at most one thumb pixel. */
static int debug_memory_thumb_offset(uint32_t base, uint32_t max_base, int track) {
    uint32_t unit;
    uint32_t offset;

    if (max_base == 0 || track <= 0) {
        return 0;
    }
    unit = max_base / (uint32_t)track;
    if (unit == 0) {
        /* max_base < track, so base * track cannot overflow 32 bits. */
        return (int)((base * (uint32_t)track) / max_base);
    }
    offset = base / unit;
    if (offset > (uint32_t)track) {
        offset = (uint32_t)track;
    }
    return (int)offset;
}

static uint32_t debug_memory_thumb_to_base(int offset, uint32_t max_base, int track) {
    uint32_t unit;

    if (max_base == 0 || track <= 0) {
        return 0;
    }
    if (offset >= track) {
        return max_base;
    }
    unit = max_base / (uint32_t)track;
    if (unit == 0) {
        /* max_base < track, so offset * max_base cannot overflow. */
        return ((uint32_t)offset * max_base) / (uint32_t)track;
    }
    return (uint32_t)offset * unit;
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
    debug_memory_scroll_drag = false;
    debug_memory_size_reset_text();
    debug_align_memory_view();
    serial_trace("INFO", "Debugger memory view opened");
}

static void debug_memory_handle_key(KeyEvent event) {
    if (event.code == KEY_ESC) {
        debug_memory_view_open = false;
        debug_memory_edit_nibble = -1;
        debug_memory_scroll_drag = false;
        terminal_add_line(&debug_term, "Memory viewer closed.");
        return;
    }
    if (event.code == KEY_F1) {
        debug_memory_mode = DEBUG_MEMORY_MODE_HEX;
        debug_memory_base -= debug_memory_base % DEBUG_MEMORY_BYTES_PER_ROW;
        debug_memory_edit_nibble = -1;
        debug_memory_scroll_drag = false;
        debug_align_memory_view();
        return;
    }
    if (event.code == KEY_F2) {
        debug_memory_mode = DEBUG_MEMORY_MODE_VISUAL;
        debug_memory_base -= debug_memory_base % (uint32_t)debug_memory_visual_w();
        debug_memory_edit_nibble = -1;
        debug_memory_scroll_drag = false;
        debug_memory_size_reset_text();
        debug_align_memory_view();
        return;
    }

    /* Width/Height boxes: Tab switches, digits type, Enter applies.  The
     * bitmap keys below stay untouched while no box has focus. */
    if (debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL) {
        int which = debug_memory_size_focus;

        if (event.code == KEY_TAB) {
            if (which < 0) {
                debug_memory_size_focus_field(0);
            } else {
                debug_memory_apply_visual_size();
                debug_memory_size_focus_field(which == 0 ? 1 : 0);
            }
            return;
        }
        if (event.code == KEY_ENTER && which >= 0) {
            debug_memory_apply_visual_size();
            debug_memory_size_focus = -1;
            return;
        }
        if (event.ch >= '0' && event.ch <= '9' && which < 0) {
            /* A digit with no box focused starts a fresh Width entry. */
            debug_memory_size_focus_field(0);
            which = debug_memory_size_focus;
        }
        if (which >= 0) {
            if (event.code == KEY_BACKSPACE) {
                debug_memory_size_fresh = false;
                if (debug_memory_size_len[which] > 0) {
                    --debug_memory_size_len[which];
                    debug_memory_size_text[which][debug_memory_size_len[which]] = '\0';
                }
                return;
            }
            if (event.ch >= '0' && event.ch <= '9') {
                if (debug_memory_size_fresh) {
                    debug_memory_size_len[which] = 0;
                    debug_memory_size_text[which][0] = '\0';
                    debug_memory_size_fresh = false;
                }
                if (debug_memory_size_len[which] < 4) {
                    debug_memory_size_text[which][debug_memory_size_len[which]++] = event.ch;
                    debug_memory_size_text[which][debug_memory_size_len[which]] = '\0';
                }
                return;
            }
        }
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
        if (keyboard_ctrl) {
            debug_memory_scroll_rows(debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL
                ? -debug_memory_visual_h()
                : -debug_memory_hex_rows());
        } else {
            debug_move_memory_cursor(debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL
                ? -debug_memory_visual_w()
                : -DEBUG_MEMORY_BYTES_PER_ROW);
        }
        return;
    }
    if (event.code == KEY_DOWN) {
        if (keyboard_ctrl) {
            debug_memory_scroll_rows(debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL
                ? debug_memory_visual_h()
                : debug_memory_hex_rows());
        } else {
            debug_move_memory_cursor(debug_memory_mode == DEBUG_MEMORY_MODE_VISUAL
                ? debug_memory_visual_w()
                : DEBUG_MEMORY_BYTES_PER_ROW);
        }
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

/* Memory-view scrollbar and size-box mouse handling. Called from
 * handle_desktop_mouse while the debug overlay owns the screen.  The
 * pointer is visible in both memory modes; only the bitmap screen has
 * interactive controls (the Task-Manager-style arrows, track and thumb,
 * plus the Width/Height boxes), so the hex screen returns without
 * consuming any click. */
static void debug_memory_handle_mouse(void) {
    int scroll_x;
    int bitmap_y;
    int track_y;
    int track_h;
    int thumb_h = 20;
    int thumb_track;
    int thumb_min;
    int thumb_y;
    int field;
    uint32_t max_base;
    bool clicked;

    if (!debug_memory_view_open || debug_memory_mode != DEBUG_MEMORY_MODE_VISUAL) {
        debug_memory_scroll_drag = false;
        return;
    }

    clicked = mouse.left && !mouse.prev_left;

    /* Clicking a box focuses it and commits whatever the previous box was
     * being edited to; clicking anywhere else drops focus the same way. */
    field = debug_memory_size_field_at(mouse.x, mouse.y);
    if (field >= 0) {
        cursor_hand_hint = true;
    }
    if (clicked) {
        if (field >= 0) {
            if (field != debug_memory_size_focus) {
                debug_memory_apply_visual_size();
                debug_memory_size_focus_field(field);
            }
            debug_memory_scroll_drag = false;
            return;
        }
        if (debug_memory_size_focus >= 0) {
            debug_memory_apply_visual_size();
            debug_memory_size_focus = -1;
        }
    }

    scroll_x = debug_memory_scroll_x();
    bitmap_y = debug_memory_visual_y();
    track_y = bitmap_y + 16;
    track_h = debug_memory_visual_h() - 32;
    thumb_track = track_h - thumb_h;
    thumb_min = track_y + 1;
    max_base = debug_memory_max_base();
    if (debug_memory_base > max_base) {
        debug_memory_base = max_base;
    }
    thumb_y = thumb_min + debug_memory_thumb_offset(debug_memory_base, max_base, thumb_track);

    if (clicked &&
        point_in_rect(mouse.x, mouse.y, scroll_x, bitmap_y - 2, 16, 16)) {
        debug_memory_scroll_rows(-1);
    } else if (clicked &&
               point_in_rect(mouse.x, mouse.y, scroll_x,
                             bitmap_y + debug_memory_visual_h() - 16, 16, 16)) {
        debug_memory_scroll_rows(1);
    } else if (clicked &&
               point_in_rect(mouse.x, mouse.y, scroll_x + 2, thumb_y, 12, thumb_h)) {
        debug_memory_scroll_drag = true;
        debug_memory_scroll_drag_offset = mouse.y - thumb_y;
    } else if (clicked &&
               point_in_rect(mouse.x, mouse.y, scroll_x, track_y, 16, track_h)) {
        if (mouse.y < thumb_y) {
            debug_memory_scroll_rows(-debug_memory_visual_h());
        } else if (mouse.y > thumb_y + thumb_h) {
            debug_memory_scroll_rows(debug_memory_visual_h());
        }
    }

    if (!mouse.left) {
        debug_memory_scroll_drag = false;
    } else if (debug_memory_scroll_drag && max_base != 0 && thumb_track > 0) {
        int desired_y = clampi(mouse.y - debug_memory_scroll_drag_offset,
                               thumb_min, thumb_min + thumb_track);
        uint32_t raw = debug_memory_thumb_to_base(desired_y - thumb_min, max_base, thumb_track);

        raw -= raw % (uint32_t)debug_memory_visual_w();
        if (raw > max_base) {
            raw = max_base;
        }
        debug_memory_base = raw;
        debug_memory_edit_nibble = -1;
        debug_memory_clamp_cursor_to_view();
    }
}

/* Breakpoint target lookup: name token -> AppId bit. */
static AppId app_index_from_bit(uint32_t bit) {
    for (int app = 0; app < APP_COUNT; ++app) {
        if (bit == (uint32_t)(1u << app)) {
            return (AppId)app;
        }
    }
    return APP_NOTEPAD;
}

static uint32_t debug_bp_target_bit(const char *token) {
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
        { "doom|dm|d",                   APP_DOOM },
    };

    if (token_is(token, "all", "everything", "every", "*")) {
        return 0x80000000u;
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
                    return (uint32_t)(1u << targets[i].app);
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
 * Breakpoint trace: symbol of the caller, rendered in the same
 * "[0xADDR] name at src/...; line N" style as the graphical crash
 * handler backtrace.  The decoded-instruction tail is deliberately not
 * appended: it pushed these lines past the debugger panel's right edge,
 * where the scroll indicator clipped them mid-token ("PUS").
 */
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

    append_string(line, len, max_len, "[0x");
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
    append_string(line, len, max_len, "] ");
    append_string(line, len, max_len, name);
    append_string(line, len, max_len, " at ");
    append_string(line, len, max_len, base);
    append_string(line, len, max_len, "; line ");
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

    line[*len] = '\0';
}

/*
 * A real desktop input opens the next breakpoint frame. The latch is
 * only cleared here (and by 'continue'/'breakpoint'), never by the
 * debugger's own terminal-blink repaints, so an idle app cannot keep
 * catching the same frame forever.
 */
static void debug_bp_note_input(void) {
    if (debug_bp_armed) {
        debug_bp_caught = false;
    }
}

/*
 * Breakpoint hit: called from render_app_window when a targeted app is
 * being drawn. AUTO-CONTINUE: the catch appends its trace to the
 * terminal log but does NOT hold the system. debug_bp_caught then
 * blocks every other render until a real input clears it, so each
 * user-visible frame logs exactly one catch; the next input frame logs
 * the next number.
 */
static void debug_bp_catch(AppId app) {
    uint32_t ebp;
    uint32_t frame_no = ++debug_bp_catch_count;

    /* AUTO-CONTINUE: log the catch, keep the run alive. The overlay
     * pops open (notification) but the system never waits for input
     * here - the next input frame fires the next catch. */
    debug_bp_caught = true;
    if (!debug_overlay_open) {
        debug_overlay_open = true;
        debug_memory_view_open = false;
    }

    {
        char head[TERM_LINE_LEN];
        size_t len = 0;

        append_string(head, &len, sizeof(head), "** BP frame #");
        append_uint(head, &len, sizeof(head), frame_no);
        append_string(head, &len, sizeof(head), " hit:");
        terminal_add_line(&debug_term, head);
    }
    {
        char line[TERM_LINE_LEN];
        size_t len = 0;

        line[0] = '\0';
        debug_bp_append_trace_line(line, &len, sizeof(line), (uint32_t)(uintptr_t)&debug_bp_catch);
        terminal_add_line(&debug_term, line);
    }
    {
        char label[TERM_LINE_LEN];
        size_t len = 0;

        append_string(label, &len, sizeof(label), "app: ");
        append_string(label, &len, sizeof(label), app_titles[app]);
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
        if (ret < 0x200000u || ret > 0x5D0000u) {
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
    uint32_t frame_no;

    if (!debug_bp_armed || debug_bp_caught || debug_bp_mask == 0 ||
        ((debug_bp_mask & 0x80000000u) == 0 && (debug_bp_mask & (uint32_t)(1u << APP_POWER)) == 0)) {
        return;
    }

    frame_no = ++debug_bp_catch_count;
    debug_bp_caught = true;
    if (!debug_overlay_open) {
        debug_overlay_open = true;
        debug_memory_view_open = false;
    }

    {
        char head[TERM_LINE_LEN];
        size_t len = 0;

        append_string(head, &len, sizeof(head), "** BP frame #");
        append_uint(head, &len, sizeof(head), frame_no);
        append_string(head, &len, sizeof(head), " hit:");
        terminal_add_line(&debug_term, head);
    }
    {
        char label[TERM_LINE_LEN];
        size_t len = 0;

        append_string(label, &len, sizeof(label), "power trigger: ");
        append_string(label, &len, sizeof(label), action);
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
        if (ret < 0x200000u || ret > 0x5D0000u) {
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

    /* Ask for the desktop size FIRST, exactly as the Settings app does, so
     * the mode switch's own map update already knows it. On a backend that
     * cannot program a mode the switch below only adopts the geometry it is
     * already presenting, and the desktop is the thing that actually moves:
     * without this request `change vid 800x600` reported "Screen changed" and
     * left the desktop at its old size. */
    if (has_resolution) {
        set_desktop_size((int)target_width, (int)target_height);
    }

    if (!set_framebuffer_mode_raw(target_width, target_height, target_bpp)) {
        terminal_add_line(&debug_term, "ERROR: video mode switch unavailable.");
        serial_trace("ERROR", "debugger video mode switch failed");
        serial_trace_video_mode("Screen change failed");
        if (has_resolution) {
            /* Put the desktop back to the geometry still on screen: the
             * request above moved it before the switch refused it. */
            set_desktop_size((int)fb.width, (int)fb.height);
            update_present_maps();
        }
        return;
    }

    update_present_maps();
    ui_input_dirty = true;

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
    len = 0;
    append_string(line, &len, sizeof(line), "Backend: ");
    append_string(line, &len, sizeof(line), video_backend_name());
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
    len = 0;
    append_string(line, &len, sizeof(line), "Drive: ");
    append_hex32(line, &len, sizeof(line), boot_drive_number);
    append_char(line, &len, sizeof(line), ' ');
    for (const char *name = disk_physical_type_label(); *name != '\0'; ++name) {
        append_char(line, &len, sizeof(line), *name);
    }
    terminal_add_line(&debug_term, line);

    if (boot_drive_info_available && boot_drive_storage_bytes != 0) {
        char storage[32] = {0};
        format_single_memory_amount(storage, sizeof(storage), boot_drive_storage_bytes);
        len = 0;
        append_string(line, &len, sizeof(line), "Storage: ");
        append_string(line, &len, sizeof(line), storage);
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
        terminal_add_line(&debug_term, "help breakpoint | help fps | help clear");
        terminal_add_line(&debug_term, "");
        terminal_add_line(&debug_term, "== ALL COMMANDS: ==");
        terminal_add_line(&debug_term, "edit view change continue test");
        terminal_add_line(&debug_term, "show hide grid breakpoint cls clear");
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
        terminal_add_line(&debug_term, "Visual mode: click/drag the right scrollbar, or");
        terminal_add_line(&debug_term, "Ctrl+Up/Down pages the bitmap.");
        terminal_add_line(&debug_term, "In the log: Ctrl+Left/Right scrolls long lines.");
        terminal_add_line(&debug_term, "<< and >> mark hidden text sides. Home/End jump.");
    } else if (token_is(token, "clear", "cls", "clr", NULL)) {
        terminal_add_line(&debug_term, "cls | clr | clear: wipe all previous debugger text.");
    } else if (token_is(token, "change", "ch", NULL, NULL)) {
        terminal_add_line(&debug_term, "change vid 800x600 | change vid bpp 4|8|16");
        terminal_add_line(&debug_term, "change bg 1 | change bg 2");
    } else if (token_is(token, "bp", "br", "breakpoint", NULL)) {
        terminal_add_line(&debug_term, "breakpoint all: catch every app on next frame.");
        terminal_add_line(&debug_term, "breakpoint <app>: catch one app when it updates.");
        terminal_add_line(&debug_term, "One catch per frame; a new click/key opens the next.");
        terminal_add_line(&debug_term, "Auto-continues: each new catch re-opens this log.");
        terminal_add_line(&debug_term, "Frame #1 is the first catch after breakpoint.");
        terminal_add_line(&debug_term, "breakpoint stop | s | off | o: stop all.");
        terminal_add_line(&debug_term, "power: shutdown/restart/halt triggers log");
        terminal_add_line(&debug_term, "a trace before the action runs.");
        terminal_add_line(&debug_term, "apps: notepad prompt paint explorer snake guess");
        terminal_add_line(&debug_term, "mines games power settings taskmgr doom all");
    } else if (token_is(token, "fps", "show", "hide", NULL)) {
        terminal_add_line(&debug_term, "show fps | s fps: toggle FPS overlay top-right.");
        terminal_add_line(&debug_term, "hide fps | h fps: hide it. Red = frame lag blink.");
        terminal_add_line(&debug_term, "show grid | s grid (ag/dg/app grid/desktop grid):");
        terminal_add_line(&debug_term, "16x16 icon snap grid overlay while dragging icons.");
        terminal_add_line(&debug_term, "hide grid | h grid: hide it. Snap follows Auto-Grid.");
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
    } else if (streq(command, "cls") || streq(command, "clr") || streq(command, "clear")) {
        terminal_reset(&debug_term);
        debug_log_scroll_x = 0;
        terminal_add_line(&debug_term, "Debugger terminal cleared.");
    } else if (streq(command, "c") || streq(command, "con") || streq(command, "continue")) {
        debug_overlay_open = false;
        debug_memory_view_open = false;
        debug_memory_scroll_drag = false;
        /* Arm the breakpoint now. The catch counter is deliberately NOT
         * reset here, and neither is the one-catch latch: 'continue'
         * only dismisses this log. The first continue after 'breakpoint'
         * still catches the next rendered frame (the latch was cleared
         * when the breakpoint was set); after that, continuing leaves
         * the latch set so the app is interactive again and the NEXT
         * real click/key logs the next frame (#2, #3, ...) instead of
         * re-catching the same idle frame. */
        if (debug_bp_mask != 0) {
            debug_bp_armed = true;
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

                append_string(msg, &len, sizeof(msg), "Crash vector ");
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
                append_string(msg, &len, sizeof(msg), " set. Type continue.");
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
        /* "show " also starts with 's'; testing command[0] sent the
         * parser to the two-letter alias and read "ow fps" as the
         * argument, so the documented "show fps" printed its usage line. */
        const char *arg = starts_with(command, "show ") ? skip_spaces(command + 5) : skip_spaces(command + 2);
        if (token_is(arg, "fps", "frames", "frame", NULL)) {
            fps_overlay_on = true;
            terminal_add_line(&debug_term, "FPS overlay shown (top-right).");
        } else if (token_is(arg, "grid", "ag", "dg", "appgrid") ||
                   token_is(arg, "desktopgrid", "deskgrid", "app grid", "desktop grid") ||
                   token_is(arg, "desk grid", "app g", "desk g", NULL)) {
            desktop_grid_overlay_on = true;
            terminal_add_line(&debug_term, "16x16 pixel snap grid overlay shown.");
            terminal_add_line(&debug_term, "Auto-Grid snaps icon drags with it off too.");
        } else {
            terminal_add_line(&debug_term, "Usage: show fps | show grid");
        }
    } else if (starts_with(command, "hide ") || starts_with(command, "h ")) {
        const char *arg = starts_with(command, "hide ") ? skip_spaces(command + 5) : skip_spaces(command + 2);
        if (token_is(arg, "fps", "frames", "frame", NULL)) {
            fps_overlay_on = false;
            terminal_add_line(&debug_term, "FPS overlay hidden.");
        } else if (token_is(arg, "grid", "ag", "dg", "appgrid") ||
                   token_is(arg, "desktopgrid", "deskgrid", "app grid", "desktop grid") ||
                   token_is(arg, "desk grid", "app g", "desk g", NULL)) {
            desktop_grid_overlay_on = false;
            terminal_add_line(&debug_term, "Grid overlay hidden.");
        } else {
            terminal_add_line(&debug_term, "Usage: hide fps | hide grid");
        }
    } else if (streq(command, "grid") || streq(command, "ag") || streq(command, "dg")) {
        desktop_grid_overlay_on = !desktop_grid_overlay_on;
        terminal_add_line(&debug_term, desktop_grid_overlay_on
            ? "16x16 icon snap grid overlay shown."
            : "Grid overlay hidden.");
    } else if (streq(command, "bp") || streq(command, "br") || streq(command, "breakpoint") ||
               starts_with(command, "bp ") || starts_with(command, "br ") || starts_with(command, "breakpoint ")) {
        char token[24];
        const char *cursor = command;

        cursor = read_token(cursor, token, sizeof(token));
        cursor = read_token(cursor, token, sizeof(token));
        if (token[0] == '\0') {
            terminal_add_line(&debug_term, "Usage: breakpoint all|<app> | breakpoint stop");
            terminal_add_line(&debug_term, "apps: notepad prompt paint explorer snake");
            terminal_add_line(&debug_term, "guess mines games power settings taskmgr doom");
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
            uint32_t bit = debug_bp_target_bit(token);

            if (bit == 0) {
                terminal_add_line(&debug_term, "Unknown breakpoint target.");
                terminal_add_line(&debug_term, "apps: notepad prompt paint explorer snake");
                terminal_add_line(&debug_term, "guess mines games power settings taskmgr doom all");
                return;
            }
            debug_bp_mask |= bit;
            debug_bp_caught = false;
            debug_bp_catch_count = 0;
            debug_bp_armed = false;   /* counts start after 'continue' */
            if (bit == 0x80000000u) {
                terminal_add_line(&debug_term, "Breakpoint set on ALL apps. Type continue.");
            } else {
                char msg[TERM_LINE_LEN];
                size_t len = 0;

                append_string(msg, &len, sizeof(msg), "Breakpoint set on ");
                append_string(msg, &len, sizeof(msg), app_titles[app_index_from_bit(bit)]);
                append_string(msg, &len, sizeof(msg), ". Type continue.");
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

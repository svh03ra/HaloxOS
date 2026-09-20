// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: runtime.c, runtime handling.

// This repository is licensed under the GNU General Public License.

/*
 * Bulk move/fill: 32 bytes of straight-line 32-bit accesses per iteration.
 *
 * The string ops (`rep movsl`/`rep stosl`) used to be the bulk path here on
 * the theory that one emulated instruction per 4 bytes beats a loop body.
 * Measured against the 614,400-byte scan-out, that is not what happens: the
 * rep forms cost 1.80M cycles and eight unrolled 32-bit moves of the same
 * bytes cost 1.07M, because one rep iteration costs about as much as a loop
 * body that has already moved four times as much. Frame time here is
 * dominated by exactly these copies (the wallpaper expansion and the
 * scan-out are ~87% of a full desktop repaint), so the wide straight-line
 * form is the one every bulk path uses now. DF is never set - nothing in
 * the kernel executes `std` - so the direction cleanup the rep prefix
 * implies is not needed either.
 */
#define BULK_MOVE_BYTES 32u

static void *memset_local(void *dest, int value, size_t len) {
    uint8_t *ptr = (uint8_t *)dest;
    uint32_t fill = (uint32_t)(uint8_t)value * 0x01010101u;

    while (len >= BULK_MOVE_BYTES) {
        *(uint32_t *)(void *)(ptr + 0) = fill;
        *(uint32_t *)(void *)(ptr + 4) = fill;
        *(uint32_t *)(void *)(ptr + 8) = fill;
        *(uint32_t *)(void *)(ptr + 12) = fill;
        *(uint32_t *)(void *)(ptr + 16) = fill;
        *(uint32_t *)(void *)(ptr + 20) = fill;
        *(uint32_t *)(void *)(ptr + 24) = fill;
        *(uint32_t *)(void *)(ptr + 28) = fill;
        ptr += BULK_MOVE_BYTES;
        len -= BULK_MOVE_BYTES;
    }
    while (len >= 4u) {
        *(uint32_t *)(void *)ptr = fill;
        ptr += 4;
        len -= 4u;
    }
    while (len-- > 0u) {
        *ptr++ = (uint8_t)value;
    }
    return dest;
}

static void *memcpy_local(void *dest, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    while (len >= BULK_MOVE_BYTES) {
        *(uint32_t *)(void *)(d + 0) = *(const uint32_t *)(const void *)(s + 0);
        *(uint32_t *)(void *)(d + 4) = *(const uint32_t *)(const void *)(s + 4);
        *(uint32_t *)(void *)(d + 8) = *(const uint32_t *)(const void *)(s + 8);
        *(uint32_t *)(void *)(d + 12) = *(const uint32_t *)(const void *)(s + 12);
        *(uint32_t *)(void *)(d + 16) = *(const uint32_t *)(const void *)(s + 16);
        *(uint32_t *)(void *)(d + 20) = *(const uint32_t *)(const void *)(s + 20);
        *(uint32_t *)(void *)(d + 24) = *(const uint32_t *)(const void *)(s + 24);
        *(uint32_t *)(void *)(d + 28) = *(const uint32_t *)(const void *)(s + 28);
        d += BULK_MOVE_BYTES;
        s += BULK_MOVE_BYTES;
        len -= BULK_MOVE_BYTES;
    }
    while (len >= 4u) {
        *(uint32_t *)(void *)d = *(const uint32_t *)(const void *)s;
        d += 4;
        s += 4;
        len -= 4u;
    }
    while (len-- > 0u) {
        *d++ = *s++;
    }
    return dest;
}

/* Bulk RGB565 fill for the rgb565 shadow buffer: two pixels per 32-bit
 * store, straight-line (see BULK_MOVE_BYTES), instead of a per-element
 * loop (fill_rect/clear_screen rows). */
static void memset16_local(void *dest, uint16_t value, size_t count) {
    uint32_t pattern = ((uint32_t)value << 16) | value;
    uint32_t *d = (uint32_t *)dest;
    size_t pairs = count >> 1;

    while (pairs >= 8u) {
        d[0] = pattern;
        d[1] = pattern;
        d[2] = pattern;
        d[3] = pattern;
        d[4] = pattern;
        d[5] = pattern;
        d[6] = pattern;
        d[7] = pattern;
        d += 8;
        pairs -= 8u;
    }
    while (pairs-- > 0u) {
        *d++ = pattern;
    }
    if ((count & 1u) != 0) {
        ((uint16_t *)dest)[count - 1u] = value;
    }
}

static size_t strlen_local(const char *text) {
    size_t len = 0;
    while (text[len] != '\0') {
        ++len;
    }
    return len;
}

static bool streq(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return false;
        }
        ++i;
    }
    return a[i] == b[i];
}

static bool starts_with(const char *text, const char *prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return false;
        }
        ++i;
    }
    return true;
}

static bool contains_text(const char *text, const char *needle) {
    if (*needle == '\0') {
        return true;
    }
    for (size_t i = 0; text[i]; ++i) {
        size_t j = 0;
        while (needle[j] && text[i + j] == needle[j]) {
            ++j;
        }
        if (needle[j] == '\0') {
            return true;
        }
    }
    return false;
}

static bool parse_uint_decimal(const char *text, uint32_t *value_out) {
    uint32_t value = 0;
    bool any = false;
    while (*text >= '0' && *text <= '9') {
        value = value * 10u + (uint32_t)(*text - '0');
        ++text;
        any = true;
    }
    if (!any) {
        return false;
    }
    *value_out = value;
    return true;
}

static bool parse_uint_auto(const char *text, uint32_t *value_out) {
    uint32_t value = 0;
    bool any = false;
    if (starts_with(text, "0x") || starts_with(text, "0X")) {
        text += 2;
    } else if (!((*text >= 'a' && *text <= 'f') || (*text >= 'A' && *text <= 'F'))) {
        return parse_uint_decimal(text, value_out);
    }

    while ((*text >= '0' && *text <= '9') || (*text >= 'a' && *text <= 'f') || (*text >= 'A' && *text <= 'F')) {
        uint32_t digit = 0;
        if (*text >= '0' && *text <= '9') {
            digit = (uint32_t)(*text - '0');
        } else if (*text >= 'a' && *text <= 'f') {
            digit = (uint32_t)(*text - 'a' + 10);
        } else {
            digit = (uint32_t)(*text - 'A' + 10);
        }
        value = (value << 4) | digit;
        ++text;
        any = true;
    }

    if (!any) {
        return false;
    }
    *value_out = value;
    return true;
}

static void append_hex8(char *dest, size_t *len, size_t max_len, uint8_t value) {
    static const char hex[] = "0123456789ABCDEF";
    append_char(dest, len, max_len, hex[(value >> 4) & 0x0F]);
    append_char(dest, len, max_len, hex[value & 0x0F]);
}

static void append_hex32(char *dest, size_t *len, size_t max_len, uint32_t value) {
    append_char(dest, len, max_len, '0');
    append_char(dest, len, max_len, 'x');
    for (int shift = 28; shift >= 0; shift -= 4) {
        static const char hex[] = "0123456789ABCDEF";
        append_char(dest, len, max_len, hex[(value >> shift) & 0x0Fu]);
    }
}

static int clampi(int value, int min, int max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void copy_string(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    if (max_len == 0) {
        return;
    }
    while (src[i] && i + 1 < max_len) {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = '\0';
}

/*
 * Append a string to a length-tracked buffer.
 *
 * Callers used to write `copy_string(buf + len, text, cap - len)` and then
 * recover the new length with `len = strlen_local(buf)` - a full rescan of
 * the line for every field appended to it, and a fresh chance to get the
 * capacity arithmetic wrong at each of the thirty-odd sites that did it.
 * Keeping the length authoritative here mirrors append_char below, so the
 * buffer is never re-measured and callers pass the total size rather than
 * a remaining-bytes expression.
 */
static void append_string(char *dest, size_t *len, size_t max_len, const char *src) {
    size_t i = *len;

    if (max_len == 0 || i >= max_len) {
        return;
    }
    while (*src != '\0' && i + 1 < max_len) {
        dest[i++] = *src++;
    }
    dest[i] = '\0';
    *len = i;
}

static void append_char(char *dest, size_t *len, size_t max_len, char ch) {
    if (*len + 1 >= max_len) {
        return;
    }
    dest[*len] = ch;
    ++(*len);
    dest[*len] = '\0';
}

static void append_uint(char *dest, size_t *len, size_t max_len, uint32_t value) {
    char buffer[16];
    int pos = 0;

    if (value == 0) {
        append_char(dest, len, max_len, '0');
        return;
    }

    while (value > 0 && pos < 15) {
        buffer[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (pos > 0) {
        append_char(dest, len, max_len, buffer[--pos]);
    }
}

static void append_padded_uint(char *dest, size_t *len, size_t max_len, uint32_t value, uint32_t width) {
    char buffer[16];
    int pos = 0;

    if (value == 0) {
        buffer[pos++] = '0';
    } else {
        while (value > 0 && pos < 15) {
            buffer[pos++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }

    while ((uint32_t)pos < width) {
        append_char(dest, len, max_len, '0');
        --width;
    }

    while (pos > 0) {
        append_char(dest, len, max_len, buffer[--pos]);
    }
}

static void append_decimal_2(char *dest, size_t *len, size_t max_len, uint32_t whole, uint32_t frac) {
    append_uint(dest, len, max_len, whole);
    append_char(dest, len, max_len, '.');
    append_padded_uint(dest, len, max_len, frac, 2);
}

static void append_memory_amount(char *dest, size_t *len, size_t max_len, uint32_t bytes) {
    if (bytes >= 1024u * 1024u * 1024u) {
        uint32_t whole = bytes / (1024u * 1024u * 1024u);
        uint32_t frac = (bytes % (1024u * 1024u * 1024u)) / ((1024u * 1024u * 1024u) / 100u);
        append_decimal_2(dest, len, max_len, whole, frac);
        append_char(dest, len, max_len, ' ');
        append_char(dest, len, max_len, 'G');
        append_char(dest, len, max_len, 'B');
    } else {
        uint32_t whole = bytes / (1024u * 1024u);
        uint32_t frac = (bytes % (1024u * 1024u)) / ((1024u * 1024u) / 100u);
        append_decimal_2(dest, len, max_len, whole, frac);
        append_char(dest, len, max_len, ' ');
        append_char(dest, len, max_len, 'M');
        append_char(dest, len, max_len, 'B');
    }
}

static void format_single_memory_amount(char *buffer, size_t max_len, uint32_t bytes) {
    size_t len = 0;
    append_memory_amount(buffer, &len, max_len, bytes);
}

static void append_frequency_label(char *dest, size_t *len, size_t max_len, uint32_t mhz) {
    if (mhz >= 1000u) {
        append_decimal_2(dest, len, max_len, mhz / 1000u, (mhz % 1000u) / 10u);
        append_char(dest, len, max_len, ' ');
        append_char(dest, len, max_len, 'G');
        append_char(dest, len, max_len, 'H');
        append_char(dest, len, max_len, 'z');
    } else {
        append_uint(dest, len, max_len, mhz);
        append_char(dest, len, max_len, ' ');
        append_char(dest, len, max_len, 'M');
        append_char(dest, len, max_len, 'H');
        append_char(dest, len, max_len, 'z');
    }
}


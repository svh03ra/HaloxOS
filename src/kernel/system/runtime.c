// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: runtime.c, runtime handling.

// This repository is licensed under the GNU General Public License.

static void *memset_local(void *dest, int value, size_t len) {
    uint8_t *ptr = (uint8_t *)dest;
    for (size_t i = 0; i < len; ++i) {
        ptr[i] = (uint8_t)value;
    }
    return dest;
}

static void *memcpy_local(void *dest, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
    return dest;
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


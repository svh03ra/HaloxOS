// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: model.c, terminal data model.

// This repository is licensed under the GNU General Public License.

static void terminal_reset(Terminal *term) {
    memset_local(term, 0, sizeof(*term));
}

static void terminal_add_line(Terminal *term, const char *text) {
    if (term->wrap_chars > 0) {
        int len = (int)strlen_local(text);
        int pos = 0;
        while (pos < len) {
            int chunk = len - pos;
            if (chunk > term->wrap_chars) chunk = term->wrap_chars;
            char buf[TERM_LINE_LEN];
            memcpy_local(buf, text + pos, chunk);
            buf[chunk] = '\0';
            if (term->line_count == TERM_MAX_LINES) {
                for (int i = 1; i < TERM_MAX_LINES; ++i) {
                    memcpy_local(term->lines[i - 1], term->lines[i], TERM_LINE_LEN);
                }
                --term->line_count;
            }
            copy_string(term->lines[term->line_count], buf, TERM_LINE_LEN);
            ++term->line_count;
            pos += chunk;
        }
    } else {
        if (term->line_count == TERM_MAX_LINES) {
            for (int i = 1; i < TERM_MAX_LINES; ++i) {
                memcpy_local(term->lines[i - 1], term->lines[i], TERM_LINE_LEN);
            }
            --term->line_count;
        }
        copy_string(term->lines[term->line_count], text, TERM_LINE_LEN);
        ++term->line_count;
    }
}

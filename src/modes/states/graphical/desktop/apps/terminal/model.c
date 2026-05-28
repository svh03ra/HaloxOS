static void terminal_reset(Terminal *term) {
    memset_local(term, 0, sizeof(*term));
}

static void terminal_add_line(Terminal *term, const char *text) {
    if (term->line_count == TERM_MAX_LINES) {
        for (int i = 1; i < TERM_MAX_LINES; ++i) {
            memcpy_local(term->lines[i - 1], term->lines[i], TERM_LINE_LEN);
        }
        --term->line_count;
    }
    copy_string(term->lines[term->line_count], text, TERM_LINE_LEN);
    ++term->line_count;
}

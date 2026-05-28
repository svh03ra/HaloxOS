static void render_boot_terminal_text(void) {
    uint32_t blink_phase = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;

    if (!boot_terminal_dirty && blink_phase == boot_terminal_last_blink) {
        return;
    }

    const int top_row = 4;
    const int max_rows = 18;
    int start = boot_term.line_count > max_rows ? boot_term.line_count - max_rows : 0;
    int row = top_row;

    vga_text_clear(VGA_TEXT_ATTR_GRAY);
    draw_text_mode_row(0, 0, "HaloxOS Command Prompt", VGA_TEXT_ATTR_GRAY);
    draw_text_mode_row(1, 0, "Type 'boot' to start the desktop. ESC returns to boot menu.", VGA_TEXT_ATTR_GRAY);

    for (int i = start; i < boot_term.line_count && row < top_row + max_rows; ++i, ++row) {
        draw_text_mode_row(row, 0, boot_term.lines[i], VGA_TEXT_ATTR_GRAY);
    }

    draw_text_mode_row(23, 0, "A:\\>", VGA_TEXT_ATTR_GRAY);
    draw_text_mode_row(23, 4, boot_term.input, VGA_TEXT_ATTR_GRAY);
    vga_text_enable_cursor(14, 15);
    vga_text_set_cursor(clampi(4 + boot_term.input_len, 0, VGA_TEXT_COLS - 1), 23);
    boot_terminal_dirty = false;
    boot_terminal_last_blink = blink_phase;
}

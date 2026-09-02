// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: graphics.c, framebuffer graphics rendering driver.

// This repository is licensed under the GNU General Public License.

static uint8_t nearest_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t best_distance = 0xFFFFFFFFu;
    uint8_t best_index = 0;

    for (int i = 0; i < 256; ++i) {
        int dr = (int)palette[i].r - r;
        int dg = (int)palette[i].g - g;
        int db = (int)palette[i].b - b;
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = (uint8_t)i;
        }
    }

    return best_index;
}

static void init_theme_colors(void) {
    color_black = nearest_color(0, 0, 0);
    color_white = nearest_color(255, 255, 255);
    color_gray_dark = nearest_color(64, 64, 64);
    color_gray = nearest_color(128, 128, 128);
    color_gray_light = nearest_color(210, 210, 210);
    color_green = nearest_color(0, 170, 0);
    color_green_dark = nearest_color(0, 100, 0);
    color_blue = nearest_color(60, 110, 220);
    color_blue_dark = nearest_color(20, 40, 100);
    color_red = nearest_color(180, 40, 40);
    color_yellow = nearest_color(230, 210, 40);
    color_orange = nearest_color(230, 130, 40);
    color_pink = nearest_color(220, 140, 180);
    color_desktop_icon = nearest_color(245, 245, 245);
    paint_color = color_black;
}

static bool framebuffer_text_mode_active(void) {
    if (vga_native_text_mode_active()) {
        return false;
    }
    return boot_text_mode && video_backend == VIDEO_BACKEND_MULTIBOOT && fb.address != NULL;
}

static uint8_t text_attr_foreground(uint8_t attr) {
    switch (attr & 0x0Fu) {
        case 0x0: return color_black;
        case 0x1: return color_blue_dark;
        case 0x4: return color_red;
        case 0x7: return color_gray_light;
        case 0x9: return color_blue;
        case 0xF: return color_white;
        default: return color_white;
    }
}

static uint8_t text_attr_background(uint8_t attr) {
    switch ((attr >> 4) & 0x07u) {
        case 0x1: return color_blue_dark;
        case 0x4: return color_red;
        case 0x7: return color_gray_light;
        default: return color_black;
    }
}

static void set_default_framebuffer_format(uint8_t bpp) {
    fb.type = bpp == 8 ? MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED : MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
    if (bpp == 15) {
        fb.red_position = 10;
        fb.red_mask_size = 5;
        fb.green_position = 5;
        fb.green_mask_size = 5;
        fb.blue_position = 0;
        fb.blue_mask_size = 5;
    } else if (bpp == 16) {
        fb.red_position = 11;
        fb.red_mask_size = 5;
        fb.green_position = 5;
        fb.green_mask_size = 6;
        fb.blue_position = 0;
        fb.blue_mask_size = 5;
    } else {
        fb.red_position = 16;
        fb.red_mask_size = 8;
        fb.green_position = 8;
        fb.green_mask_size = 8;
        fb.blue_position = 0;
        fb.blue_mask_size = 8;
    }
}

static bool framebuffer_bpp_supported(uint8_t type, uint8_t bpp) {
    if (type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED) {
        return bpp == 8;
    }
    if (type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        return bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32;
    }
    return false;
}

static uint32_t scale_channel_to_mask(uint8_t value, uint8_t bits) {
    uint32_t max_value;

    if (bits == 0) {
        return 0;
    }
    if (bits >= 8) {
        return value;
    }

    max_value = (1u << bits) - 1u;
    return ((uint32_t)value * max_value + 127u) / 255u;
}

static uint32_t pack_framebuffer_color(Color color) {
    return (scale_channel_to_mask(color.r, fb.red_mask_size) << fb.red_position) |
           (scale_channel_to_mask(color.g, fb.green_mask_size) << fb.green_position) |
           (scale_channel_to_mask(color.b, fb.blue_mask_size) << fb.blue_position);
}

static void program_vga_palette(void) {
    if (video_backend == VIDEO_BACKEND_VGA) {
        vga_sync_palette();
        return;
    }

    if (fb.bpp != 8) {
        return;
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        for (int i = 0; i < 256; ++i) {
            Color output = settings_applied.palette_mode == 1 ? quantize_color_16(palette[i]) : palette[i];
            vmware_write_reg(SVGA_PALETTE_BASE + i * 3 + 0, output.r);
            vmware_write_reg(SVGA_PALETTE_BASE + i * 3 + 1, output.g);
            vmware_write_reg(SVGA_PALETTE_BASE + i * 3 + 2, output.b);
        }
        return;
    }

    outb(0x3C8, 0);
    for (int i = 0; i < 256; ++i) {
        Color output = settings_applied.palette_mode == 1 ? quantize_color_16(palette[i]) : palette[i];
        outb(0x3C9, output.r / 4);
        outb(0x3C9, output.g / 4);
        outb(0x3C9, output.b / 4);
    }
}

/*
 * Software color quantization for direct-color framebuffers.
 *
 * Bootloader (Multiboot) framebuffers cannot be re-moded from the kernel,
 * so when the configured color mode is Default (256 colors) or Low (16
 * colors) the presented RGB image is quantized in software. True-Color!
 * keeps the full RGB path untouched.
 *
 * A 64K RGB565 lookup table is built once per color-mode change so the
 * per-frame cost is a simple array lookup instead of a 256-entry nearest
 * search per pixel.
 */
static uint8_t quant_lut[65536];
static int8_t quant_lut_mode = -1;

static void build_quant_lut(uint8_t mode) {
    for (uint32_t v = 0; v < 65536; ++v) {
        Color c = rgb565_to_color((uint16_t)v);
        if (mode == 1) {
            uint32_t best_distance = 0xFFFFFFFFu;
            uint8_t best_index = 0;
            for (int j = 0; j < 16; ++j) {
                int dr = (int)vga_ega16_colors[j].r - (int)c.r;
                int dg = (int)vga_ega16_colors[j].g - (int)c.g;
                int db = (int)vga_ega16_colors[j].b - (int)c.b;
                uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
                if (distance < best_distance) {
                    best_distance = distance;
                    best_index = (uint8_t)j;
                }
            }
            quant_lut[v] = best_index;
        } else {
            quant_lut[v] = nearest_color(c.r, c.g, c.b);
        }
    }
    quant_lut_mode = (int8_t)mode;
}

static Color present_color_for(uint16_t rgb565) {
    if (settings_applied.palette_mode == 2) {
        return rgb565_to_color(rgb565);
    }
    if (quant_lut_mode != (int8_t)settings_applied.palette_mode) {
        build_quant_lut(settings_applied.palette_mode);
    }
    if (settings_applied.palette_mode == 1) {
        return vga_ega16_colors[quant_lut[rgb565]];
    }
    return palette[quant_lut[rgb565]];
}

static bool init_framebuffer(uint32_t magic, const MultibootInfo *mbi) {
    uint32_t bar0 = 0;
    bool bga_text_boot_backend_ready = false;

    memset_local(&fb, 0, sizeof(fb));
    video_backend = VIDEO_BACKEND_NONE;
    memset_local(&vmware_svga, 0, sizeof(vmware_svga));

    /*
     * Prefer the BGA backend when present (QEMU std VGA, v86): it can
     * leave the bootloader framebuffer for a real DOS text-mode boot
     * menu and re-enter graphics afterwards via the DISPI registers.
     * The currently active mode set by the bootloader is adopted as-is.
     */
    if (detect_bga_backend() && find_vga_framebuffer_bar(&bar0)) {
        bga_text_boot_backend_ready = true;
        uint16_t current_width = bga_read(VBE_DISPI_INDEX_XRES);
        uint16_t current_height = bga_read(VBE_DISPI_INDEX_YRES);
        uint16_t current_bpp = bga_read(VBE_DISPI_INDEX_BPP);
        uint8_t current_type = current_bpp == 8 ? MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED
                                                : MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
        if (current_width >= OS_WIDTH && current_height >= OS_HEIGHT &&
            current_width <= MAX_OUTPUT_WIDTH && current_height <= MAX_OUTPUT_HEIGHT &&
            framebuffer_bpp_supported(current_type, (uint8_t)current_bpp) &&
            (bga_read(VBE_DISPI_INDEX_ENABLE) & VBE_DISPI_ENABLED) != 0) {
            fb.address = (uint8_t *)(uintptr_t)bar0;
            fb.width = current_width;
            fb.height = current_height;
            fb.bpp = (uint8_t)current_bpp;
            fb.pitch = fb.width * ((fb.bpp + 7u) / 8u);
            set_default_framebuffer_format(fb.bpp);
            video_backend = VIDEO_BACKEND_BGA;
            update_present_maps();
            return true;
        }
    }

    if (magic == 0x2BADB002 && mbi != NULL) {
        /*
         * Multiboot1 flag 12 explicitly says the framebuffer fields are
         * present. Always log the raw hand-off before validating it: this
         * makes a GRUB mode-selection problem distinguishable from a VGA
         * backend problem on real hardware.
         */
        serial_trace_uint_value("INFO", "Multiboot flags", mbi->flags);
        if ((mbi->flags & (1u << 12)) != 0) {
            serial_trace_uint_value("INFO", "Multiboot framebuffer width", mbi->framebuffer_width);
            serial_trace_uint_value("INFO", "Multiboot framebuffer height", mbi->framebuffer_height);
            serial_trace_uint_value("INFO", "Multiboot framebuffer bpp", mbi->framebuffer_bpp);
            serial_trace_uint_value("INFO", "Multiboot framebuffer type", mbi->framebuffer_type);
            serial_trace_uint_value("INFO", "Multiboot framebuffer pitch", mbi->framebuffer_pitch);
        }

        uint8_t framebuffer_type = mbi->framebuffer_type;
        if ((mbi->flags & (1u << 12)) != 0 &&
            mbi->framebuffer_addr <= 0xFFFFFFFFull &&
            framebuffer_bpp_supported(framebuffer_type, mbi->framebuffer_bpp)) {
            fb.address = (uint8_t *)(uintptr_t)mbi->framebuffer_addr;
            fb.width = mbi->framebuffer_width;
            fb.height = mbi->framebuffer_height;
            fb.pitch = mbi->framebuffer_pitch;
            fb.bpp = mbi->framebuffer_bpp;
            fb.type = framebuffer_type;
        }
        if (fb.address != NULL &&
            fb.width >= OS_WIDTH &&
            fb.height >= OS_HEIGHT &&
            fb.width <= MAX_OUTPUT_WIDTH &&
            fb.height <= MAX_OUTPUT_HEIGHT &&
            fb.pitch >= fb.width * ((fb.bpp + 7u) / 8u)) {
            if (fb.type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB &&
                mbi->framebuffer_red_mask_size != 0 &&
                mbi->framebuffer_green_mask_size != 0 &&
                mbi->framebuffer_blue_mask_size != 0) {
                fb.red_position = mbi->framebuffer_red_field_position;
                fb.red_mask_size = mbi->framebuffer_red_mask_size;
                fb.green_position = mbi->framebuffer_green_field_position;
                fb.green_mask_size = mbi->framebuffer_green_mask_size;
                fb.blue_position = mbi->framebuffer_blue_field_position;
                fb.blue_mask_size = mbi->framebuffer_blue_mask_size;
            } else {
                set_default_framebuffer_format(fb.bpp);
            }
            video_backend = VIDEO_BACKEND_MULTIBOOT;
            update_present_maps();
            return true;
        }
    }

    if (init_vmware_svga_backend()) {
        return true;
    }

    /*
     * BGA hardware booted in text mode (gfxpayload=text): keep the BGA
     * backend so pressing 1 programs a linear framebuffer via DISPI.
     * The classic VGA probe below would otherwise claim the card and the
     * 8/16-bit color modes would be lost. Only classic cards fall through.
     */
    if (bga_text_boot_backend_ready) {
        uint32_t bar0_text = 0;
        if (find_vga_framebuffer_bar(&bar0_text)) {
            fb.address = (uint8_t *)(uintptr_t)bar0_text;
            fb.width = OS_WIDTH;
            fb.height = OS_HEIGHT;
            fb.pitch = OS_WIDTH;
            fb.bpp = 8;
            fb.type = MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED;
            set_default_framebuffer_format(fb.bpp);
            video_backend = VIDEO_BACKEND_BGA;
            update_present_maps();
            serial_trace("INFO", "BGA backend: adopted from text boot");
            return true;
        }
    }

    if (init_vga_backend()) {
        serial_trace("INFO", "VGA backend: available");
        return true;
    }

    serial_trace("ERROR", "no compatible video backend detected");
    return true;
}

static void present(void) {
    if (boot_text_mode && !framebuffer_text_mode_active()) {
        return;
    }

    if (fb.address == NULL) {
        return;
    }

    if (video_backend == VIDEO_BACKEND_VGA) {
        vga_present();
        return;
    }

    if (fb.bpp == 8 && fb.width == OS_WIDTH && fb.height == OS_HEIGHT) {
        for (int y = 0; y < OS_HEIGHT; ++y) {
            uint8_t *dest = fb.address + (size_t)y * fb.pitch;
            memcpy_local(dest, &backbuffer[y * OS_WIDTH], OS_WIDTH);
        }
        if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
            vmware_update_screen();
        }
        return;
    }

    for (uint32_t y = 0; y < fb.height; ++y) {
        bool inside_y = y >= present_offset_y && y < present_offset_y + present_content_height;
        uint16_t sy = inside_y ? present_y_map[y] : 0;
        const uint8_t *src = &backbuffer[sy * OS_WIDTH];

        if (fb.bpp == 8) {
            uint8_t *dest = fb.address + (size_t)y * fb.pitch;
            for (uint32_t x = 0; x < fb.width; ++x) {
                bool inside = inside_y && x >= present_offset_x && x < present_offset_x + present_content_width;
                dest[x] = inside ? src[present_x_map[x]] : color_black;
            }
            continue;
        }

        if (fb.bpp == 15 || fb.bpp == 16) {
            uint16_t *dest = (uint16_t *)(fb.address + (size_t)y * fb.pitch);
            for (uint32_t x = 0; x < fb.width; ++x) {
                bool inside = inside_y && x >= present_offset_x && x < present_offset_x + present_content_width;
                Color c = inside ? rgb565_to_color(backbuffer_rgb565[(size_t)sy * OS_WIDTH + present_x_map[x]]) : (Color){0, 0, 0};
                if (settings_applied.palette_mode == 1) {
                    c = quantize_color_16(c);
                }
                dest[x] = (uint16_t)pack_framebuffer_color(c);
            }
            continue;
        }

        if (fb.bpp == 15 || fb.bpp == 16) {
            uint16_t *dest = (uint16_t *)(fb.address + (size_t)y * fb.pitch);
            for (uint32_t x = 0; x < fb.width; ++x) {
                Color c = present_color_for(backbuffer_rgb565[(size_t)sy * OS_WIDTH + present_x_map[x]]);
                dest[x] = (uint16_t)pack_framebuffer_color(c);
            }
            continue;
        }

        if (fb.bpp == 24) {
            uint8_t *dest = fb.address + (size_t)y * fb.pitch;
            for (uint32_t x = 0; x < fb.width; ++x) {
                Color c = present_color_for(backbuffer_rgb565[(size_t)sy * OS_WIDTH + present_x_map[x]]);
                uint32_t packed = pack_framebuffer_color(c);
                dest[x * 3 + 0] = (uint8_t)(packed & 0xFFu);
                dest[x * 3 + 1] = (uint8_t)((packed >> 8) & 0xFFu);
                dest[x * 3 + 2] = (uint8_t)((packed >> 16) & 0xFFu);
            }
            continue;
        }

        {
            uint32_t *dest = (uint32_t *)(fb.address + (size_t)y * fb.pitch);
            for (uint32_t x = 0; x < fb.width; ++x) {
                Color c = present_color_for(backbuffer_rgb565[(size_t)sy * OS_WIDTH + present_x_map[x]]);
                dest[x] = pack_framebuffer_color(c);
            }
        }
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        vmware_update_screen();
    }
}

static void clear_screen(uint8_t color) {
    memset_local(backbuffer, color, sizeof(backbuffer));
    {
        uint16_t rgb = palette_rgb565(color);
        for (int i = 0; i < OS_WIDTH * OS_HEIGHT; ++i) {
            backbuffer_rgb565[i] = rgb;
        }
    }
}

static void draw_pixel(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= OS_WIDTH || y >= OS_HEIGHT) {
        return;
    }
    backbuffer[y * OS_WIDTH + x] = color;
    backbuffer_rgb565[y * OS_WIDTH + x] = palette_rgb565(color);
}

static void fill_rect(int x, int y, int w, int h, uint8_t color) {
    int x0 = clampi(x, 0, OS_WIDTH);
    int y0 = clampi(y, 0, OS_HEIGHT);
    int x1 = clampi(x + w, 0, OS_WIDTH);
    int y1 = clampi(y + h, 0, OS_HEIGHT);

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            backbuffer[py * OS_WIDTH + px] = color;
            backbuffer_rgb565[py * OS_WIDTH + px] = palette_rgb565(color);
        }
    }
}

static void draw_rect(int x, int y, int w, int h, uint8_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

static void draw_char(int x, int y, char ch, uint8_t fg, uint8_t bg, bool transparent) {
    uint8_t glyph_index;

    if ((unsigned char)ch < 32 || (unsigned char)ch > 127) {
        glyph_index = 0;
    } else {
        glyph_index = (uint8_t)((unsigned char)ch - 32);
    }

    for (int row = 0; row < 8; ++row) {
        uint8_t bits = font8x8_basic[glyph_index][row];
        for (int col = 0; col < 8; ++col) {
            if ((bits >> col) & 1u) {
                draw_pixel(x + col, y + row, fg);
            } else if (!transparent) {
                draw_pixel(x + col, y + row, bg);
            }
        }
    }
}

static void draw_char_scaled(int x, int y, char ch, uint8_t fg, uint8_t bg, bool transparent, int scale) {
    uint8_t glyph_index;
    if (scale < 1) {
        scale = 1;
    }

    if ((unsigned char)ch < 32 || (unsigned char)ch > 127) {
        glyph_index = 0;
    } else {
        glyph_index = (uint8_t)((unsigned char)ch - 32);
    }

    for (int row = 0; row < 8; ++row) {
        uint8_t bits = font8x8_basic[glyph_index][row];
        for (int col = 0; col < 8; ++col) {
            bool set = ((bits >> col) & 1u) != 0;
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    if (set) {
                        draw_pixel(x + col * scale + sx, y + row * scale + sy, fg);
                    } else if (!transparent) {
                        draw_pixel(x + col * scale + sx, y + row * scale + sy, bg);
                    }
                }
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent) {
    int cursor_x = x;
    int cursor_y = y;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\n') {
            cursor_x = x;
            cursor_y += 10;
            continue;
        }
        draw_char(cursor_x, cursor_y, text[i], fg, bg, transparent);
        cursor_x += 8;
    }
}

static void draw_text_scaled(int x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent, int scale) {
    int cursor_x = x;
    int cursor_y = y;

    if (scale < 1) {
        scale = 1;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\n') {
            cursor_x = x;
            cursor_y += 10 * scale;
            continue;
        }
        draw_char_scaled(cursor_x, cursor_y, text[i], fg, bg, transparent, scale);
        cursor_x += 8 * scale;
    }
}

static void draw_text_center(int center_x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent) {
    int x = center_x - (int)(strlen_local(text) * 8) / 2;
    draw_text(x, y, text, fg, bg, transparent);
}

static void draw_text_center_scaled(int center_x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent, int scale) {
    if (scale < 1) {
        scale = 1;
    }
    int x = center_x - (int)(strlen_local(text) * 8 * scale) / 2;
    draw_text_scaled(x, y, text, fg, bg, transparent, scale);
}

static void framebuffer_text_write_at(int col, int row, const char *text, uint8_t attr) {
    uint8_t fg = text_attr_foreground(attr);
    uint8_t bg = text_attr_background(attr);
    int x;
    int y;

    if (row < 0 || row >= VGA_TEXT_ROWS || col >= VGA_TEXT_COLS) {
        return;
    }

    if (col < 0) {
        text -= col;
        col = 0;
    }

    x = col * 8;
    y = row * 16;
    for (int i = 0; text[i] != '\0' && col + i < VGA_TEXT_COLS; ++i) {
        fill_rect(x + i * 8, y, 8, 16, bg);
        draw_char(x + i * 8, y + 4, text[i], fg, bg, true);
    }
}

static void vga_text_write_at(int col, int row, const char *text, uint8_t attr) {
    if (framebuffer_text_mode_active()) {
        framebuffer_text_write_at(col, row, text, attr);
        return;
    }

    if (row < 0 || row >= VGA_TEXT_ROWS || col >= VGA_TEXT_COLS) {
        return;
    }

    if (col < 0) {
        text -= col;
        col = 0;
    }

    for (int i = 0; text[i] != '\0' && col + i < VGA_TEXT_COLS; ++i) {
        vga_text_buffer[row * VGA_TEXT_COLS + col + i] = ((uint16_t)attr << 8) | (uint8_t)text[i];
    }
}

static void vga_text_clear(uint8_t attr) {
    if (framebuffer_text_mode_active()) {
        clear_screen(text_attr_background(attr));
        return;
    }

    for (int i = 0; i < VGA_TEXT_COLS * VGA_TEXT_ROWS; ++i) {
        vga_text_buffer[i] = ((uint16_t)attr << 8) | ' ';
    }
}

static void vga_text_disable_cursor(void) {
    if (framebuffer_text_mode_active()) {
        return;
    }

    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static void vga_text_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    if (framebuffer_text_mode_active()) {
        return;
    }

    outb(0x3D4, 0x0A);
    outb(0x3D5, (uint8_t)((inb(0x3D5) & 0xC0) | cursor_start));
    outb(0x3D4, 0x0B);
    outb(0x3D5, (uint8_t)((inb(0x3D5) & 0xE0) | cursor_end));
}

static void vga_text_set_cursor(int col, int row) {
    uint16_t pos = (uint16_t)(row * VGA_TEXT_COLS + col);
    if (framebuffer_text_mode_active()) {
        fill_rect(col * 8, row * 16 + 14, 8, 2, color_gray_light);
        return;
    }

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFFu));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFFu));
}

static void draw_text_mode_row(int row, int col, const char *text, uint8_t attr) {
    vga_text_write_at(col, row, text, attr);
}

static void draw_text_mode_center(int row, const char *text, uint8_t attr) {
    int col = (VGA_TEXT_COLS - (int)strlen_local(text)) / 2;
    if (col < 0) {
        col = 0;
    }
    vga_text_write_at(col, row, text, attr);
}

static void draw_text_clipped(int x, int y, int max_w, const char *text, uint8_t fg, uint8_t bg, bool transparent) {
    int max_chars = max_w / 8;
    int cursor_x = x;

    if (max_chars <= 0) {
        return;
    }

    for (int i = 0; text[i] != '\0' && i < max_chars; ++i) {
        draw_char(cursor_x, y, text[i], fg, bg, transparent);
        cursor_x += 8;
    }
}

static void draw_text_block(int x, int y, int w, int h, const char *text, uint8_t fg, uint8_t bg, bool transparent) {
    int max_cols = w / 8;
    int max_rows = h / 10;
    int row = 0;
    int col = 0;

    if (max_cols <= 0 || max_rows <= 0) {
        return;
    }

    for (int i = 0; text[i] != '\0'; ++i) {
        char ch = text[i];
        if (ch == '\n') {
            ++row;
            col = 0;
            if (row >= max_rows) {
                break;
            }
            continue;
        }

        if (col >= max_cols) {
            ++row;
            col = 0;
            if (row >= max_rows) {
                break;
            }
        }

        draw_char(x + col * 8, y + row * 10, ch, fg, bg, transparent);
        ++col;
    }
}

static bool point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static uint16_t image_width(const uint8_t *image) {
    return (uint16_t)(image[0] | ((uint16_t)image[1] << 8));
}

static uint16_t image_height(const uint8_t *image) {
    return (uint16_t)(image[2] | ((uint16_t)image[3] << 8));
}

static const uint8_t *image_pixels(const uint8_t *image) {
    return image + 4;
}

static const uint8_t *image_alpha(const uint8_t *image) {
    size_t count = (size_t)image_width(image) * image_height(image);
    return image + 4 + count;
}

static const uint16_t *image_rgb565(const uint8_t *image) {
    size_t count = (size_t)image_width(image) * image_height(image);
    return (const uint16_t *)(const void *)(image + 4 + count + count);
}

static uint16_t palette_rgb565(uint8_t color) {
    Color c = palette[color];
    return (uint16_t)(((uint16_t)(c.r >> 3) << 11) |
                      ((uint16_t)(c.g >> 2) << 5) |
                      (uint16_t)(c.b >> 3));
}

static Color rgb565_to_color(uint16_t value) {
    Color c;
    c.r = (uint8_t)((((value >> 11) & 0x1Fu) * 255u) / 31u);
    c.g = (uint8_t)((((value >> 5) & 0x3Fu) * 255u) / 63u);
    c.b = (uint8_t)(((value & 0x1Fu) * 255u) / 31u);
    return c;
}

static int text_pixel_width(const char *text) {
    return (int)strlen_local(text) * 8;
}

static void draw_image_at(const uint8_t *image, int x, int y, bool transparent) {
    uint16_t width = image_width(image);
    uint16_t height = image_height(image);
    const uint8_t *pixels = image_pixels(image);
    const uint8_t *alpha = image_alpha(image);
    const uint16_t *rgb565 = image_rgb565(image);

    for (uint16_t py = 0; py < height; ++py) {
        for (uint16_t px = 0; px < width; ++px) {
            size_t index = (size_t)py * width + px;
            if (!transparent || alpha[index] >= 128) {
                int dx = x + px;
                int dy = y + py;
                if (dx >= 0 && dy >= 0 && dx < OS_WIDTH && dy < OS_HEIGHT) {
                    backbuffer[dy * OS_WIDTH + dx] = pixels[index];
                    backbuffer_rgb565[dy * OS_WIDTH + dx] = rgb565[index];
                }
            }
        }
    }
}

static void draw_image(const uint8_t *image) {
    draw_image_at(image, 0, 0, false);
}

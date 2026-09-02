// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: vga_legacy.c, register-level VGA compatibility driver.

// This repository is licensed under the GNU General Public License.

/*
 * These are the two graphics modes guaranteed by VGA itself.  They do not
 * need a VBE BIOS call or a linear framebuffer, so they also work on ISA VGA
 * cards and on emulators configured with an IBM-compatible VGA adapter.
 */
typedef enum {
    VGA_LEGACY_MODE_NONE,
    VGA_LEGACY_MODE_640X480X16,
    VGA_LEGACY_MODE_320X240X256
} VgaLegacyMode;

static VgaLegacyMode vga_legacy_mode = VGA_LEGACY_MODE_NONE;
static uint8_t vga_rgb565_to_ega[65536];
static bool vga_rgb565_ega_ready = false;

static const Color vga_ega_colors[16] = {
    {0, 0, 0},       {0, 0, 170},     {0, 170, 0},    {0, 170, 170},
    {170, 0, 0},     {170, 0, 170},   {170, 85, 0},   {170, 170, 170},
    {85, 85, 85},    {85, 85, 255},   {85, 255, 85},  {85, 255, 255},
    {255, 85, 85},   {255, 85, 255},  {255, 255, 85}, {255, 255, 255}
};

static const uint8_t vga_seq_640x480x16[5] = {
    0x03, 0x01, 0x0F, 0x00, 0x02
};

static const uint8_t vga_crtc_640x480x16[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 0x00, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x8C, 0xDF, 0x28,
    0x00, 0xE7, 0x04, 0xE3, 0xFF
};

static const uint8_t vga_gc_640x480x16[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF
};

static const uint8_t vga_seq_320x240x256[5] = {
    0x03, 0x01, 0x0F, 0x00, 0x06
};

static const uint8_t vga_crtc_320x240x256[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0D, 0x3E, 0x00, 0x41,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0xAC, 0xDF, 0x28,
    0x00, 0xE7, 0x06, 0xE3, 0xFF
};

static const uint8_t vga_gc_320x240x256[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF
};

static const uint8_t vga_seq_text[5] = {
    0x03, 0x00, 0x03, 0x00, 0x07
};

static const uint8_t vga_crtc_text[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F,
    0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50, 0x9C, 0x8E, 0x8F, 0x28,
    0x1F, 0x96, 0xB9, 0xA3, 0xFF
};

static const uint8_t vga_gc_text[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF
};

static void vga_set_map_mask(uint8_t mask) {
    outb(0x3C4, 0x02);
    outb(0x3C5, mask);
}

static void vga_write_registers(uint8_t misc,
                                const uint8_t *sequencer,
                                const uint8_t *crtc,
                                const uint8_t *graphics,
                                uint8_t attribute_mode,
                                bool text_mode) {
    static const uint8_t text_palette[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
    };

    outb(0x3C4, 0x00);
    outb(0x3C5, 0x01);
    outb(0x3C2, misc);

    for (uint8_t index = 1; index < 5; ++index) {
        outb(0x3C4, index);
        outb(0x3C5, sequencer[index]);
    }

    outb(0x3D4, 0x03);
    outb(0x3D5, (uint8_t)(inb(0x3D5) | 0x80u));
    outb(0x3D4, 0x11);
    outb(0x3D5, (uint8_t)(inb(0x3D5) & 0x7Fu));
    for (uint8_t index = 0; index < 25; ++index) {
        outb(0x3D4, index);
        outb(0x3D5, crtc[index]);
    }

    for (uint8_t index = 0; index < 9; ++index) {
        outb(0x3CE, index);
        outb(0x3CF, graphics[index]);
    }

    (void)inb(0x3DA);
    for (uint8_t index = 0; index < 16; ++index) {
        outb(0x3C0, index);
        outb(0x3C0, text_mode ? text_palette[index] : index);
    }
    outb(0x3C0, 0x10);
    outb(0x3C0, attribute_mode);
    outb(0x3C0, 0x11);
    outb(0x3C0, 0x00);
    outb(0x3C0, 0x12);
    outb(0x3C0, 0x0F);
    outb(0x3C0, 0x13);
    outb(0x3C0, text_mode ? 0x08 : 0x00);
    outb(0x3C0, 0x14);
    outb(0x3C0, 0x00);
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);

    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03);
}

static void vga_clear_graphics_memory(void) {
    volatile uint8_t *vram = (volatile uint8_t *)(uintptr_t)0xA0000;

    vga_set_map_mask(0x0F);
    for (uint32_t offset = 0; offset < 0x10000u; ++offset) {
        vram[offset] = 0;
    }
}

static uint8_t vga_nearest_ega_color(Color color) {
    uint32_t best_distance = 0xFFFFFFFFu;
    uint8_t best_index = 0;

    for (uint8_t index = 0; index < 16; ++index) {
        int dr = (int)vga_ega_colors[index].r - color.r;
        int dg = (int)vga_ega_colors[index].g - color.g;
        int db = (int)vga_ega_colors[index].b - color.b;
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = index;
        }
    }

    return best_index;
}

static void vga_build_rgb565_ega_map(void) {
    if (vga_rgb565_ega_ready) {
        return;
    }

    for (uint32_t value = 0; value < 65536u; ++value) {
        Color color = rgb565_to_color((uint16_t)value);
        vga_rgb565_to_ega[value] = vga_nearest_ega_color(color);
    }

    vga_rgb565_ega_ready = true;
}

static void vga_sync_palette(void) {
    outb(0x3C6, 0xFF);
    outb(0x3C8, 0);

    if (vga_legacy_mode == VGA_LEGACY_MODE_640X480X16) {
        for (uint8_t index = 0; index < 16; ++index) {
            Color color = vga_ega_colors[index];
            outb(0x3C9, color.r >> 2);
            outb(0x3C9, color.g >> 2);
            outb(0x3C9, color.b >> 2);
        }
        return;
    }

    for (uint16_t index = 0; index < 256; ++index) {
        Color color = palette[index];
        outb(0x3C9, color.r >> 2);
        outb(0x3C9, color.g >> 2);
        outb(0x3C9, color.b >> 2);
    }
}

static bool init_vga_legacy_backend(void) {
    uint8_t misc = inb(0x3CC);
    uint8_t status = inb(0x3DA);

    if (misc == 0xFFu && status == 0xFFu) {
        return false;
    }

    vga_legacy_mode = VGA_LEGACY_MODE_NONE;
    video_backend = VIDEO_BACKEND_VGA;
    return true;
}

static bool vga_set_legacy_mode(uint16_t width, uint16_t height, uint16_t bpp) {
    if (bpp == 4) {
        if (width != 640 || height != 480) {
            return false;
        }
        vga_write_registers(0xE3,
                            vga_seq_640x480x16,
                            vga_crtc_640x480x16,
                            vga_gc_640x480x16,
                            0x01,
                            false);
        vga_legacy_mode = VGA_LEGACY_MODE_640X480X16;
        fb.width = 640;
        fb.height = 480;
        fb.pitch = 80;
        fb.bpp = 4;
    } else if (bpp == 8) {
        /*
         * Mode X is a real 320x240, 256-colour VGA mode.  It is the closest
         * native legacy mode when a requested VBE-sized 8-bit mode is absent.
         */
        vga_write_registers(0xE3,
                            vga_seq_320x240x256,
                            vga_crtc_320x240x256,
                            vga_gc_320x240x256,
                            0x41,
                            false);
        vga_legacy_mode = VGA_LEGACY_MODE_320X240X256;
        fb.width = 320;
        fb.height = 240;
        fb.pitch = 80;
        fb.bpp = 8;
    } else {
        return false;
    }

    fb.address = (uint8_t *)(uintptr_t)0xA0000;
    fb.type = 0;
    fb.red_position = 0;
    fb.red_mask_size = 0;
    fb.green_position = 0;
    fb.green_mask_size = 0;
    fb.blue_position = 0;
    fb.blue_mask_size = 0;
    vga_clear_graphics_memory();
    vga_build_rgb565_ega_map();
    update_present_maps();
    vga_sync_palette();
    return true;
}

static void vga_restore_text_mode(void) {
    if (vga_legacy_mode == VGA_LEGACY_MODE_NONE) {
        return;
    }

    vga_write_registers(0x67,
                        vga_seq_text,
                        vga_crtc_text,
                        vga_gc_text,
                        0x0C,
                        true);
    vga_legacy_mode = VGA_LEGACY_MODE_NONE;
}

static void vga_present(void) {
    volatile uint8_t *vram = (volatile uint8_t *)(uintptr_t)0xA0000;

    if (vga_legacy_mode == VGA_LEGACY_MODE_640X480X16) {
        for (uint8_t plane = 0; plane < 4; ++plane) {
            vga_set_map_mask((uint8_t)(1u << plane));
            for (uint32_t y = 0; y < 480; ++y) {
                const uint8_t *source = &backbuffer[(size_t)present_y_map[y] * OS_WIDTH];
                uint32_t row = y * 80u;
                for (uint32_t byte_x = 0; byte_x < 80; ++byte_x) {
                    uint8_t packed = 0;
                    for (uint32_t bit = 0; bit < 8; ++bit) {
                        uint32_t x = byte_x * 8u + bit;
                        uint16_t rgb = backbuffer_rgb565[(size_t)present_y_map[y] * OS_WIDTH + present_x_map[x]];
                        uint8_t color = vga_rgb565_to_ega[rgb];
                        if ((color & (1u << plane)) != 0) {
                            packed |= (uint8_t)(0x80u >> bit);
                        }
                    }
                    vram[row + byte_x] = packed;
                }
            }
        }
        return;
    }

    if (vga_legacy_mode == VGA_LEGACY_MODE_320X240X256) {
        for (uint8_t plane = 0; plane < 4; ++plane) {
            vga_set_map_mask((uint8_t)(1u << plane));
            for (uint32_t y = 0; y < 240; ++y) {
                const uint8_t *source = &backbuffer[(size_t)present_y_map[y] * OS_WIDTH];
                uint32_t row = y * 80u;
                for (uint32_t byte_x = 0; byte_x < 80; ++byte_x) {
                    uint32_t x = byte_x * 4u + plane;
                    vram[row + byte_x] = source[present_x_map[x]];
                }
            }
        }
    }
}

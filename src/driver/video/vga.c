// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: vga.c, legacy standard VGA (IBM VGA) graphics driver.

#define VGA_AC_INDEX        0x3C0
#define VGA_MISC_WRITE      0x3C2
#define VGA_MISC_READ       0x3CC
#define VGA_SEQ_INDEX       0x3C4
#define VGA_SEQ_DATA        0x3C5
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA        0x3C9
#define VGA_GC_INDEX        0x3CE
#define VGA_GC_DATA         0x3CF
#define VGA_CRTC_INDEX      0x3D4
#define VGA_CRTC_DATA       0x3D5
#define VGA_INSTAT_READ     0x3DA

#define VGA_FB_ADDRESS 0xA0000u
#define VGA_FONT_BYTES 8192

typedef struct {
    uint8_t misc;
    uint8_t seq[5];
    uint8_t crtc[25];
    uint8_t gc[9];
    uint8_t ac[5];
} VgaModeRegisters;

static const Color vga_ega16_colors[16] = {
    {0, 0, 0},       {0, 0, 170},     {0, 170, 0},    {0, 170, 170},
    {170, 0, 0},     {170, 0, 170},   {170, 85, 0},   {170, 170, 170},
    {85, 85, 85},    {85, 85, 255},   {85, 255, 85},  {85, 255, 255},
    {255, 85, 85},   {255, 85, 255},  {255, 255, 85}, {255, 255, 255}
};

static const VgaModeRegisters vga_mode_text3 = {
    0x67,
    {0x03, 0x00, 0x03, 0x00, 0x03},
    {0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F, 0x20, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x0F, 0xFF},
    {0x0C, 0x00, 0x0F, 0x08, 0x00}
};

static const VgaModeRegisters vga_mode_12h = {
    0xE3,
    {0x03, 0x01, 0x0F, 0x00, 0x06},
    {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 0x00, 0x40, 0x20, 0x00,
     0x00, 0x00, 0x00, 0x00, 0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF},
    {0x01, 0x00, 0x0F, 0x00, 0x00}
};

static const VgaModeRegisters vga_mode_13h = {
    0x63,
    {0x03, 0x01, 0x0F, 0x00, 0x0E},
    {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x20, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF},
    {0x41, 0x00, 0x0F, 0x00, 0x00}
};

static const VgaModeRegisters vga_mode_x = {
    0xE3,
    {0x03, 0x01, 0x0F, 0x00, 0x06},
    {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0D, 0x3E, 0x00, 0x41, 0x20, 0x00,
     0x00, 0x00, 0x00, 0x00, 0xEA, 0xAC, 0xDF, 0x28, 0x00, 0xE7, 0x06, 0xE3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF},
    {0x41, 0x00, 0x0F, 0x00, 0x00}
};

static uint8_t vga_font_backup[VGA_FONT_BYTES];
static bool vga_graphics_active = false;
static uint8_t palette_index_to_ega[256];
static bool palette_ega_map_ready = false;

typedef struct {
    uint8_t misc;
    uint8_t seq[5];
    uint8_t crtc[25];
    uint8_t gc[9];
    uint8_t ac[21];
    uint8_t dac[768];
} VgaSavedState;

static VgaSavedState vga_saved_state;
static bool vga_state_saved = false;
static bool vga_native_text_active = false;

static void update_present_maps(void);

static void vga_build_ega_map(void) {
    if (palette_ega_map_ready) {
        return;
    }
    for (int i = 0; i < 256; ++i) {
        uint32_t best_distance = 0xFFFFFFFFu;
        uint8_t best_index = 0;
        for (int j = 0; j < 16; ++j) {
            int dr = (int)vga_ega16_colors[j].r - (int)palette[i].r;
            int dg = (int)vga_ega16_colors[j].g - (int)palette[i].g;
            int db = (int)vga_ega16_colors[j].b - (int)palette[i].b;
            uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
            if (distance < best_distance) {
                best_distance = distance;
                best_index = (uint8_t)j;
            }
        }
        palette_index_to_ega[i] = best_index;
    }
    palette_ega_map_ready = true;
}

static void vga_write_seq(uint8_t index, uint8_t value) {
    outb(VGA_SEQ_INDEX, index);
    outb(VGA_SEQ_DATA, value);
}

static void vga_write_gc(uint8_t index, uint8_t value) {
    outb(VGA_GC_INDEX, index);
    outb(VGA_GC_DATA, value);
}

static void vga_write_crtc(uint8_t index, uint8_t value) {
    outb(VGA_CRTC_INDEX, index);
    outb(VGA_CRTC_DATA, value);
}

static void vga_write_ac(uint8_t index, uint8_t value) {
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, index);
    outb(VGA_AC_INDEX, value);
}

static uint8_t vga_read_seq(uint8_t index) {
    outb(VGA_SEQ_INDEX, index);
    return inb(VGA_SEQ_DATA);
}

static uint8_t vga_read_crtc(uint8_t index) {
    outb(VGA_CRTC_INDEX, index);
    return inb(VGA_CRTC_DATA);
}

static uint8_t vga_read_gc(uint8_t index) {
    outb(VGA_GC_INDEX, index);
    return inb(VGA_GC_DATA);
}

static uint8_t vga_read_ac(uint8_t index) {
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, index);
    return inb(0x3C1);
}

static void vga_save_state(void) {
    int i;

    vga_saved_state.misc = inb(VGA_MISC_READ);
    for (i = 0; i < 5; ++i) {
        vga_saved_state.seq[i] = vga_read_seq((uint8_t)i);
    }
    for (i = 0; i < 25; ++i) {
        vga_saved_state.crtc[i] = vga_read_crtc((uint8_t)i);
    }
    for (i = 0; i < 9; ++i) {
        vga_saved_state.gc[i] = vga_read_gc((uint8_t)i);
    }
    for (i = 0; i < 21; ++i) {
        vga_saved_state.ac[i] = vga_read_ac((uint8_t)i);
    }
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);

    outb(0x3C7, 0);
    for (i = 0; i < 768; ++i) {
        vga_saved_state.dac[i] = inb(VGA_DAC_DATA);
    }

    vga_state_saved = true;
}

static void vga_restore_state(void) {
    int i;

    if (!vga_state_saved) {
        return;
    }

    vga_write_seq(0x00, 0x01);
    outb(VGA_MISC_WRITE, vga_saved_state.misc);

    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, (uint8_t)(vga_saved_state.crtc[0x11] & 0x7Fu));

    for (i = 0; i < 25; ++i) {
        if (i == 0x11) {
            continue;
        }
        vga_write_crtc((uint8_t)i, vga_saved_state.crtc[i]);
    }
    vga_write_crtc(0x11, vga_saved_state.crtc[0x11]);

    for (i = 1; i < 5; ++i) {
        vga_write_seq((uint8_t)i, vga_saved_state.seq[i]);
    }
    vga_write_seq(0x00, vga_saved_state.seq[0]);

    for (i = 0; i < 9; ++i) {
        vga_write_gc((uint8_t)i, vga_saved_state.gc[i]);
    }

    inb(VGA_INSTAT_READ);
    for (i = 0; i < 21; ++i) {
        outb(VGA_AC_INDEX, (uint8_t)i);
        outb(VGA_AC_INDEX, vga_saved_state.ac[i]);
    }
    outb(VGA_AC_INDEX, 0x20);

    outb(VGA_DAC_WRITE_INDEX, 0);
    for (i = 0; i < 768; ++i) {
        outb(VGA_DAC_DATA, vga_saved_state.dac[i]);
    }
}

static void vga_load_mode_registers(const VgaModeRegisters *regs) {
    uint8_t i;

    vga_write_seq(0x00, 0x01);
    outb(VGA_MISC_WRITE, regs->misc);

    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, (uint8_t)(inb(VGA_CRTC_DATA) & 0x7Fu));

    for (i = 1; i < 5; ++i) {
        vga_write_seq(i, regs->seq[i]);
    }

    for (i = 0; i < 25; ++i) {
        uint8_t value = regs->crtc[i];
        if (i == 0x11) {
            value &= 0x7Fu;
        }
        vga_write_crtc(i, value);
    }

    for (i = 0; i < 9; ++i) {
        vga_write_gc(i, regs->gc[i]);
    }

    for (i = 0; i < 16; ++i) {
        vga_write_ac(i, i);
    }
    for (i = 0; i < 5; ++i) {
        vga_write_ac((uint8_t)(0x10 + i), regs->ac[i]);
    }

    vga_write_seq(0x00, regs->seq[0]);
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
}

static uint8_t vga_reverse_byte(uint8_t value) {
    uint8_t result = 0;
    for (int b = 0; b < 8; ++b) {
        result = (uint8_t)((result << 1) | (value & 1u));
        value >>= 1;
    }
    return result;
}

static void vga_restore_text_font(void) {
    uint8_t *vram = (uint8_t *)(uintptr_t)VGA_FB_ADDRESS;
    int ch;
    int row;

    for (ch = 0; ch < 256; ++ch) {
        uint8_t *cell = vga_font_backup + ch * 16;
        int glyph = (ch >= 32 && ch <= 127) ? ch - 32 : 0;

        for (row = 0; row < 8; ++row) {
            uint8_t bits = vga_reverse_byte(font8x8_basic[glyph][row]);
            cell[row * 2] = bits;
            cell[row * 2 + 1] = bits;
        }
    }

    vga_write_seq(0x02, 0x04);
    vga_write_seq(0x04, 0x06);
    vga_write_gc(0x04, 0x02);
    vga_write_gc(0x05, 0x00);
    vga_write_gc(0x06, 0x04);
    for (ch = 0; ch < 256; ++ch) {
        memcpy_local(vram + ch * 32, vga_font_backup + ch * 16, 16);
    }
    vga_write_seq(0x02, 0x03);
    vga_write_seq(0x04, 0x03);
    vga_write_gc(0x04, 0x00);
    vga_write_gc(0x05, 0x10);
    vga_write_gc(0x06, 0x0E);
}

static void vga_program_ega_dac(void) {
    int i;

    outb(VGA_DAC_WRITE_INDEX, 0);
    for (i = 0; i < 16; ++i) {
        outb(VGA_DAC_DATA, (uint8_t)(vga_ega16_colors[i].r / 4));
        outb(VGA_DAC_DATA, (uint8_t)(vga_ega16_colors[i].g / 4));
        outb(VGA_DAC_DATA, (uint8_t)(vga_ega16_colors[i].b / 4));
    }
}

static void vga_enter_text_mode(void) {
    vga_restore_text_font();
    vga_load_mode_registers(&vga_mode_text3);
    vga_graphics_active = false;
    vga_native_text_active = true;
    vga_program_ega_dac();
}

static void vga_save_and_enter_text_mode(void) {
    if (vga_graphics_active) {
        vga_save_state();
    }
    if (vga_state_saved) {
        vga_restore_state();
        vga_restore_text_font();
        vga_graphics_active = false;
        vga_native_text_active = true;
        return;
    }
    vga_enter_text_mode();
}

static bool vga_native_text_mode_active(void) {
    return vga_native_text_active;
}

static bool vga_probe_present(void) {
    uint8_t misc;
    uint8_t saved;
    uint8_t test;
    uint8_t readback;

    misc = inb(VGA_MISC_READ);
    if ((misc & 0x01u) == 0) {
        return false;
    }

    outb(VGA_CRTC_INDEX, 0x0A);
    saved = inb(VGA_CRTC_DATA);
    test = (uint8_t)(saved ^ 0x08u);
    outb(VGA_CRTC_DATA, test);
    readback = inb(VGA_CRTC_DATA);
    outb(VGA_CRTC_DATA, saved);
    return readback == test;
}

static bool vga_set_graphics_mode(uint16_t width, uint16_t height, uint16_t bpp) {
    const VgaModeRegisters *regs = NULL;
    uint32_t clear_bytes = 0;

    if (bpp == 8 && (width == 320 || width == 240) && (height == 200 || height == 240)) {
        regs = &vga_mode_13h;
        clear_bytes = 64000;
    } else if (width == 640 && height == 480 && bpp == 4) {
        regs = &vga_mode_12h;
        clear_bytes = 38400;
    }

    if (regs == NULL) {
        return false;
    }

    vga_load_mode_registers(regs);
    vga_write_seq(0x02, 0x0F);
    memset_local((uint8_t *)(uintptr_t)VGA_FB_ADDRESS, 0, clear_bytes);
    if (regs == &vga_mode_13h) {
        fb.width = 320;
        fb.height = 200;
        fb.pitch = 320;
    } else {
        fb.width = 640;
        fb.height = 480;
        fb.pitch = 80;
    }
    fb.address = (uint8_t *)(uintptr_t)VGA_FB_ADDRESS;
    fb.bpp = (uint8_t)bpp;
    fb.type = MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED;
    if (bpp == 4) {
        vga_build_ega_map();
        vga_program_ega_dac();
    } else if (bpp == 8) {
        program_vga_palette();
    }
    update_present_maps();
    vga_graphics_active = true;
    vga_native_text_active = false;
    return true;
}

static bool init_vga_backend(void) {
    if (!vga_probe_present()) {
        return false;
    }

    outw(0x01CE, 0x04);
    outw(0x01CF, 0x00);

    vga_enter_text_mode();
    fb.address = (uint8_t *)(uintptr_t)VGA_FB_ADDRESS;
    fb.width = 320;
    fb.height = 200;
    fb.pitch = 320;
    fb.bpp = 8;
    fb.type = MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED;
    video_backend = VIDEO_BACKEND_VGA;
    return true;
}

static void present_vga_linear_256(void) {
    uint8_t *vram = (uint8_t *)(uintptr_t)VGA_FB_ADDRESS;
    uint32_t y;

    for (y = 0; y < fb.height; ++y) {
        uint16_t sy = present_y_map[y];
        const uint8_t *src = &backbuffer[sy * OS_WIDTH];
        uint8_t *dest = vram + (size_t)y * fb.pitch;
        uint32_t x;

        for (x = 0; x < fb.width; ++x) {
            dest[x] = src[present_x_map[x]];
        }
    }
}

static void present_vga_planar_16(void) {
    uint8_t *vram = (uint8_t *)(uintptr_t)VGA_FB_ADDRESS;
    uint32_t plane;

    for (plane = 0; plane < 4; ++plane) {
        uint32_t y;

        vga_write_seq(0x02, (uint8_t)(1u << plane));
        for (y = 0; y < fb.height; ++y) {
            uint16_t sy = present_y_map[y];
            const uint8_t *src = &backbuffer[sy * OS_WIDTH];
            uint8_t *dest = vram + (size_t)y * fb.pitch;
            uint32_t byte_col;

            for (byte_col = 0; byte_col < fb.pitch; ++byte_col) {
                uint32_t x0 = byte_col * 8;
                uint8_t bits = 0;
                uint32_t b;

                for (b = 0; b < 8; ++b) {
                    uint8_t ega = palette_index_to_ega[src[present_x_map[x0 + b]]];
                    bits |= (uint8_t)(((ega >> plane) & 1u) << (7 - b));
                }
                dest[byte_col] = bits;
            }
        }
    }
}

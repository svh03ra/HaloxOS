// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: graphics.c, framebuffer graphics rendering driver.

// This repository is licensed under the GNU General Public License.

/* Runtime pixel-drawing hot path: cached palette-index -> RGB565 table
 * (defined near palette_rgb565 at the bottom of this file). */
static uint16_t pixel_rgb565_fast(uint8_t color);

/* Inverse direction (RGB565 -> palette index), used by the quantizing
 * present path and the raw-RGB565 restore paths; defined with the v2
 * asset index LUT further down. */
static uint8_t plane_index_for_rgb565(uint16_t rgb);

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
    /* exact BSOD blue #0909BF, snapped to slot 216 on indexed
     * framebuffers and to the nearest channel value on direct-color */
    color_crash_blue = nearest_color(9, 9, 191);
    color_red = nearest_color(180, 40, 40);
    color_yellow = nearest_color(230, 210, 40);
    color_orange = nearest_color(230, 130, 40);
    color_pink = nearest_color(220, 140, 180);
    color_indigo = nearest_color(75, 0, 130);
    color_violet = nearest_color(160, 32, 240);
    color_desktop_icon = nearest_color(245, 245, 245);
    paint_color = color_black;
}

static bool framebuffer_text_mode_active(void) {
    if (vga_native_text_mode_active()) {
        return false;
    }
    if (!boot_text_mode || fb.address == NULL) {
        return false;
    }
    /*
     * Bootloader framebuffer (real hardware, VBE-only VMs) and QEMU's Bochs
     * VBE (BGA) both draw the boot menu as emulated text into the live
     * linear framebuffer.  BGA joins that list because disabling DISPI to
     * return to the legacy VGA text mode leaves the scanout reading the
     * linear framebuffer on current QEMU builds: the text buffer at 0xB8000
     * held the whole menu while the display stayed black - the boot menu was
     * invisible on QEMU only.  See enter_boot_text_mode() in video_modes.c.
     */
    if (video_backend == VIDEO_BACKEND_MULTIBOOT) {
        return true;
    }
    return video_backend == VIDEO_BACKEND_BGA &&
           (bga_read(VBE_DISPI_INDEX_ENABLE) & VBE_DISPI_ENABLED) != 0;
}

static uint8_t text_attr_foreground(uint8_t attr) {
    switch (attr & 0x0Fu) {
        case 0x0: return color_black;
        case 0x1: return color_blue_dark;
        case 0x2: return color_green;
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
        case 0x2: return color_green;
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

/*
 * Bytes one output pixel occupies in the framebuffer.
 *
 * This exists because `fb.bpp / 8` is NOT it.  The packed 15-bit mode a lot
 * of VGA BIOSes prefer for direct colour (VBE mode 0x110, 5-5-5) reports
 * bpp 15, and 15/8 is 1 there - while the pixel really is two bytes wide,
 * exactly like a 16bpp one.  Every place that turned a column count into a
 * byte offset with that expression was therefore half a row to the left on
 * such a card: the letterbox margins, the partial scanouts (moving pointer,
 * clock, a dragged window) and the band fill.  That is the sheared, pasted
 * looking desktop that appears on real hardware whose mode comes back as
 * "640x480x15" while a QEMU or VMware mode is 16bpp and hides it.
 */
static size_t output_bytes_per_pixel(void) {
    return ((size_t)fb.bpp + 7u) / 8u;
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
 * Only the *final* 64K output LUT (present_pixel_lut below) is kept in
 * memory. Its two ingredients already exist:
 *   - 256-colour mode: rgb565_index_lut, the closed-form nearest palette
 *     index (verified to match brute-force nearest_color for all inputs),
 *   - 16-colour mode: a 16-entry scan of the EGA palette, run only while
 *     the LUT is being (re)built, i.e. once per colour-mode change.
 * The previous implementation cached that intermediate in a second 64 KB
 * table (quant_lut) that nothing else ever read.
 */
static uint8_t nearest_ega16_index(Color c) {
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

    return best_index;
}

static Color present_color_for(uint16_t rgb565) {
    if (settings_applied.palette_mode == 2) {
        return rgb565_to_color(rgb565);
    }
    if (settings_applied.palette_mode == 1) {
        return vga_ega16_colors[nearest_ega16_index(rgb565_to_color(rgb565))];
    }
    return palette[plane_index_for_rgb565(rgb565)];
}

/*
 * Full-chain output LUT: maps a backbuffer RGB565 value straight to the
 * packed framebuffer pixel, fusing rgb565_to_color + palette/EGA
 * quantization + format packing. Valid only while the output format and
 * palette mode stay unchanged; both are rare runtime events, so the
 * small rebuild cost is amortized over millions of pixels. This removes
 * ~10 arithmetic ops + a 16-entry nearest-color search per pixel from
 * every scanout of the 24/32bpp present paths.
 *
 * Storage is the render_scratch union declared in vga.c: present LUT
 * (24/32bpp present paths) and the VGA 4bpp plane staging buffers are
 * never live in the same video mode, so they share 256 KB of BSS.
 */
static int8_t present_pixel_lut_mode = -1;
static uint8_t present_pixel_lut_bpp = 0;

static void present_pixel_lut_build(uint8_t mode, uint8_t bpp) {
    for (uint32_t v = 0; v < 65536; ++v) {
        present_pixel_lut[v] = pack_framebuffer_color(present_color_for((uint16_t)v));
    }
    present_pixel_lut_mode = (int8_t)mode;
    present_pixel_lut_bpp = bpp;
}

/*
 * Fetch the ready-to-store output LUT for this frame.
 *
 * The old per-pixel helper re-tested "is the LUT current?" (two loads
 * plus two compares) for every one of the 307200 pixels of a 640x480
 * scanout. The rebuild check is a per-frame event, so it is hoisted here
 * and the inner loops only do a table lookup + store.
 */
static const uint32_t *present_lut_ready(void) {
    if (present_pixel_lut_mode != (int8_t)settings_applied.palette_mode ||
        present_pixel_lut_bpp != fb.bpp) {
        present_pixel_lut_build(settings_applied.palette_mode, fb.bpp);
    }
    return present_pixel_lut;
}

/*
 * 16-bit RGB565 stores, four pixels per iteration.
 *
 * The LUT already returns the packed framebuffer pixel, so two pixels
 * are merged into one 32-bit store and four into two. That removes most
 * of the per-pixel loop overhead (address scaling, compare, branch) from
 * the 15/16bpp scanout, which is where every slow machine spends its frame
 * time.
 */
static void present_row_16(const uint16_t *src, uint16_t *dest, uint32_t count,
                           const uint32_t *lut) {
    uint32_t x = 0;

    for (; x + 4u <= count; x += 4u) {
        uint32_t pair0 = (lut[src[x]] & 0xFFFFu) | (lut[src[x + 1u]] << 16);
        uint32_t pair1 = (lut[src[x + 2u]] & 0xFFFFu) | (lut[src[x + 3u]] << 16);
        uint64_t quad = (uint64_t)pair0 | ((uint64_t)pair1 << 32);
        *(uint64_t *)(void *)(dest + x) = quad;
    }
    for (; x + 2u <= count; x += 2u) {
        uint32_t pair = (lut[src[x]] & 0xFFFFu) | (lut[src[x + 1u]] << 16);
        *(uint32_t *)(void *)(dest + x) = pair;
    }
    if (x < count) {
        dest[x] = (uint16_t)lut[src[x]];
    }
}

/* 24-bit rows: four pixels (12 bytes) leave as three 32-bit stores. */
static void present_row_24(const uint16_t *src, uint8_t *dest, uint32_t count,
                           const uint32_t *lut) {
    uint32_t x = 0;

    for (; x + 4u <= count; x += 4u) {
        uint32_t p0 = lut[src[x]];
        uint32_t p1 = lut[src[x + 1u]];
        uint32_t p2 = lut[src[x + 2u]];
        uint32_t p3 = lut[src[x + 3u]];
        *(uint32_t *)(void *)(dest + 0) = (p0 & 0x00FFFFFFu) | (p1 << 24);
        *(uint32_t *)(void *)(dest + 4) = ((p1 >> 8) & 0x00FFFFu) | (p2 << 16);
        *(uint32_t *)(void *)(dest + 8) = ((p2 >> 16) & 0x0000FFu) | (p3 << 8);
        dest += 12;
    }
    for (; x < count; ++x) {
        uint32_t packed = lut[src[x]];
        dest[0] = (uint8_t)(packed & 0xFFu);
        dest[1] = (uint8_t)((packed >> 8) & 0xFFu);
        dest[2] = (uint8_t)((packed >> 16) & 0xFFu);
        dest += 3;
    }
}

/* 32-bit rows: two pixels per 64-bit store. */
static void present_row_32(const uint16_t *src, uint32_t *dest, uint32_t count,
                           const uint32_t *lut) {
    uint32_t x = 0;

    for (; x + 2u <= count; x += 2u) {
        uint64_t pair = (uint64_t)lut[src[x]] | ((uint64_t)lut[src[x + 1u]] << 32);
        *(uint64_t *)(void *)(dest + x) = pair;
    }
    if (x < count) {
        dest[x] = lut[src[x]];
    }
}

/* Adopt the bootloader-programmed linear framebuffer (the VBE mode
 * GRUB set before the kernel started). Shared by the direct multiboot
 * hand-off and the VMware SVGA fallback below. */
static bool adopt_multiboot_framebuffer(uint32_t magic, const MultibootInfo *mbi) {
    if (magic != 0x2BADB002 || mbi == NULL) {
        return false;
    }
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

    {
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
            fb.width >= OS_BASE_WIDTH &&
            fb.height >= OS_BASE_HEIGHT &&
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
            /*
             * This is the machine where the mode we just adopted is the only
             * one the OS would ever see, so it is the one whose resolution
             * list needs the video BIOS.
             *
             * Hand the block to vbe.c and NOTHING MORE.  The firmware is not
             * called from here any more: this runs before the kernel has an
             * interrupt table, so a fault inside a real-mode BIOS call had
             * nowhere to go, and the machine reset with the speaker beeping
             * and nothing on screen.  The question is asked later, from the
             * running desktop, and only when the resolution list is opened.
             */
            vbe_note_boot_info(mbi);
            return true;
        }
    }
    return false;
}

/* After a failed SVGA init the device may sit with its engine disabled
 * (the fifo setup wrote ENABLE=0). The BIOS/GRUB mode itself was never
 * reprogrammed, so re-enabling restores exactly what the bootloader
 * left on screen - the fallback framebuffer writes then display. */
static void vmware_restore_boot_mode(void) {
    if (vmware_svga.fifo_ready) {
        vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_ENABLE);
    }
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
        if (current_width >= OS_BASE_WIDTH && current_height >= OS_BASE_HEIGHT &&
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
            /* Book the booted mode as the fallback the mode setter can put
             * back if a requested one turns out unusable. */
            remember_usable_mode();
            return true;
        }
    }

    /*
     * VMware SVGA is tried BEFORE the bootloader framebuffer, and that order
     * is the whole point of the backend: the bootloader hands over a single
     * fixed mode, while SVGA can program any mode the host offers (that is
     * how the Resolution arrows work in Settings). With the old order the
     * adopt below always succeeded first - GRUB sets gfxpayload on every
     * boot - so a VMware guest silently ran on the fixed boot framebuffer
     * and the Settings tab's resolution arrows were permanently greyed out.
     *
     * The fallback is unchanged and sits right below: if SVGA cannot give a
     * usable mode (host bpp limits, fifo problems) the boot framebuffer is
     * adopted exactly as before.
     */
    if (init_vmware_svga_backend()) {
        return true;
    }

    /*
     * VMware SVGA could not provide a usable mode (host bpp limits,
     * fifo problems). Fall back to the bootloader framebuffer instead
     * of leaving the machine without a working backend - the graphical
     * boot menu / login then renders exactly like on any other card.
     */
    if (vmware_svga.device_present) {
        serial_trace("WARNING", "VMware SVGA: usable mode not available - using bootloader framebuffer");
    }
    vmware_restore_boot_mode();
    if (adopt_multiboot_framebuffer(magic, mbi)) {
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
            fb.width = OS_BASE_WIDTH;
            fb.height = OS_BASE_HEIGHT;
            fb.pitch = OS_BASE_WIDTH;
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

/*
 * Blank the letterbox bands around the content box.
 *
 * Every scanout writes only the content box, so the bands around it keep
 * whatever the last bigger frame left there until they are painted. The
 * work only has to happen when the geometry changes, which is what
 * present_margins_dirty tracks (see its note in kernel.c): raised on every
 * mode or canvas change, lowered here. A full-screen content box has no
 * bands, so lower it and do nothing.
 */
static void present_fill_margins(void) {
    uint8_t index_black;

    if (!present_margins_dirty) {
        return;
    }
    present_margins_dirty = false;

    if (present_content_width == fb.width && present_content_height == fb.height) {
        return;
    }

    if (fb.bpp == 8) {
        index_black = color_black;
        for (uint32_t y = 0; y < fb.height; ++y) {
            uint8_t *row = fb.address + (size_t)y * fb.pitch;
            if (y < present_offset_y || y >= present_offset_y + present_content_height) {
                memset_local(row, index_black, fb.pitch);
            } else {
                memset_local(row, index_black, present_offset_x);
                memset_local(row + present_offset_x + present_content_width, index_black,
                             fb.width - present_offset_x - present_content_width);
            }
        }
        return;
    }

    {
        for (uint32_t y = 0; y < fb.height; ++y) {
            uint32_t row_off = (size_t)y * fb.pitch;
            if (y < present_offset_y || y >= present_offset_y + present_content_height) {
                memset_local(fb.address + row_off, 0, fb.pitch);
            } else {
                uint32_t left_bytes = present_offset_x * (uint32_t)output_bytes_per_pixel();
                uint32_t right_bytes = (fb.width - present_offset_x - present_content_width) * (uint32_t)output_bytes_per_pixel();
                memset_local(fb.address + row_off, 0, left_bytes);
                memset_local(fb.address + row_off + left_bytes + present_content_width * (uint32_t)output_bytes_per_pixel(), 0, right_bytes);
            }
        }
    }
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

    /* 8bpp output: the indexed plane IS the image, so the scanout is a
     * plain copy. Contiguous rows (pitch == width) collapse into ONE
     * memcpy for the whole screen instead of 480 row copies. */
    if (fb.bpp == 8 && fb.width == OS_WIDTH && fb.height == OS_HEIGHT) {
        if (fb.pitch == OS_WIDTH) {
            memcpy_local(fb.address, backbuffer, (size_t)OS_WIDTH * OS_HEIGHT);
        } else {
            for (int y = 0; y < OS_HEIGHT; ++y) {
                uint8_t *dest = fb.address + (size_t)y * fb.pitch;
                memcpy_local(dest, &backbuffer[y * OS_WIDTH], OS_WIDTH);
            }
        }
        if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
            vmware_update_screen();
        }
        return;
    }

    /* Common case: 1:1 mapping, no scaling. Every row is one bulk copy
     * out of the shadow buffers; no per-pixel sampling maps at all. The
     * output LUT is resolved once here, never inside the pixel loops. */
    if (present_content_width == OS_WIDTH && present_content_height == OS_HEIGHT &&
        fb.width >= OS_WIDTH && fb.height >= OS_HEIGHT) {
        const uint32_t *lut = (fb.bpp > 8) ? present_lut_ready() : NULL;
        /* present_offset_x is a COLUMN, not a byte count, so it has to be
         * scaled by the output pixel size before it is added to a byte
         * offset. Adding it raw put a letterboxed desktop at half its
         * margin on every direct-colour output: a centred 800x600 canvas on
         * a 1024-wide mode landed 56px left of centre instead of 112. */
        const size_t left_bytes = (size_t)present_offset_x * output_bytes_per_pixel();
        const bool direct565 = (fb.bpp == 16 && fb.red_position == 11 && fb.green_position == 5 &&
                                fb.blue_position == 0 && fb.red_mask_size == 5 &&
                                fb.green_mask_size == 6 && fb.blue_mask_size == 5 &&
                                settings_applied.palette_mode == 2);

        /*
         * The desktop fills the framebuffer exactly - no letterbox in either
         * direction, and each row's bytes are followed immediately by the next
         * row's. The scanout is then ONE bulk pass over the whole image
         * instead of 480 row-sized ones: the per-row form pays a call, a
         * rep-prefix setup and a destination-address computation per row, so
         * a 640x480 output pays that 480 times to move the same 614,400
         * bytes. Nothing about the image changes; only the number of times
         * the inner loops are entered.
         */
        if (present_offset_x == 0 && present_offset_y == 0 &&
            fb.width == OS_WIDTH && fb.height == OS_HEIGHT &&
            fb.pitch == (size_t)OS_WIDTH * output_bytes_per_pixel()) {
            const uint32_t count = (uint32_t)OS_WIDTH * OS_HEIGHT;

            if (fb.bpp == 8) {
                memcpy_local(fb.address, backbuffer, count);
            } else if (fb.bpp == 15 || fb.bpp == 16) {
                if (direct565) {
                    memcpy_local(fb.address, backbuffer_rgb565, (size_t)count * 2u);
                } else {
                    present_row_16(backbuffer_rgb565, (uint16_t *)(void *)fb.address, count, lut);
                }
            } else if (fb.bpp == 24) {
                present_row_24(backbuffer_rgb565, fb.address, count, lut);
            } else {
                present_row_32(backbuffer_rgb565, (uint32_t *)(void *)fb.address, count, lut);
            }

            if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
                vmware_update_screen();
            }
            return;
        }

        for (uint32_t y = 0; y < OS_HEIGHT; ++y) {
            const uint8_t *src = &backbuffer[y * OS_WIDTH];
            const uint16_t *src_rgb = &backbuffer_rgb565[y * OS_WIDTH];
            size_t row_off = (size_t)(y + present_offset_y) * fb.pitch + left_bytes;

            if (fb.bpp == 8) {
                memcpy_local(fb.address + row_off, src, OS_WIDTH);
            } else if (fb.bpp == 15 || fb.bpp == 16) {
                if (direct565) {
                    /* True-color and the framebuffer layout IS RGB565:
                     * straight bulk copy, zero per-pixel math. */
                    memcpy_local(fb.address + row_off, src_rgb, OS_WIDTH * 2u);
                } else {
                    present_row_16(src_rgb, (uint16_t *)(void *)(fb.address + row_off),
                                   OS_WIDTH, lut);
                }
            } else if (fb.bpp == 24) {
                present_row_24(src_rgb, fb.address + row_off, OS_WIDTH, lut);
            } else {
                present_row_32(src_rgb, (uint32_t *)(void *)(fb.address + row_off),
                               OS_WIDTH, lut);
            }
        }
        /* Black letterbox bands (an output larger than the desktop). Called
         * on every 1:1 scanout, not only for a geometry that has bands: the
         * flag it reads is raised by every mode/canvas change, and a bigger
         * content box - which has no bands of its own - still has to retire
         * the bands it painted over, which is exactly what the older
         * geometry-keyed cache never did. */
        present_fill_margins();
        if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
            vmware_update_screen();
        }
        return;
    }

    {
        const uint32_t *lut = (fb.bpp > 8) ? present_lut_ready() : NULL;

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
                uint16_t *dest = (uint16_t *)(void *)(fb.address + (size_t)y * fb.pitch);
                uint16_t out_black = (uint16_t)lut[0];
                for (uint32_t x = 0; x < fb.width; ++x) {
                    bool inside = inside_y && x >= present_offset_x && x < present_offset_x + present_content_width;
                    if (!inside) {
                        dest[x] = out_black;
                        continue;
                    }
                    dest[x] = (uint16_t)lut[backbuffer_rgb565[(size_t)sy * OS_WIDTH + present_x_map[x]]];
                }
                continue;
            }

            if (fb.bpp == 24) {
                uint8_t *dest = fb.address + (size_t)y * fb.pitch;
                uint32_t out_black = lut[0];
                for (uint32_t x = 0; x < fb.width; ++x) {
                    /* Outside the content box this branch used to sample
                     * source column 0 (the maps are 0 out there) and paint
                     * a strip of the desktop into the letterbox band. */
                    bool inside = inside_y && x >= present_offset_x &&
                                  x < present_offset_x + present_content_width;
                    uint32_t packed = inside
                        ? lut[backbuffer_rgb565[(size_t)sy * OS_WIDTH + present_x_map[x]]]
                        : out_black;
                    dest[x * 3 + 0] = (uint8_t)(packed & 0xFFu);
                    dest[x * 3 + 1] = (uint8_t)((packed >> 8) & 0xFFu);
                    dest[x * 3 + 2] = (uint8_t)((packed >> 16) & 0xFFu);
                }
                continue;
            }

            {
                uint32_t *dest = (uint32_t *)(void *)(fb.address + (size_t)y * fb.pitch);
                for (uint32_t x = 0; x < fb.width; ++x) {
                    dest[x] = lut[backbuffer_rgb565[(size_t)sy * OS_WIDTH + present_x_map[x]]];
                }
            }
        }
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        vmware_update_screen();
    }
}

/*
 * Whether a partial scan-out can be honoured at all.
 *
 * A scaled output, the VGA planar backend (which owns the whole screen) or a
 * framebuffer smaller than the canvas all force the full-frame scan-out
 * instead. VMware SVGA is supported now that vmware_update_rect() can tell
 * the host which rectangle changed: the guest writes only those pixels and
 * the host refresh costs the same rectangle, not the whole 640x480.
 */
static bool present_partial_supported(void) {
    return present_content_width == OS_WIDTH && present_content_height == OS_HEIGHT &&
           fb.width >= OS_WIDTH && fb.height >= OS_HEIGHT &&
           video_backend != VIDEO_BACKEND_VGA;
}

/* Is the output a true RGB565 surface that can take the RGB565 shadow
 * plane verbatim? Mirrors the direct565 test in present(); kept in one
 * place so a partial scan-out cannot disagree with the full one. */
static bool present_direct565(void) {
    return fb.bpp == 16 && fb.red_position == 11 && fb.green_position == 5 &&
           fb.blue_position == 0 && fb.red_mask_size == 5 &&
           fb.green_mask_size == 6 && fb.blue_mask_size == 5 &&
           settings_applied.palette_mode == 2;
}

/*
 * Partial scanout: present only one rectangle of the desktop.
 *
 * This is what makes a moving pointer cheap. A full frame repaints the
 * wallpaper, the icons, the taskbar and then scans out 640x480 pixels;
 * a pointer-only frame touches two small rectangles (~600 pixels total)
 * instead. Whenever the output is scaled, the backend owns the screen
 * (VGA / VMware SVGA) or the geometry is not 1:1, it silently falls back
 * to a full present, so callers never have to think about it.
 */
static void present_rect(int x, int y, int w, int h) {
    if (fb.address == NULL) {
        return;
    }

    if (!present_partial_supported()) {
        present();
        return;
    }

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > OS_WIDTH) { w = OS_WIDTH - x; }
    if (y + h > OS_HEIGHT) { h = OS_HEIGHT - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    /* Same unit rule as present(): the margin is a column count, so it is
     * scaled here too (1 byte per pixel on the indexed output, 2/3/4 on the
     * direct-colour ones). Without the scale a partial scanout - the moving
     * pointer and the clock - painted its rectangle 56px off on a letterboxed
     * 16bpp desktop. */
    if (fb.bpp == 8) {
        for (int row = y; row < y + h; ++row) {
            memcpy_local(fb.address + (size_t)(row + present_offset_y) * fb.pitch +
                             (size_t)present_offset_x + (size_t)x,
                         &backbuffer[(size_t)row * OS_WIDTH + x], (size_t)w);
        }
    } else {
        const uint32_t *lut = present_lut_ready();
        const size_t left_bytes = (size_t)present_offset_x * output_bytes_per_pixel();
        const bool direct565 = (fb.bpp == 16) && present_direct565();

        for (int row = y; row < y + h; ++row) {
            const uint16_t *src = &backbuffer_rgb565[(size_t)row * OS_WIDTH + x];
            uint8_t *dest = fb.address + (size_t)(row + present_offset_y) * fb.pitch +
                            left_bytes + (size_t)x * output_bytes_per_pixel();

            if (fb.bpp == 15 || fb.bpp == 16) {
                if (direct565) {
                    /* True-color RGB565 output: the shadow plane already
                     * holds the exact bytes, so skip the LUT loop. */
                    memcpy_local(dest, src, (size_t)w * 2u);
                } else {
                    present_row_16(src, (uint16_t *)(void *)dest, (uint32_t)w, lut);
                }
            } else if (fb.bpp == 24) {
                present_row_24(src, dest, (uint32_t)w, lut);
            } else {
                present_row_32(src, (uint32_t *)(void *)dest, (uint32_t)w, lut);
            }
        }
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        /* The host only needs to refresh the pixels just written. Screen
         * coordinates: the desktop rectangle plus its letterbox offset. */
        vmware_update_rect(x + present_offset_x, y + present_offset_y, w, h);
    }
}

static void clear_screen(uint8_t color) {
    if (present_need_index_plane()) {
        /* The canvas, not sizeof(backbuffer): the storage is sized for the
         * largest canvas the OS can grow to, and clearing all of it would
         * touch far more memory than the mode actually shows. */
        memset_local(backbuffer, color, (size_t)OS_WIDTH * OS_HEIGHT);
    }
    if (present_need_rgb_plane()) {
        memset16_local(backbuffer_rgb565, pixel_rgb565_fast(color), OS_WIDTH * OS_HEIGHT);
    }
}

/*
 * Plane-aware pixel writers.
 *
 * Every primitive goes through these so it only pays for the shadow
 * plane its output format actually presents (see present_index_plane_live
 * in vga.c). The flags are read once per call site, not per pixel, so the
 * inner loops keep a single predictable branch.
 *
 * pixel_rgb565_fast() resolves an 8-bit palette index to the cached RGB565
 * value (a table lookup once the palette is programmed); the inverse
 * direction only runs on the rare raw-RGB565 restore paths.
 */
static void plane_set_pixel(uint32_t index, uint8_t color) {
    if (present_need_index_plane()) {
        backbuffer[index] = color;
    }
    if (present_need_rgb_plane()) {
        backbuffer_rgb565[index] = pixel_rgb565_fast(color);
    }
}

static void plane_set_pixel_rgb(uint32_t index, uint16_t rgb) {
    if (present_need_rgb_plane()) {
        backbuffer_rgb565[index] = rgb;
    }
    if (present_need_index_plane()) {
        backbuffer[index] = plane_index_for_rgb565(rgb);
    }
}

static uint16_t plane_get_pixel_rgb(int x, int y) {
    if (present_need_rgb_plane()) {
        return backbuffer_rgb565[(uint32_t)y * OS_WIDTH + (uint32_t)x];
    }
    return pixel_rgb565_fast(backbuffer[(uint32_t)y * OS_WIDTH + (uint32_t)x]);
}

/*
 * Window clip: when set, all pixel/rect/image drawing is confined to
 * the client area of the app window being rendered, so a scene like
 * the firecracker demo can never bleed outside its window face onto
 * the desktop. render_app_window enables it around each app draw and
 * disables it afterwards.
 */
static int clip_x0 = 0;
static int clip_y0 = 0;
/* Compile-time defaults, not the live canvas: clip_x1/clip_y1 are set by
 * reset_clip() before every draw, and OS_WIDTH is a runtime value that
 * cannot appear in a static initializer. */
static int clip_x1 = OS_BASE_WIDTH;
static int clip_y1 = OS_BASE_HEIGHT;
static bool clip_enabled = false;

static void set_window_clip(int x, int y, int w, int h) {
    clip_x0 = clampi(x, 0, OS_WIDTH);
    clip_y0 = clampi(y, 0, OS_HEIGHT);
    clip_x1 = clampi(x + w, 0, OS_WIDTH);
    clip_y1 = clampi(y + h, 0, OS_HEIGHT);
    if (clip_x1 < clip_x0) clip_x1 = clip_x0;
    if (clip_y1 < clip_y0) clip_y1 = clip_y0;
    clip_enabled = true;
}

static void clear_window_clip(void) {
    clip_enabled = false;
}

/* Crash-safe variant for the exception handler: app windows set the clip
 * in app_dispatch.c and clear it after drawing, but a crash INSIDE an
 * app render (e.g. DOOM) leaves the clip stuck on that window's client
 * rectangle. Without this reset the BSOD is squeezed into the crashed
 * app's window face instead of covering the full screen. */
static void graphics_reset_window_clip(void) {
    clip_x0 = 0;
    clip_y0 = 0;
    clip_x1 = OS_WIDTH;
    clip_y1 = OS_HEIGHT;
    clip_enabled = false;
}

static bool clip_contains(int x, int y) {
    if (!clip_enabled) {
        return true;
    }
    return x >= clip_x0 && x < clip_x1 && y >= clip_y0 && y < clip_y1;
}

static void draw_pixel(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= OS_WIDTH || y >= OS_HEIGHT) {
        return;
    }
    if (!clip_contains(x, y)) {
        return;
    }
    plane_set_pixel((uint32_t)y * OS_WIDTH + (uint32_t)x, color);
}

static void fill_rect(int x, int y, int w, int h, uint8_t color) {
    int x0 = clampi(x, 0, OS_WIDTH);
    int y0 = clampi(y, 0, OS_HEIGHT);
    int x1 = clampi(x + w, 0, OS_WIDTH);
    int y1 = clampi(y + h, 0, OS_HEIGHT);
    bool want_index = present_need_index_plane();
    bool want_rgb = present_need_rgb_plane();
    uint16_t rgb = want_rgb ? pixel_rgb565_fast(color) : 0;
    int row_len = x1 - x0;

    if (clip_enabled) {
        if (x0 < clip_x0) x0 = clip_x0;
        if (y0 < clip_y0) y0 = clip_y0;
        if (x1 > clip_x1) x1 = clip_x1;
        if (y1 > clip_y1) y1 = clip_y1;
        row_len = x1 - x0;
    }

    if (row_len <= 0 || y1 <= y0) {
        return;
    }

    /* Bulk fill per row: one memset per plane, and only for the plane the
     * output format presents (both on the frame after a mode change). */
    if (row_len == OS_WIDTH) {
        for (int py = y0; py < y1; ++py) {
            if (want_index) {
                memset_local(&backbuffer[py * OS_WIDTH], color, (size_t)row_len);
            }
            if (want_rgb) {
                memset16_local(&backbuffer_rgb565[py * OS_WIDTH], rgb, (size_t)row_len);
            }
        }
        return;
    }

    for (int py = y0; py < y1; ++py) {
        if (want_index) {
            memset_local(&backbuffer[py * OS_WIDTH + x0], color, (size_t)row_len);
        }
        if (want_rgb) {
            memset16_local(&backbuffer_rgb565[py * OS_WIDTH + x0], rgb, (size_t)row_len);
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

    if (!clip_enabled && x >= 0 && y >= 0 && x + 8 <= OS_WIDTH && y + 8 <= OS_HEIGHT) {
        /*
         * Fully on-screen: write rows directly, no per-pixel clipping.
         *
         * The two row bases are computed once and then stepped, instead of
         * being recomputed from `(y + row) * OS_WIDTH + x` for both planes on
         * every one of the eight rows: that address expression is two
         * multiplies per row, i.e. more work than the eight pixel stores it
         * precedes. Colours and the plane election still resolve once per
         * glyph, never per pixel.
         */
        bool want_index = present_need_index_plane();
        bool want_rgb = present_need_rgb_plane();
        uint16_t fg_rgb = want_rgb ? pixel_rgb565_fast(fg) : 0;
        uint16_t bg_rgb = (want_rgb && !transparent) ? pixel_rgb565_fast(bg) : 0;
        uint8_t *dest = &backbuffer[(size_t)y * OS_WIDTH + x];
        uint16_t *dest_rgb = &backbuffer_rgb565[(size_t)y * OS_WIDTH + x];
        const uint8_t *glyph = font8x8_basic[glyph_index];

        for (int row = 0; row < 8; ++row) {
            uint8_t bits = glyph[row];

            if (transparent) {
                /*
                 * Transparent text is what essentially every label, menu
                 * entry and line of terminal output uses, and a glyph row has
                 * a handful of set bits in it. Walking those bits directly
                 * costs a bit-scan and a store each; the bit-at-a-time form
                 * paid eight shifts and eight tests to find the same pixels,
                 * and read both planes' flags at every column.
                 */
                while (bits != 0) {
                    int col = __builtin_ctz(bits);
                    if (want_index) dest[col] = fg;
                    if (want_rgb) dest_rgb[col] = fg_rgb;
                    bits = (uint8_t)(bits & (uint8_t)(bits - 1u));
                }
            } else if (bits == 0) {
                if (want_index) memset_local(dest, bg, 8);
                if (want_rgb) memset16_local(dest_rgb, bg_rgb, 8);
            } else if (bits == 0xFFu) {
                if (want_index) memset_local(dest, fg, 8);
                if (want_rgb) memset16_local(dest_rgb, fg_rgb, 8);
            } else {
                for (int col = 0; col < 8; ++col) {
                    if ((bits >> col) & 1u) {
                        if (want_index) dest[col] = fg;
                        if (want_rgb) dest_rgb[col] = fg_rgb;
                    } else {
                        if (want_index) dest[col] = bg;
                        if (want_rgb) dest_rgb[col] = bg_rgb;
                    }
                }
            }
            dest += OS_WIDTH;
            dest_rgb += OS_WIDTH;
        }
        return;
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

/*
 * Centre every line of a (possibly multi-line) string on its own width.
 *
 * strlen() over the whole block counted the newline and every later line,
 * so a two-line message was shifted left by half of ALL its characters and
 * both lines then started at that same x. That is what pushed DOOM's
 * "Not enough RAM for DOOM" / "(need 10 MB, machine has less)" notice off
 * the left edge of its window: the first line was wider than the space the
 * whole block left it. Per-line centring keeps each line inside the window
 * and matches the newline-aware draw_text() above (10px line pitch).
 */
static void draw_text_center(int center_x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent) {
    const char *line = text;

    for (;;) {
        const char *end = line;
        const char *p;
        int x;

        while (*end != '\0' && *end != '\n') {
            ++end;
        }
        x = center_x - (int)(end - line) * 8 / 2;
        for (p = line; p < end; ++p) {
            draw_char(x, y, *p, fg, bg, transparent);
            x += 8;
        }
        if (*end == '\0') {
            return;
        }
        line = end + 1;
        y += 10;
    }
}

static void draw_text_center_scaled(int center_x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent, int scale) {
    if (scale < 1) {
        scale = 1;
    }
    const char *line = text;

    for (;;) {
        const char *end = line;
        const char *p;
        int x;

        while (*end != '\0' && *end != '\n') {
            ++end;
        }
        x = center_x - (int)(end - line) * 8 * scale / 2;
        for (p = line; p < end; ++p) {
            draw_char_scaled(x, y, *p, fg, bg, transparent, scale);
            x += 8 * scale;
        }
        if (*end == '\0') {
            return;
        }
        line = end + 1;
        y += 10 * scale;
    }
}

/*
 * Emulated text cell height for the framebuffer text screens.  The default
 * 8x16 cell leaves an 8px gap between boot-screen lines; a screen that asks
 * for denser text (the xHCI input check) can shrink it to 14px so the white
 * diagnostic lines sit closer together.  Native VGA text mode is hardware
 * 8x16 and ignores this value.
 */
static int text_mode_cell_height = 16;

static void text_mode_set_cell_height(int height) {
    if (height < 8) {
        height = 8;
    }
    if (height > 16) {
        height = 16;
    }
    text_mode_cell_height = height;
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
    y = row * text_mode_cell_height;
    for (int i = 0; text[i] != '\0' && col + i < VGA_TEXT_COLS; ++i) {
        fill_rect(x + i * 8, y, 8, text_mode_cell_height, bg);
        draw_char(x + i * 8, y + (text_mode_cell_height - 8) / 2,
                  text[i], fg, bg, true);
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
        fill_rect(col * 8, row * text_mode_cell_height + text_mode_cell_height - 2,
                  8, 2, color_gray_light);
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

#define MAX_PACKED4_COLOURS 16

static uint16_t image_width(const uint8_t *image) {
    return (uint16_t)(image[0] | ((uint16_t)image[1] << 8));
}

static uint16_t image_height(const uint8_t *image) {
    return (uint16_t)(image[2] | ((uint16_t)image[3] << 8));
}

/*
 * Image asset formats:
 *   v1 (legacy, 4 B/px): index, alpha, rgb565 planes after the w/h u16s.
 *   v2: w/h u16s, then u8 format (0xA2 opaque / 0xB3 transparent), u8
 *       reserved, then optional 1 B/px alpha plane and the 2 B/px
 *       rgb565 plane. The index plane is synthesized on demand below.
 * Detection: byte 5 == 0 is impossible in v2 (format byte is 0xA2/0xB3),
 * while every v1 asset starts its index plane with a real palette index
 * that can be 0 - so a nonzero byte 5 signals v2.
 */
static bool image_is_v2(const uint8_t *image) {
    return (image[4] == 0xA2 || image[4] == 0xB3) && image[5] == 0x48;
}

/*
 * v3 "packed4" assets: the full-screen backgrounds are dithered from a
 * handful of colours, so they ship as 4 bits per pixel plus their own
 * 16-entry RGB565 palette (tools/mkpacked4.py repacks them losslessly).
 * That is a quarter of the bytes of the v2 RGB565 form - about 1.4 MB of
 * kernel image for the three backgrounds - and the blit expands two
 * pixels per byte instead of reading one 16-bit pixel at a time.
 */
static bool image_is_packed4(const uint8_t *image) {
    return image[4] == 0xC4 && image[5] == 0x48;
}

static const uint8_t *image_packed4_palette(const uint8_t *image) {
    return image + 7;
}

static const uint8_t *image_packed4_pixels(const uint8_t *image) {
    return image + 7 + (size_t)image[6] * 2u;
}

static bool image_has_alpha(const uint8_t *image) {
    return image_is_v2(image) ? image[4] == 0xB3 : true;
}

static const uint8_t *image_pixels(const uint8_t *image) {
    return image + 4;
}

static const uint8_t *image_alpha(const uint8_t *image) {
    if (image_is_v2(image)) {
        return image + 6;
    }
    size_t count = (size_t)image_width(image) * image_height(image);
    return image + 4 + count;
}

static const uint16_t *image_rgb565(const uint8_t *image) {
    if (image_is_v2(image)) {
        size_t count = (size_t)image_width(image) * image_height(image);
        return (const uint16_t *)(const void *)(image + 6 + (image[4] == 0xB3 ? count : 0));
    }
    size_t count = (size_t)image_width(image) * image_height(image);
    return (const uint16_t *)(const void *)(image + 4 + count + count);
}

/*
 * v2 asset index synthesis: a 64K-entry rgb565 -> palette-index LUT
 * built once (lazily, on first v2 draw) with the closed-form
 * nearest-color (per-channel cube scan + gray-ramp bracket probe),
 * verified to match brute-force nearest_color for all 65536 inputs.
 * draw_image_at converts each drawn pixel with one LUT load - no
 * per-asset index planes are stored anymore. 64KB BSS total.
 */
static uint8_t rgb565_index_lut[65536];
static bool rgb565_index_lut_ready = false;
static void rgb565_index_lut_build(void);

/* Inverse lookup used by the raw-RGB565 restore paths (crash badge). */
static uint8_t plane_index_for_rgb565(uint16_t rgb) {
    if (!rgb565_index_lut_ready) {
        rgb565_index_lut_build();
    }
    return rgb565_index_lut[rgb];
}

static void rgb565_index_lut_build(void) {
    static const uint8_t cube[6] = {0, 51, 102, 153, 204, 255};

    for (uint32_t v = 0; v < 65536u; ++v) {
        uint32_t r = (((v >> 11) & 0x1Fu) * 255u) / 31u;
        uint32_t g = (((v >> 5) & 0x3Fu) * 255u) / 63u;
        uint32_t b = ((v & 0x1Fu) * 255u) / 31u;
        uint32_t ri = 0;
        uint32_t gi = 0;
        uint32_t bi = 0;
        uint32_t best_r = (r > cube[0]) ? r : cube[0] - r;
        uint32_t best_g = (g > cube[0]) ? g : cube[0] - g;
        uint32_t best_b = (b > cube[0]) ? b : cube[0] - b;
        uint32_t dc;
        uint32_t gray_best = 0xFFFFFFFFu;
        uint32_t gray_index = 216;

        best_r *= best_r;
        best_g *= best_g;
        best_b *= best_b;

        for (uint32_t v2c = 1; v2c < 6; ++v2c) {
            uint32_t qr = (r > cube[v2c]) ? r - cube[v2c] : cube[v2c] - r;
            uint32_t qg = (g > cube[v2c]) ? g - cube[v2c] : cube[v2c] - g;
            uint32_t qb = (b > cube[v2c]) ? b - cube[v2c] : cube[v2c] - b;
            if (qr * qr < best_r) { best_r = qr * qr; ri = v2c; }
            if (qg * qg < best_g) { best_g = qg * qg; gi = v2c; }
            if (qb * qb < best_b) { best_b = qb * qb; bi = v2c; }
        }

        dc = best_r + best_g + best_b;

        {
            /* Gray ramp: squared distance is quadratic in shade s,
             * minimized near m=(r+g+b)/3 - probing the ramp entries
             * bracketing that minimum is sufficient. */
            uint32_t m = (r + g + b) / 3u;
            int32_t i0 = (int32_t)((m * 39u) / 255u);

            for (int32_t k = i0 - 1; k <= i0 + 1; ++k) {
                if (k < 0 || k >= 40) {
                    continue;
                }
                uint32_t s = (uint32_t)((k * 255u) / 39u);
                uint32_t qr = (s > r ? s - r : r - s);
                uint32_t qg = (s > g ? s - g : g - s);
                uint32_t qb = (s > b ? s - b : b - s);
                uint32_t d = qr * qr + qg * qg + qb * qb;
                if (d < gray_best) {
                    gray_best = d;
                    gray_index = (uint32_t)k;
                }
            }
        }

        if (gray_best < dc) {
            rgb565_index_lut[v] = (uint8_t)(216u + gray_index);
        } else {
            rgb565_index_lut[v] = (uint8_t)(36u * ri + 6u * gi + bi);
        }
    }
    rgb565_index_lut_ready = true;
}

static uint8_t image_pixel_index(const uint8_t *image, size_t i) {
    if (image_is_v2(image)) {
        if (!rgb565_index_lut_ready) {
            rgb565_index_lut_build();
        }
        return rgb565_index_lut[image_rgb565(image)[i]];
    }
    return image_pixels(image)[i];
}

static uint16_t palette_rgb565(uint8_t color) {
    Color c = palette[color];
    return (uint16_t)(((uint16_t)(c.r >> 3) << 11) |
                      ((uint16_t)(c.g >> 2) << 5) |
                      (uint16_t)(c.b >> 3));
}

/*
 * Runtime pixel-drawing hot path. palette[] is built once at boot and
 * never modified, so the RGB565 form of every palette index can be
 * cached in a 256-entry table. This turns the per-pixel cost of the old
 * path (palette struct load + 3 shifts + 3 ors) into a single byte load
 * from a table that stays hot in cache, and lets fill_rect hoist the
 * color conversion out of its loops entirely.
 */
static uint16_t palette_rgb565_cache[256];
static bool palette_rgb565_cache_ready = false;

static void palette_rgb565_cache_build(void) {
    for (int i = 0; i < 256; ++i) {
        palette_rgb565_cache[i] = palette_rgb565((uint8_t)i);
    }
    palette_rgb565_cache_ready = true;
}

static uint16_t pixel_rgb565_fast(uint8_t color) {
    if (!palette_rgb565_cache_ready) {
        palette_rgb565_cache_build();
    }
    return palette_rgb565_cache[color];
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

/*
 * Resolve a packed4 asset's 16-entry palette into the forms the output
 * planes want: RGB565 always, and the kernel palette index only when the
 * indexed plane is live. Both packed4 blitters start here, so the two
 * cannot disagree about what a colour index means.
 */
static void packed4_palette_resolve(const uint8_t *image, uint16_t *pal_rgb,
                                    uint8_t *pal_index, int *pal_count) {
    const uint8_t *palette = image_packed4_palette(image);
    int count = (int)image[6];
    bool want_index = present_need_index_plane();

    if (count > MAX_PACKED4_COLOURS) {
        count = MAX_PACKED4_COLOURS;
    }
    for (int i = 0; i < MAX_PACKED4_COLOURS; ++i) {
        uint16_t rgb = 0;

        if (i < count) {
            rgb = (uint16_t)(palette[i * 2] | ((uint16_t)palette[i * 2 + 1] << 8));
            if (want_index) {
                pal_index[i] = plane_index_for_rgb565(rgb);
            }
        } else if (want_index) {
            pal_index[i] = 0;
        }
        pal_rgb[i] = rgb;
    }
    *pal_count = count;
}

/*
 * Per-source-byte expansion table for packed4 rows.
 *
 * One source byte carries two pixels, so a 256-entry table keyed on the byte
 * removes the nibble split, the palette bound test and one of the two stores
 * from the inner loop: the loop body becomes a byte load, a table load and a
 * single store per two pixels.
 *
 * It is cached rather than rebuilt per call. The wallpaper is re-expanded on
 * every desktop repaint, and the tables describe the asset plus the plane
 * election only - neither of which changes between frames - so rebuilding
 * them 256 times per frame was pure waste on the most executed loop in the
 * OS, and cost 1.5KB of stack in the blitter's frame for the privilege.
 */
static const uint8_t *packed4_tables_image = NULL;
static bool packed4_tables_index = false;
static bool packed4_tables_rgb = false;
static uint32_t packed4_pair_rgb[256];
static uint16_t packed4_pair_index[256];

static void packed4_pair_tables(const uint8_t *image) {
    uint16_t pal_rgb[MAX_PACKED4_COLOURS];
    uint8_t pal_index[MAX_PACKED4_COLOURS];
    bool want_index = present_need_index_plane();
    bool want_rgb = present_need_rgb_plane();
    int count;

    /* The asset's palette comes out of the asset itself, so the image
     * pointer plus the plane election is the whole key. */
    if (image == packed4_tables_image && want_index == packed4_tables_index &&
        want_rgb == packed4_tables_rgb) {
        return;
    }
    packed4_palette_resolve(image, pal_rgb, pal_index, &count);

    /* Entries past the palette clamp to colour 0 exactly as the per-pixel
     * form did, so a byte whose nibbles are out of range paints the same
     * pixels it painted before. */
    for (uint32_t byte = 0; byte < 256; ++byte) {
        int hi = (int)(byte >> 4);
        int lo = (int)(byte & 0x0Fu);

        if (hi >= count) {
            hi = 0;
        }
        if (lo >= count) {
            lo = 0;
        }
        packed4_pair_rgb[byte] = (uint32_t)pal_rgb[hi] | ((uint32_t)pal_rgb[lo] << 16);
        packed4_pair_index[byte] = (uint16_t)((uint16_t)pal_index[hi] | ((uint16_t)pal_index[lo] << 8));
    }

    packed4_tables_image = image;
    packed4_tables_index = want_index;
    packed4_tables_rgb = want_rgb;
}

/*
 * Packed 4bpp blit. Two pixels leave per source byte, and the 16-entry
 * palette is resolved once per image: RGB565 for the presented plane, and
 * (only when the indexed plane is live) the kernel palette index for it.
 * The destination geometry is already clipped by the caller.
 */
static void draw_image_packed4(const uint8_t *image, int x0, int y0, int x1, int y1,
                               int src_x0, int src_y0) {
    uint16_t width = image_width(image);
    uint32_t stride = ((uint32_t)width + 1u) / 2u;
    const uint8_t *packed = image_packed4_pixels(image);
    bool want_index = present_need_index_plane();
    bool want_rgb = present_need_rgb_plane();
    uint16_t pal_rgb[MAX_PACKED4_COLOURS];
    uint8_t pal_index[MAX_PACKED4_COLOURS];
    int count;
    const uint32_t *pair_rgb = packed4_pair_rgb;
    const uint16_t *pair_index = packed4_pair_index;
    int row_w = x1 - x0;

    packed4_pair_tables(image);
    packed4_palette_resolve(image, pal_rgb, pal_index, &count);

    for (int py = 0; py < y1 - y0; ++py) {
        const uint8_t *row = packed + (size_t)(src_y0 + py) * stride;
        uint16_t *dest_rgb = &backbuffer_rgb565[(size_t)(y0 + py) * OS_WIDTH + x0];
        uint8_t *dest_idx = &backbuffer[(size_t)(y0 + py) * OS_WIDTH + x0];
        int px = 0;

        /*
         * Two pixels per source byte while the byte covers both of them, and
         * only when a single plane is live (the election is complementary, so
         * this is the normal case; both live would need both stores).
         *
         * Eight pixels (four source bytes) per iteration: the body is two
         * table loads and two stores per pair, so widening the step is what
         * takes the loop overhead out of the row. This is the full-screen
         * wallpaper on every desktop repaint, the single most executed loop
         * in the OS.
         */
        if ((src_x0 & 1) == 0 && want_rgb != want_index) {
            const uint8_t *src = row + (uint32_t)src_x0 / 2u;

            if (want_rgb) {
                uint32_t *dest32 = (uint32_t *)(void *)dest_rgb;

                for (; px + 7 < row_w; px += 8) {
                    dest32[0] = pair_rgb[src[0]];
                    dest32[1] = pair_rgb[src[1]];
                    dest32[2] = pair_rgb[src[2]];
                    dest32[3] = pair_rgb[src[3]];
                    dest32 += 4;
                    src += 4;
                }
                for (; px + 1 < row_w; px += 2) {
                    *dest32++ = pair_rgb[*src++];
                }
            } else {
                uint16_t *dest16 = (uint16_t *)(void *)dest_idx;

                for (; px + 7 < row_w; px += 8) {
                    dest16[0] = pair_index[src[0]];
                    dest16[1] = pair_index[src[1]];
                    dest16[2] = pair_index[src[2]];
                    dest16[3] = pair_index[src[3]];
                    dest16 += 4;
                    src += 4;
                }
                for (; px + 1 < row_w; px += 2) {
                    *dest16++ = pair_index[*src++];
                }
            }
        }

        for (; px < row_w; ++px) {
            uint32_t sx = (uint32_t)(src_x0 + px);
            uint8_t byte = row[sx >> 1];
            int entry = (int)((sx & 1u) ? (byte & 0x0Fu) : (byte >> 4));
            if (entry >= count) entry = 0;
            if (want_rgb) dest_rgb[px] = pal_rgb[entry];
            if (want_index) dest_idx[px] = pal_index[entry];
        }
    }
}

static void draw_image_at(const uint8_t *image, int x, int y, bool transparent) {
    uint16_t width = image_width(image);
    uint16_t height = image_height(image);
    const uint16_t *rgb565 = image_rgb565(image);
    /* v1 assets keep a stored index plane; v2 assets synthesize per
     * pixel via the rgb565_index_lut (see image_pixel_index). */
    const uint8_t *pixels = image_is_v2(image) ? NULL : image_pixels(image);
    const uint8_t *alpha = image_has_alpha(image) ? image_alpha(image) : NULL;
    int x0 = x;
    int y0 = y;
    int x1 = x + (int)width;
    int y1 = y + (int)height;
    int src_x0 = 0;
    int src_y0 = 0;

    /* Intersect against screen and window clip once, not per pixel. */
    if (x0 < 0) { src_x0 = -x0; x0 = 0; }
    if (y0 < 0) { src_y0 = -y0; y0 = 0; }
    if (x1 > OS_WIDTH) x1 = OS_WIDTH;
    if (y1 > OS_HEIGHT) y1 = OS_HEIGHT;
    if (clip_enabled) {
        if (clip_x0 > x0) { src_x0 += clip_x0 - x0; x0 = clip_x0; }
        if (clip_y0 > y0) { src_y0 += clip_y0 - y0; y0 = clip_y0; }
        if (x1 > clip_x1) x1 = clip_x1;
        if (y1 > clip_y1) y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    if (image_is_packed4(image)) {
        draw_image_packed4(image, x0, y0, x1, y1, src_x0, src_y0);
        return;
    }

    if (!rgb565_index_lut_ready && image_is_v2(image)) {
        rgb565_index_lut_build();
    }

    /* Plane election resolved once for the whole image. */
    {
        bool want_index = present_need_index_plane();
        bool want_rgb = present_need_rgb_plane();

        for (int py = 0; y0 + py < y1; ++py) {
            int sy = src_y0 + py;
            int dy = y0 + py;
            size_t row_base = (size_t)sy * width + src_x0;
            const uint8_t *src_p = pixels != NULL ? pixels + row_base : NULL;
            const uint16_t *src_r = rgb565 + row_base;
            const uint8_t *src_a = alpha != NULL ? alpha + row_base : NULL;
            uint8_t *dest_p = &backbuffer[(size_t)dy * OS_WIDTH + x0];
            uint16_t *dest_r = &backbuffer_rgb565[(size_t)dy * OS_WIDTH + x0];
            int row_w = x1 - x0;

            if ((!transparent || alpha == NULL) && pixels != NULL) {
                if (want_index) memcpy_local(dest_p, src_p, (size_t)row_w);
                if (want_rgb) memcpy_local(dest_r, src_r, (size_t)row_w * 2u);
                continue;
            }

            for (int px = 0; px < row_w; ++px) {
                if (transparent && alpha != NULL && src_a[px] < 128) {
                    continue;
                }
                if (want_rgb) dest_r[px] = src_r[px];
                if (want_index) dest_p[px] = pixels != NULL ? src_p[px] : rgb565_index_lut[src_r[px]];
            }
        }
    }
}

/*
 * Blit an image scaled to FILL the whole canvas (nearest neighbour).
 *
 * The wallpapers are 640x480 paintings and a desktop from the mode table can
 * be anything from the 640x480 base up to the canvas cap, so something has to
 * decide what a bigger desktop shows. This is the "Stretch" wallpaper fit:
 * one painting fills the screen at whatever size the desktop is, instead of
 * repeating (a tiled painting shows its own borders as seams across the
 * middle of the screen). Nearest neighbour is exact for the 1:1 case and
 * cheap for the rest, because the source row and column are held in
 * accumulators - no division per pixel, and no scaled copy of the art is
 * stored anywhere.
 *
 * The per-pixel format work is the same three asset forms draw_image_at
 * knows: v3 packed4 (what the backgrounds ship as), v2 rgb565 with the index
 * LUT, and v1 with its stored index plane.
 */
/*
 * Run starts for the packed4 nearest-neighbour wallpaper path.
 *
 * The old stretch loop walked destination pixels and advanced a source
 * accumulator for every one of them.  At 1024x768+ that is a million
 * iterations of format tests, palette bounds checks and accumulator
 * updates for a 640x480 painting.  A 4bpp wallpaper only has one colour
 * lookup per SOURCE pixel, so build the destination spans once per frame
 * and paint each span with the already-resolved colour.  The spans are
 * identical for every row; only the source row changes.
 */
static uint16_t packed4_run_start[MAX_OUTPUT_WIDTH + 1];

static void packed4_build_run_starts(int src_w, int dst_w) {
    int sx = 0;
    int acc = 0;

    for (int i = 0; i <= src_w; ++i) {
        packed4_run_start[i] = 0;
    }

    for (int x = 0; x < dst_w; ++x) {
        acc += src_w;
        while (acc >= dst_w) {
            acc -= dst_w;
            ++sx;
            if (sx <= src_w) {
                packed4_run_start[sx] = (uint16_t)(x + 1);
            }
        }
    }

    /* Defensive: a future caller with a degenerate scale must not leave a
     * trailing span pointing at column 0. */
    for (int i = sx + 1; i <= src_w; ++i) {
        packed4_run_start[i] = (uint16_t)dst_w;
    }
}

static void draw_packed4_stretched(const uint8_t *image, int src_w, int src_h,
                                   int dst_w, int dst_h,
                                   const uint16_t *pal_rgb,
                                   const uint8_t *pal_index, int pal_count,
                                   bool want_rgb) {
    const uint8_t *packed = image_packed4_pixels(image);
    uint32_t stride = ((uint32_t)src_w + 1u) / 2u;
    int sy = 0;
    int acc_y = 0;

    packed4_build_run_starts(src_w, dst_w);

    for (int y = 0; y < dst_h; ++y) {
        const uint8_t *row = packed + (size_t)sy * stride;
        uint16_t *dest_r = &backbuffer_rgb565[(size_t)y * (size_t)dst_w];
        uint8_t *dest_p = &backbuffer[(size_t)y * (size_t)dst_w];

        if (want_rgb) {
            for (int sx = 0; sx < src_w; ++sx) {
                int x0 = packed4_run_start[sx];
                int x1 = packed4_run_start[sx + 1];
                uint8_t byte;
                int entry;
                uint16_t color;

                if (x1 <= x0) {
                    continue;
                }
                byte = row[(uint32_t)sx >> 1];
                entry = (int)((sx & 1) ? (byte & 0x0Fu) : (byte >> 4));
                if (entry >= pal_count) {
                    entry = 0;
                }
                color = pal_rgb[entry];
                for (int x = x0; x < x1; ++x) {
                    dest_r[x] = color;
                }
            }
        } else {
            for (int sx = 0; sx < src_w; ++sx) {
                int x0 = packed4_run_start[sx];
                int x1 = packed4_run_start[sx + 1];
                uint8_t byte;
                int entry;
                uint8_t color;

                if (x1 <= x0) {
                    continue;
                }
                byte = row[(uint32_t)sx >> 1];
                entry = (int)((sx & 1) ? (byte & 0x0Fu) : (byte >> 4));
                if (entry >= pal_count) {
                    entry = 0;
                }
                color = pal_index[entry];
                for (int x = x0; x < x1; ++x) {
                    dest_p[x] = color;
                }
            }
        }

        acc_y += src_h;
        while (acc_y >= dst_h) {
            acc_y -= dst_h;
            ++sy;
        }
        if (sy >= src_h) {
            sy = src_h - 1;
        }
    }
}

static void draw_image_stretched(const uint8_t *image) {
    uint16_t src_w = image_width(image);
    uint16_t src_h = image_height(image);
    int dst_w = OS_WIDTH;
    int dst_h = OS_HEIGHT;
    bool want_index = present_need_index_plane();
    bool want_rgb = present_need_rgb_plane();
    bool packed4 = image_is_packed4(image);
    const uint16_t *rgb565 = packed4 ? NULL : image_rgb565(image);
    const uint8_t *pixels = (!packed4 && !image_is_v2(image)) ? image_pixels(image) : NULL;
    const uint8_t *packed = packed4 ? image_packed4_pixels(image) : NULL;
    uint32_t packed_stride = ((uint32_t)src_w + 1u) / 2u;
    uint16_t pal_rgb[MAX_PACKED4_COLOURS];
    uint8_t pal_index[MAX_PACKED4_COLOURS];
    int pal_count = packed4 ? (int)image[6] : 0;
    int sy = 0;
    int acc_y = 0;

    if (src_w == 0 || src_h == 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    /*
     * The art is already the size of the canvas: nothing is stretched, and
     * this is the case the OS boots into (a 640x480 wallpaper on the 640x480
     * canvas). Routing it to the exact blitter is not a micro-optimisation -
     * the loop below walks destination pixels with a source-column
     * accumulator and re-tests the asset form, the palette bound and both
     * plane elections on every one of them, which measured at ~29 emulated
     * instructions per pixel and made the wallpaper 86% of a full desktop
     * repaint. The 1:1 blitter resolves all of that once and expands two
     * packed pixels per source byte.
     *
     * Only taken with the window clip off: the stretch below ignores the clip
     * (it only ever paints the desktop), while the 1:1 blitter honours it.
     */
    if ((int)src_w == dst_w && (int)src_h == dst_h && !clip_enabled) {
        draw_image_at(image, 0, 0, false);
        return;
    }

    if (packed4) {
        packed4_palette_resolve(image, pal_rgb, pal_index, &pal_count);
        if ((int)src_w <= MAX_OUTPUT_WIDTH) {
            draw_packed4_stretched(image, (int)src_w, (int)src_h, dst_w, dst_h,
                                   pal_rgb, pal_index, pal_count, want_rgb);
            return;
        }
    } else if (!rgb565_index_lut_ready && image_is_v2(image)) {
        rgb565_index_lut_build();
    }

    for (int y = 0; y < dst_h; ++y) {
        uint16_t *dest_r = &backbuffer_rgb565[(size_t)y * (size_t)dst_w];
        uint8_t *dest_p = &backbuffer[(size_t)y * (size_t)dst_w];
        int sx = 0;
        int acc_x = 0;

        for (int x = 0; x < dst_w; ++x) {
            if (packed4) {
                uint8_t byte = packed[(size_t)sy * packed_stride + ((uint32_t)sx >> 1)];
                int entry = (int)((sx & 1) ? (byte & 0x0Fu) : (byte >> 4));
                if (entry >= pal_count) {
                    entry = 0;
                }
                if (want_rgb) {
                    dest_r[x] = pal_rgb[entry];
                }
                if (want_index) {
                    dest_p[x] = pal_index[entry];
                }
            } else {
                size_t si = (size_t)sy * src_w + (size_t)sx;
                if (want_rgb) {
                    dest_r[x] = rgb565[si];
                }
                if (want_index) {
                    dest_p[x] = pixels != NULL ? pixels[si] : rgb565_index_lut[rgb565[si]];
                }
            }

            /* Advance the source column by src_w per destination column:
             * the accumulator is the fraction of a source pixel covered so
             * far, so this is exact nearest-neighbour with no division. */
            acc_x += (int)src_w;
            while (acc_x >= dst_w) {
                acc_x -= dst_w;
                ++sx;
            }
        }

        acc_y += (int)src_h;
        while (acc_y >= dst_h) {
            acc_y -= dst_h;
            ++sy;
        }
        if (sy >= (int)src_h) {
            sy = (int)src_h - 1;
        }
    }
}

static void draw_image(const uint8_t *image) {
    draw_image_at(image, 0, 0, false);
}

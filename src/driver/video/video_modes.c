// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: video_modes.c, VBE video mode switching driver.

// This repository is licensed under the GNU General Public License.

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA 0x01CF
#define VBE_DISPI_INDEX_ID 0x0
#define VBE_DISPI_INDEX_XRES 0x1
#define VBE_DISPI_INDEX_YRES 0x2
#define VBE_DISPI_INDEX_BPP 0x3
#define VBE_DISPI_INDEX_ENABLE 0x4
#define VBE_DISPI_INDEX_BANK 0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH 0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET 0x8
#define VBE_DISPI_INDEX_Y_OFFSET 0x9
#define VBE_DISPI_ID0 0xB0C0
#define VBE_DISPI_ID5 0xB0C5
#define VBE_DISPI_DISABLED 0x00
#define VBE_DISPI_ENABLED 0x01
#define VBE_DISPI_LFB_ENABLED 0x40
#define VBE_DISPI_NOCLEARMEM 0x80

#define PCI_VENDOR_ID_VMWARE 0x15ADu
#define PCI_DEVICE_ID_VMWARE_SVGA 0x0710u
#define PCI_DEVICE_ID_VMWARE_SVGA2 0x0405u
#define PCI_DEVICE_ID_VMWARE_SVGA3 0x0406u
#define PCI_DEVICE_ID_VMWARE_SVGA_EFI 0x0407u
#define PCI_DEVICE_ID_VMWARE_SVGA4 0x0408u
#define PCI_DEVICE_ID_VMWARE_SVGA4_EFI 0x0409u
#define PCI_DEVICE_ID_VMWARE_SVGA4_RO 0x0410u
#define VMWARE_SVGA_LEGACY_BASE_PORT 0x4560

#define SVGA_MAGIC 0x900000UL
#define SVGA_MAKE_ID(ver) ((SVGA_MAGIC << 8) | (ver))
#define SVGA_ID_0 SVGA_MAKE_ID(0)
#define SVGA_ID_1 SVGA_MAKE_ID(1)
#define SVGA_ID_2 SVGA_MAKE_ID(2)

#define SVGA_INDEX_PORT 0
#define SVGA_VALUE_PORT 1

#define SVGA_REG_ID 0
#define SVGA_REG_ENABLE 1
#define SVGA_REG_WIDTH 2
#define SVGA_REG_HEIGHT 3
#define SVGA_REG_MAX_WIDTH 4
#define SVGA_REG_MAX_HEIGHT 5
#define SVGA_REG_DEPTH 6
#define SVGA_REG_BITS_PER_PIXEL 7
#define SVGA_REG_PSEUDOCOLOR 8
#define SVGA_REG_RED_MASK 9
#define SVGA_REG_GREEN_MASK 10
#define SVGA_REG_BLUE_MASK 11
#define SVGA_REG_BYTES_PER_LINE 12
#define SVGA_REG_FB_START 13
#define SVGA_REG_FB_OFFSET 14
#define SVGA_REG_VRAM_SIZE 15
#define SVGA_REG_FB_SIZE 16
#define SVGA_REG_CAPABILITIES 17
#define SVGA_REG_MEM_START 18
#define SVGA_REG_MEM_SIZE 19
#define SVGA_REG_CONFIG_DONE 20
#define SVGA_REG_SYNC 21
#define SVGA_REG_BUSY 22
#define SVGA_REG_GUEST_ID 23
#define SVGA_REG_HOST_BITS_PER_PIXEL 28
#define SVGA_REG_MEM_REGS 30

#define SVGA_REG_ENABLE_DISABLE 0
#define SVGA_REG_ENABLE_ENABLE 1

#define SVGA_CAP_8BIT_EMULATION 0x00000100u

#define SVGA_PALETTE_BASE 1024

#define SVGA_FIFO_MIN 0
#define SVGA_FIFO_MAX 1
#define SVGA_FIFO_NEXT_CMD 2
#define SVGA_FIFO_STOP 3

#define SVGA_CMD_UPDATE 1

#define VMWARE_GUEST_ID_OTHER 0x500A

static void build_system_palette(void) {
    int index = 0;
    const uint8_t cube[] = {0, 51, 102, 153, 204, 255};

    for (int r = 0; r < 6; ++r) {
        for (int g = 0; g < 6; ++g) {
            for (int b = 0; b < 6; ++b) {
                palette[index++] = (Color){cube[r], cube[g], cube[b]};
            }
        }
    }

    for (int i = 0; i < 40; ++i) {
        uint8_t shade = (uint8_t)((i * 255) / 39);
        palette[index++] = (Color){shade, shade, shade};
    }

    /* Grayscale slot 216 is repurposed as the exact crash-screen blue
     * (#0909BF). On 8bpp indexed framebuffers the theme color can then
     * hit this slot exactly instead of snapping to the 6x6x6 cube. */
    palette[216] = (Color){9, 9, 191};
}

static Color quantize_color_16(Color input) {
    static const Color ega16[16] = {
        {0, 0, 0},       {0, 0, 170},     {0, 170, 0},    {0, 170, 170},
        {170, 0, 0},     {170, 0, 170},   {170, 85, 0},   {170, 170, 170},
        {85, 85, 85},    {85, 85, 255},   {85, 255, 85},  {85, 255, 255},
        {255, 85, 85},   {255, 85, 255},  {255, 255, 85}, {255, 255, 255}
    };
    uint32_t best_distance = 0xFFFFFFFFu;
    Color best = ega16[0];

    for (int i = 0; i < 16; ++i) {
        int dr = (int)ega16[i].r - input.r;
        int dg = (int)ega16[i].g - input.g;
        int db = (int)ega16[i].b - input.b;
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = ega16[i];
        }
    }

    return best;
}

static void update_present_maps(void) {
    if (fb.width == 0 || fb.height == 0) {
        return;
    }

    /*
     * Re-pick the desktop canvas for this mode first (see the canvas note
     * in kernel.c): on anything that can hold the base canvas natively the
     * canvas BECOMES the mode, so the desktop fills the screen at 1:1 with
     * the same taskbar, text and icon sizes instead of being magnified or
     * letterboxed into a 640x480 picture. Only a mode smaller than the base
     * canvas - a real low-resolution mode - samples the canvas down.
     */
    update_canvas_for_framebuffer();

    present_content_width = fb.width < (uint32_t)OS_WIDTH ? fb.width : (uint32_t)OS_WIDTH;
    present_content_height = fb.height < (uint32_t)OS_HEIGHT ? fb.height : (uint32_t)OS_HEIGHT;
    present_offset_x = (fb.width - present_content_width) / 2u;
    present_offset_y = (fb.height - present_content_height) / 2u;

    /* New content box (or new output) means the bands around it are no
     * longer known to be black: any geometry presented since the last
     * blanking may have painted into what is now margin. The next scanout
     * repaints them - see the note on present_margins_dirty in kernel.c. */
    present_margins_dirty = true;

    /* Elect the shadow plane this output format presents (see the comment
     * on present_index_plane_live in vga.c) and, when the election
     * changes, blank the storage so the plane that is about to become
     * visible can never show bytes of the other format - index 0 and
     * RGB565 0 are both black.
     *
     * The live plane is the one present() actually reads: 8bpp indexed
     * output scans the index plane, everything else scans RGB565 (native
     * VGA 640x480x16 included, via the RGB565->EGA map). Only the larger
     * union member needs clearing, because both planes overlay one buffer. */
    {
        bool index_live = (fb.bpp == 8);
        if (index_live != present_index_plane_live) {
            present_index_plane_live = index_live;
            /* Two bytes per canvas pixel: both planes overlay the same
             * allocation, so this clears the larger member. */
            memset_local(backbuffer, 0, (size_t)OS_WIDTH * OS_HEIGHT * 2u);
        }
    }

    /* Full clear first: the mapped window moves between modes, so stale
     * entries outside the new content area must read as 0, not leftover
     * values from the previous geometry. */
    memset_local(present_x_map, 0, sizeof(present_x_map));
    memset_local(present_y_map, 0, sizeof(present_y_map));

    for (uint32_t x = 0; x < present_content_width; ++x) {
        present_x_map[present_offset_x + x] =
            (uint16_t)((x * OS_WIDTH) / present_content_width);
    }

    for (uint32_t y = 0; y < present_content_height; ++y) {
        present_y_map[present_offset_y + y] =
            (uint16_t)((y * OS_HEIGHT) / present_content_height);
    }
}

/*
 * The list the resolution row offers.
 *
 * On a machine that can really change its mode the answer is the firmware's
 * own size list (see the vbe_size_list_* accessors), because that is what can
 * actually be programmed: a card with 832x624 or 1152x864 in its list could
 * never be asked for them while the row only knew the seven built-in entries,
 * and a card offering few sizes had arrows that skipped over most of the
 * table. Every other backend keeps the built-in 4:3 / widescreen columns.
 */

/* Which column a request really reads.  The built-in table has two, and the
 * widescreen checkbox picks between them; a machine whose row is the
 * firmware's own list has one run of sizes, where the flag is ignored (see
 * vbe_size_list_length). */
static bool resolution_column(bool widescreen) {
    return widescreen;
}

/* The two built-in columns no longer have to be the same length: the wide
 * column carries the extra 16:9/16:10 sizes, while the standard column keeps
 * the 4:3/5:4 run.  The selected index is always reduced modulo this count. */
static int resolution_column_count(bool widescreen) {
    return widescreen ? VIDEO_MODE_COUNT_WIDE : VIDEO_MODE_COUNT_4_3;
}

/*
 * Does the widescreen checkbox have anything to switch here?
 *
 * On the firmware's list it does not - that list is one ascending run of the
 * sizes the card can really program, not two matching columns - so Settings
 * draws the row as unavailable rather than offering a control that would
 * change nothing.
 */
static bool resolution_widescreen_applicable(void) {
    return !(video_backend == VIDEO_BACKEND_MULTIBOOT && vbe_mode_available());
}

static int resolution_entry_count(bool widescreen) {
    bool column = resolution_column(widescreen);

    if (video_backend == VIDEO_BACKEND_MULTIBOOT && vbe_mode_available()) {
        return vbe_size_list_length(column);
    }
    return resolution_column_count(column);
}

static bool resolution_entry_at(bool widescreen, int index, VideoModeOption *out) {
    bool column = resolution_column(widescreen);

    if (video_backend == VIDEO_BACKEND_MULTIBOOT && vbe_mode_available()) {
        return vbe_size_list_at(column, index, out);
    }
    if (index < 0 || index >= resolution_column_count(column)) {
        return false;
    }
    *out = (column ? video_modes_wide : video_modes_4_3)[index];
    return true;
}

/* The entry the resolution arrows are pointing at, in whichever column the
 * widescreen checkbox selects. */
static const VideoModeOption *resolution_option_for_settings(const SettingsState *state) {
    static VideoModeOption entry;
    int count = resolution_entry_count(state->widescreen);

    if (count > 0 &&
        resolution_entry_at(state->widescreen, state->resolution_mode % count, &entry)) {
        return &entry;
    }

    /* Nothing to point at on this backend: the configured size, so the row
     * still names the desktop that is on screen. */
    entry.width = HALOXOS_CONFIG_SCREEN_WIDTH;
    entry.height = HALOXOS_CONFIG_SCREEN_HEIGHT;
    return &entry;
}

/*
 * Point the resolution selection at the display the boot actually produced.
 *
 * The desktop fills the output natively, and GRUB falls down its gfxpayload
 * list when a machine cannot do the first choice - so the mode on screen can
 * differ from the configured one. Leaving the selection on the configured
 * entry then opens Settings naming a size this machine is not showing, and on
 * a fixed display nothing else is selectable either, so the row would be dead
 * in both directions. Naming the size that is really there keeps the row
 * honest and the arrows meaningful on the backends that can move.
 *
 * No entry matches a size the card rounded to its own granularity (BGA turns
 * 1366x768 into 1360x768), in which case the configured selection is left
 * alone: it is still an entry this machine can present.
 */
static void snap_settings_resolution_to_desktop(void) {
    bool wide;
    bool found = false;
    int index;

    if (video_backend == VIDEO_BACKEND_MULTIBOOT && vbe_mode_available()) {
        /* The row offers this machine's own list, so the selection has to be
         * an entry of THAT list; a built-in index would name an unrelated
         * size here. */
        index = vbe_size_list_index_of((uint16_t)fb.width, (uint16_t)fb.height, &wide);
        if (index < 0) {
            return;
        }
    } else {
        for (index = 0; index < VIDEO_MODE_COUNT_4_3; ++index) {
            if (video_modes_4_3[index].width == fb.width &&
                video_modes_4_3[index].height == fb.height) {
                wide = false;
                found = true;
                break;
            }
        }
        if (!found) {
            for (index = 0; index < VIDEO_MODE_COUNT_WIDE; ++index) {
                if (video_modes_wide[index].width == fb.width &&
                    video_modes_wide[index].height == fb.height) {
                    wide = true;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return;
        }
    }

    settings_applied.widescreen = wide;
    settings_pending.widescreen = wide;
    settings_applied.resolution_mode = (uint8_t)index;
    settings_pending.resolution_mode = (uint8_t)index;
}

static uint16_t output_width_for_settings(const SettingsState *state) {
    return resolution_option_for_settings(state)->width;
}

static uint16_t output_height_for_settings(const SettingsState *state) {
    return resolution_option_for_settings(state)->height;
}

static uint8_t output_bpp_for_settings(const SettingsState *state) {
    if (state->palette_mode == 2) {
        return 16;
    }
    if (state->palette_mode == 1 && video_backend == VIDEO_BACKEND_VGA) {
        return 4;
    }
    return 8;
}

static void framebuffer_mode_string(char *buffer, size_t max_len, uint32_t width, uint32_t height, uint8_t bpp) {
    size_t len = 0;
    append_uint(buffer, &len, max_len, width);
    append_char(buffer, &len, max_len, 'x');
    append_uint(buffer, &len, max_len, height);
    append_char(buffer, &len, max_len, 'x');
    append_uint(buffer, &len, max_len, bpp);
}

static uint16_t bga_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

static void bga_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t vmware_port(uint8_t offset) {
    return (uint16_t)(vmware_svga.io_base + (uint16_t)vmware_svga.port_stride * offset);
}

static uint32_t vmware_read_reg(uint32_t index) {
    outl(vmware_port(SVGA_INDEX_PORT), index);
    return inl(vmware_port(SVGA_VALUE_PORT));
}

static void vmware_write_reg(uint32_t index, uint32_t value) {
    outl(vmware_port(SVGA_INDEX_PORT), index);
    outl(vmware_port(SVGA_VALUE_PORT), value);
}

static void clear_boot_status(void) {
    boot_status_text[0] = '\0';
    boot_menu_dirty = true;
}

static void set_boot_status(const char *text) {
    copy_string(boot_status_text, text, sizeof(boot_status_text));
    boot_menu_dirty = true;
}

static bool is_vmware_svga_device(uint16_t device_id) {
    return device_id == PCI_DEVICE_ID_VMWARE_SVGA ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA2 ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA3 ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA_EFI ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA4 ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA4_EFI ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA4_RO;
}

static bool find_vmware_svga_device(uint8_t *bus_out,
                                    uint8_t *slot_out,
                                    uint8_t *function_out,
                                    uint16_t *device_id_out,
                                    uint32_t *bar0_out) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t function = 0; function < 8; ++function) {
                uint32_t id = pci_config_read32((uint8_t)bus, slot, function, 0x00);
                uint16_t vendor_id;
                uint16_t device_id;
                uint32_t class_reg;

                if (id == 0xFFFFFFFFu) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                vendor_id = (uint16_t)(id & 0xFFFFu);
                device_id = (uint16_t)((id >> 16) & 0xFFFFu);
                if (vendor_id != PCI_VENDOR_ID_VMWARE || !is_vmware_svga_device(device_id)) {
                    continue;
                }

                class_reg = pci_config_read32((uint8_t)bus, slot, function, 0x08);
                if (((class_reg >> 24) & 0xFFu) != 0x03u || ((class_reg >> 16) & 0xFFu) != 0x00u) {
                    continue;
                }

                *bus_out = (uint8_t)bus;
                *slot_out = slot;
                *function_out = function;
                *device_id_out = device_id;
                *bar0_out = pci_config_read32((uint8_t)bus, slot, function, 0x10);
                return true;
            }
        }
    }

    return false;
}

static void pci_enable_device(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t command = pci_config_read32(bus, slot, function, 0x04);
    command |= 0x00000007u;
    pci_config_write32(bus, slot, function, 0x04, command);
}

static bool vmware_fifo_sync(void) {
    uint32_t spins = 0;

    if (!vmware_svga.fifo_ready) {
        return false;
    }

    vmware_write_reg(SVGA_REG_SYNC, 1);
    /* Bounded: the device normally clears BUSY within microseconds.
     * An unbounded spin froze the whole kernel inside the first
     * graphics frame (login screen) when the FIFO was wedged, with
     * the last frame still visible on screen and every key ignored.
     * Bail out, stop using the FIFO, and let the linear framebuffer
     * keep scanning out instead. */
    while (vmware_read_reg(SVGA_REG_BUSY) != 0) {
        if (++spins > 200000u) {
            vmware_svga.fifo_ready = false;
            serial_trace("WARNING", "VMware SVGA: FIFO busy timeout - falling back to the linear framebuffer");
            return false;
        }
    }
    return true;
}

static bool vmware_fifo_write_words(const uint32_t *words, uint32_t count) {
    volatile uint32_t *fifo = vmware_svga.fifo;
    uint32_t bytes_needed = count * 4u;

    if (!vmware_svga.fifo_ready || fifo == NULL || count == 0) {
        return false;
    }

    {
    uint32_t waits = 0;
    for (;;) {
        uint32_t min = fifo[SVGA_FIFO_MIN];
        uint32_t max = fifo[SVGA_FIFO_MAX];
        uint32_t next = fifo[SVGA_FIFO_NEXT_CMD];
        uint32_t stop = fifo[SVGA_FIFO_STOP];
        uint32_t free_bytes;

        if (next >= stop) {
            free_bytes = (max - next) + (stop - min);
        } else {
            free_bytes = stop - next;
        }

        if (free_bytes >= bytes_needed + 4u) {
            for (uint32_t i = 0; i < count; ++i) {
                fifo[next / 4u] = words[i];
                next += 4u;
                if (next == max) {
                    next = min;
                }
            }
            fifo[SVGA_FIFO_NEXT_CMD] = next;
            return true;
        }

        {
            static bool dbg_stall;
            if (!dbg_stall) {
                dbg_stall = true;
                serial_trace_hex_value("DBG", "SVGA fifo stall next", next);
                serial_trace_hex_value("DBG", "SVGA fifo stall stop", stop);
                serial_trace_hex_value("DBG", "SVGA fifo stall min", min);
                serial_trace_hex_value("DBG", "SVGA fifo stall max", max);
            }
        }
        if (!vmware_fifo_sync()) {
            return false;
        }
        if (++waits > 2000000u) {
            vmware_svga.fifo_ready = false;
            serial_trace("WARNING", "VMware SVGA: FIFO never freed space - falling back to the linear framebuffer");
            return false;
        }
    }
    }
}

/*
 * Ask the SVGA host to refresh one rectangle.  The command takes screen
 * coordinates, so a cursor-sized or DOOM-window-sized present no longer
 * has to tell the host the whole 640x480 surface changed.  The FIFO path
 * is the same as the full-screen update; only the rectangle differs.
 */
static void vmware_update_rect(int x, int y, int w, int h) {
    uint32_t command[5];

    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb.width) { w = (int)fb.width - x; }
    if (y + h > (int)fb.height) { h = (int)fb.height - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    command[0] = SVGA_CMD_UPDATE;
    command[1] = (uint32_t)x;
    command[2] = (uint32_t)y;
    command[3] = (uint32_t)w;
    command[4] = (uint32_t)h;
    if (!vmware_fifo_write_words(command, 5)) {
        serial_trace("DBG", "SVGA update: fifo refused");
        return;
    }
    vmware_fifo_sync();
}

static void vmware_update_screen(void) {
    vmware_update_rect(0, 0, (int)fb.width, (int)fb.height);
}

static bool init_vmware_svga_backend(void) {
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t function = 0;
    uint16_t device_id = 0;
    uint32_t bar0 = 0;
    uint32_t id = SVGA_ID_2;
    uint32_t fifo_regs_bytes;

    memset_local(&vmware_svga, 0, sizeof(vmware_svga));

    if (!find_vmware_svga_device(&bus, &slot, &function, &device_id, &bar0)) {
        return false;
    }

    /* The adapter answered, so a later failure means the DEVICE could not
     * give a usable mode - which is what the fallback warning says. Without
     * this the message also fired on machines that simply have no VMware
     * adapter at all (every non-BGA boot reaches this path first now). */
    vmware_svga.device_present = true;

    pci_enable_device(bus, slot, function);

    if (device_id == PCI_DEVICE_ID_VMWARE_SVGA) {
        vmware_svga.io_base = VMWARE_SVGA_LEGACY_BASE_PORT;
        vmware_svga.port_stride = 4;
    } else {
        if ((bar0 & 0x1u) == 0) {
            return false;
        }
        vmware_svga.io_base = (uint16_t)(bar0 & ~0x3u);
        vmware_svga.port_stride = 1;
    }

    while (true) {
        vmware_write_reg(SVGA_REG_ID, id);
        if (vmware_read_reg(SVGA_REG_ID) == id) {
            break;
        }
        if (id == SVGA_ID_0) {
            return false;
        }
        --id;
    }

    vmware_svga.fb_start = vmware_read_reg(SVGA_REG_FB_START);
    vmware_svga.fb_size = vmware_read_reg(SVGA_REG_FB_SIZE);
    vmware_svga.mem_start = vmware_read_reg(SVGA_REG_MEM_START);
    vmware_svga.mem_size = vmware_read_reg(SVGA_REG_MEM_SIZE);
    vmware_svga.fifo_num_regs = vmware_read_reg(SVGA_REG_MEM_REGS);
    vmware_svga.caps = vmware_read_reg(SVGA_REG_CAPABILITIES);
    vmware_svga.host_bpp = vmware_read_reg(SVGA_REG_HOST_BITS_PER_PIXEL);
    if (vmware_svga.host_bpp == 0) {
        vmware_svga.host_bpp = vmware_read_reg(SVGA_REG_BITS_PER_PIXEL);
    }
    vmware_svga.max_width = vmware_read_reg(SVGA_REG_MAX_WIDTH);
    vmware_svga.max_height = vmware_read_reg(SVGA_REG_MAX_HEIGHT);

    if (vmware_svga.fb_start == 0 || vmware_svga.mem_start == 0 || vmware_svga.mem_size == 0) {
        return false;
    }

    if (vmware_svga.fifo_num_regs < 4) {
        vmware_svga.fifo_num_regs = 4;
    }

    fifo_regs_bytes = vmware_svga.fifo_num_regs * 4u;
    if (fifo_regs_bytes >= vmware_svga.mem_size) {
        return false;
    }

    vmware_svga.fifo = (volatile uint32_t *)(uintptr_t)vmware_svga.mem_start;
    vmware_svga.fifo[SVGA_FIFO_MIN] = fifo_regs_bytes;
    vmware_svga.fifo[SVGA_FIFO_MAX] = vmware_svga.mem_size;
    vmware_svga.fifo[SVGA_FIFO_NEXT_CMD] = fifo_regs_bytes;
    vmware_svga.fifo[SVGA_FIFO_STOP] = fifo_regs_bytes;
    vmware_write_reg(SVGA_REG_CONFIG_DONE, 1);
    vmware_write_reg(SVGA_REG_GUEST_ID, VMWARE_GUEST_ID_OTHER);
    vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
    vmware_svga.fifo_ready = true;

    fb.address = (uint8_t *)(uintptr_t)vmware_svga.fb_start;
    fb.width = OS_BASE_WIDTH;
    fb.height = OS_BASE_HEIGHT;
    fb.pitch = OS_BASE_WIDTH;
    fb.bpp = 8;
    set_default_framebuffer_format(fb.bpp);
    update_present_maps();
    video_backend = VIDEO_BACKEND_VMWARE_SVGA;
    return true;
}

static bool detect_bga_backend(void) {
    uint16_t id = bga_read(VBE_DISPI_INDEX_ID);
    return id >= VBE_DISPI_ID0 && id <= VBE_DISPI_ID5;
}

static bool detect_video_mode_switch(void) {
    /*
     * All of these can move the display: the register backends through their
     * own devices, the classic VGA through its mode tables, and the
     * bootloader framebuffer through the video BIOS - or, on a machine whose
     * firmware cannot program modes at all, by re-sizing the desktop inside
     * the mode GRUB set.
     *
     * The last one is deliberately answered WITHOUT asking the firmware: this
     * runs in the boot, before the kernel has an interrupt table, and that is
     * exactly where a real-mode BIOS call has nowhere to fault to - a machine
     * that reset and beeped instead of booting.  The question waits for the
     * resolution list (video_mode_switch_prepare below).
     */
    return video_backend == VIDEO_BACKEND_BGA ||
           video_backend == VIDEO_BACKEND_VMWARE_SVGA ||
           video_backend == VIDEO_BACKEND_VGA ||
           video_backend == VIDEO_BACKEND_MULTIBOOT;
}

/*
 * The one place the firmware is asked what it can program.  Called when
 * Settings is opened, so the boot is over and the desktop is running by the
 * time any of this happens.
 *
 * On a machine with a video BIOS the resolution row then becomes that
 * firmware's own mode list and Apply programs real display modes; on a machine
 * without one nothing changes and the row keeps the built-in sizes, which
 * re-size the desktop inside the mode the bootloader set.  A firmware that
 * will not answer costs the row, never the boot.
 */
static void video_mode_switch_prepare(void) {
    if (video_backend != VIDEO_BACKEND_MULTIBOOT) {
        return;
    }

    vbe_prepare();
    video_mode_switch_available = detect_video_mode_switch();

    if (vbe_mode_available()) {
        /*
         * The row now offers the firmware's list, so the selection has to name
         * an entry of THAT list: an index from the built-in table would point
         * at an unrelated size.
         */
        snap_settings_resolution_to_desktop();
    } else {
        serial_trace("INFO", "VBE: no firmware mode list; the resolution row keeps its built-in sizes");
    }
}

/*
 * The resolution row's source, as a value for Settings to print next to a
 * plain label: the number of sizes when the firmware's list is what the row
 * offers, and the reason it is not otherwise.  Terse on purpose - it is one
 * field of a row, not a paragraph.
 */
static void video_mode_list_string(char *buffer, size_t max_len) {
    size_t len = 0;

    buffer[0] = '\0';
    if (vbe_mode_available()) {
        append_uint(buffer, &len, max_len, (uint32_t)vbe_size_count);
        append_string(buffer, &len, max_len, " sizes");
    } else {
        append_string(buffer, &len, max_len, vbe_status_text());
    }
}

static const char *video_backend_name(void) {
    switch (video_backend) {
        case VIDEO_BACKEND_MULTIBOOT: return "Multiboot framebuffer";
        case VIDEO_BACKEND_VGA: return "IBM VGA";
        case VIDEO_BACKEND_BGA: return "Bochs/QEMU BGA";
        case VIDEO_BACKEND_VMWARE_SVGA: return "VMware SVGA";
        default: return "None";
    }
}

static void serial_trace_video_mode(const char *label) {
    if (!debug) {
        return;
    }
    serial_trace_begin("INFO");
    serial_write_string(label);
    serial_write_string(": ");
    serial_write_uint(fb.width);
    serial_write_char('x');
    serial_write_uint(fb.height);
    serial_write_string("x");
    serial_write_uint(fb.bpp);
    serial_write_string(" pitch=");
    serial_write_uint(fb.pitch);
    serial_write_string(" fb=");
    serial_write_hex32((uint32_t)(uintptr_t)fb.address);
    serial_write_string(" backend=");
    serial_write_string(video_backend_name());
    serial_write_string("\n");
}

/*
 * Geometry of the last mode this driver accepted, and the two device
 * program/readback pairs the mode setter and the rollback share.
 *
 * The DISPI and SVGA registers are programmed BEFORE the readback is judged,
 * so a mode the card cannot actually hold used to leave the card showing it
 * while set_framebuffer_mode_raw reported failure: the caller only reverted
 * the settings, and the desktop stayed stranded at whatever the card had
 * been told. A 640x480 canvas presented into a 192x144 framebuffer was the
 * visible result, and every later switch started from the broken geometry,
 * so the screen could never be brought back to a usable resolution again.
 *
 * Keeping the last accepted mode lets a failed request put it back: the
 * caller's revert then lands on a working screen instead of a dead one.
 */
static bool last_good_mode_valid = false;
static uint16_t last_good_width = 0;
static uint16_t last_good_height = 0;
static uint16_t last_good_bpp = 0;

static void remember_usable_mode(void) {
    /*
     * "Usable" means the present step can describe it, not "at least as big
     * as the canvas": a mode below the 640x480 base is a real low-resolution
     * output, and the desktop handles it by keeping its base canvas and
     * sampling it down (see update_canvas_for_framebuffer). Refusing to
     * remember those modes would leave a failed sub-base switch with nothing
     * to roll back to.
     */
    if (fb.width == 0 || fb.height == 0 ||
        fb.width > MAX_OUTPUT_WIDTH || fb.height > MAX_OUTPUT_HEIGHT || fb.bpp == 0) {
        return;
    }
    last_good_mode_valid = true;
    last_good_width = (uint16_t)fb.width;
    last_good_height = (uint16_t)fb.height;
    last_good_bpp = fb.bpp;
}

static void bga_program(uint16_t width, uint16_t height, uint16_t bpp) {
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, width);
    bga_write(VBE_DISPI_INDEX_YRES, height);
    bga_write(VBE_DISPI_INDEX_BPP, bpp);
    bga_write(VBE_DISPI_INDEX_BANK, 0);
    bga_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, height);
    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);
}

static void bga_read_geometry(void) {
    fb.width = bga_read(VBE_DISPI_INDEX_XRES);
    fb.height = bga_read(VBE_DISPI_INDEX_YRES);
    fb.bpp = (uint8_t)bga_read(VBE_DISPI_INDEX_BPP);
    fb.pitch = fb.width * ((fb.bpp + 7u) / 8u);
}

static void vmware_program(uint16_t width, uint16_t height, uint16_t bpp) {
    vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
    vmware_write_reg(SVGA_REG_WIDTH, width);
    vmware_write_reg(SVGA_REG_HEIGHT, height);
    vmware_write_reg(SVGA_REG_BITS_PER_PIXEL, bpp);
}

static void vmware_read_geometry(void) {
    vmware_svga.fb_offset = vmware_read_reg(SVGA_REG_FB_OFFSET);
    fb.pitch = vmware_read_reg(SVGA_REG_BYTES_PER_LINE);
    fb.width = vmware_read_reg(SVGA_REG_WIDTH);
    fb.height = vmware_read_reg(SVGA_REG_HEIGHT);
    fb.bpp = (uint8_t)vmware_read_reg(SVGA_REG_BITS_PER_PIXEL);
    fb.address = (uint8_t *)(uintptr_t)(vmware_svga.fb_start + vmware_svga.fb_offset);
    vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_ENABLE);
}

/*
 * Put the last accepted mode back on the card and re-derive the present maps
 * for it. False means there was nothing to restore (or the device would not
 * take it back), in which case the caller is no worse off than before.
 */
static bool restore_last_good_mode(void) {
    if (!last_good_mode_valid) {
        return false;
    }

    if (video_backend == VIDEO_BACKEND_BGA) {
        bga_program(last_good_width, last_good_height, last_good_bpp);
        bga_read_geometry();
    } else if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        vmware_program(last_good_width, last_good_height, last_good_bpp);
        vmware_read_geometry();
    } else if (video_backend == VIDEO_BACKEND_MULTIBOOT && vbe_mode_available()) {
        /*
         * A card with no programmable registers is undone through the same
         * video BIOS call that moved it: put the mode that was working back.
         * Its own rollback covers the case where a set fails mid-flight; this
         * is the recovery for a caller that refused the result afterwards.
         */
        if (!vbe_set_mode(last_good_width, last_good_height, (uint8_t)last_good_bpp)) {
            return false;
        }
        boot_text_mode = false;
        update_present_maps();
        serial_trace_video_mode("Restored the last usable mode");
        return true;
    } else {
        return false;
    }

    if (fb.width != last_good_width || fb.height != last_good_height) {
        return false;
    }

    if (fb.bpp != (uint8_t)last_good_bpp) {
        set_default_framebuffer_format(fb.bpp);
    }
    boot_text_mode = false;
    update_present_maps();
    serial_trace_video_mode("Restored the last usable mode");
    return true;
}

static bool set_framebuffer_mode_raw(uint16_t width, uint16_t height, uint16_t bpp) {
    uint16_t actual_bpp = bpp;

    if (width > MAX_OUTPUT_WIDTH || height > MAX_OUTPUT_HEIGHT) {
        return false;
    }

    /*
     * BGA and VMware have no linear 4bpp framebuffer. Low (16 colors) on
     * those backends uses an 8-bit mode with the DAC quantized to the
     * classic 16 EGA colors instead. The real planar 4bpp path stays on
     * the classic VGA backend.
     */
    if (bpp == 4 && video_backend != VIDEO_BACKEND_VGA) {
        bpp = 8;
        actual_bpp = 8;
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        if (width > vmware_svga.max_width || height > vmware_svga.max_height) {
            return false;
        }

        if (bpp == 8) {
            if ((vmware_svga.caps & SVGA_CAP_8BIT_EMULATION) != 0) {
                actual_bpp = 8;
            } else if (vmware_svga.host_bpp >= 16) {
                actual_bpp = (uint16_t)vmware_svga.host_bpp;
            } else {
                return false;
            }
        } else if (bpp == 16) {
            if (vmware_svga.host_bpp < 16) {
                return false;
            }
            actual_bpp = (uint16_t)vmware_svga.host_bpp;
        } else if (bpp != vmware_svga.host_bpp) {
            return false;
        }

        vmware_program(width, height, actual_bpp);
        vmware_read_geometry();

        /* Same rule as the BGA path below: the device has already been
         * reprogrammed by now, so a strict comparison here could only leave
         * the guest presenting a geometry its own maps no longer describe.
         * Adopt the readback whenever it is a usable mode - the only unusable
         * answers are an empty geometry and one past the output cap, because
         * a mode BELOW the base canvas is a legitimate low-resolution output
         * the present step samples the desktop down into. */
        if (fb.width == 0 || fb.height == 0 ||
            fb.width > MAX_OUTPUT_WIDTH || fb.height > MAX_OUTPUT_HEIGHT) {
            restore_last_good_mode();
            return false;
        }

        if (fb.bpp != actual_bpp) {
            set_default_framebuffer_format(fb.bpp);
        }

        boot_text_mode = false;
        vga_clear_native_text_flag();
        update_present_maps();
        remember_usable_mode();
        return true;
    }

    if (video_backend == VIDEO_BACKEND_VGA) {
        if (vga_graphics_active && fb.width == width && fb.height == height && fb.bpp == bpp) {
            boot_text_mode = false;
            remember_usable_mode();
            return true;
        }
        if (vga_set_graphics_mode(width, height, bpp)) {
            boot_text_mode = false;
            remember_usable_mode();
            return true;
        }
        /*
         * No VBE linear framebuffer: keep the requested 640x480 geometry
         * with mode 12h (16 colors) before dropping to 320x200x256.
         * Resolution-correct beats colorful-but-squashed.
         */
        if (vga_set_graphics_mode(640, 480, 4)) {
            serial_trace("WARNING", "classic VGA: using 640x480x16 native fallback");
            boot_text_mode = false;
            remember_usable_mode();
            return true;
        }
        if (vga_set_graphics_mode(320, 200, 8)) {
            serial_trace("WARNING", "classic VGA: using 320x200x256 fallback");
            boot_text_mode = false;
            remember_usable_mode();
            return true;
        }
        return false;
    }

    if (video_backend == VIDEO_BACKEND_MULTIBOOT) {
        /*
         * Program the mode through the video BIOS when there is one to talk
         * to.  This is the difference between "the desktop got smaller inside
         * the mode GRUB set" and the display really changing, which is what
         * the resolution list promises on every other backend.
         */
        if (vbe_set_mode(width, height, bpp)) {
            boot_text_mode = false;
            vga_clear_native_text_flag();
            update_present_maps();
            remember_usable_mode();
            return true;
        }

        if (vbe_mode_available()) {
            /*
             * The card has a mode list and this size is not on it.  Refusing
             * is the honest answer: falling through to the adopt path would
             * re-size the desktop inside the current mode, which leaves the
             * screen with a border around a smaller desktop instead of
             * showing the resolution that was asked for.
             */
            return false;
        }

        /*
         * No VBE list on this bootloader framebuffer: the output itself cannot
         * move, but the desktop can still be re-sized inside it.  Only a
         * request that fits the output is a real presentation; a larger one
         * would be clamped back to the output by update_canvas_for_framebuffer
         * while the settings row claimed otherwise.
         */
        (void)bpp;
        if (fb.address != NULL &&
            width <= fb.width && height <= fb.height &&
            fb.width >= OS_BASE_WIDTH && fb.height >= OS_BASE_HEIGHT) {
            remember_usable_mode();
            return true;
        }
        return false;
    }

    if (video_backend == VIDEO_BACKEND_NONE) {
        return false;
    }

    if (!video_mode_switch_available) {
        return false;
    }

    bga_program(width, height, bpp);

    /* Leaving the boot text menu through a real DISPI graphics mode:
     * the classic VGA text flag is stale now (enter_boot_text_mode set
     * it, and this path bypasses vga_set_graphics_mode which would clear
     * it), so clear it here or the crash screen would refuse to render. */
    boot_text_mode = false;
    vga_clear_native_text_flag();

    bga_read_geometry();

    /*
     * Adopt what the card actually programmed instead of insisting on the
     * request byte for byte. BGA rounds to its own granularity - 1366 comes
     * back as 1360 - and the old strict comparison returned "failed" AFTER
     * the registers had already been written, with the present maps still
     * describing the previous geometry: the desktop was left as a black
     * screen because nothing matched the new mode any more.
     *
     * The readback is therefore the truth: fb is what the card reports, the
     * canvas follows it (update_present_maps -> update_canvas_for_framebuffer)
     * and the desktop fills the geometry that is really there. Only an
     * unusable readback - an empty geometry, something past the output cap,
     * or a pitch too small for a row - still fails.
     *
     * The base-canvas floor specifically must NOT be part of that judgement:
     * a smaller mode (a firmware that booted below 480p, not a list entry any
     * more) keeps the 640x480 canvas and the present maps sample it down,
     * which is exactly how the pre-canvas build presented those modes.
     * Judging them unusable made the resolution arrows skip the bottom of
     * every column.
     */
    if (fb.width == 0 || fb.height == 0 ||
        fb.width > MAX_OUTPUT_WIDTH || fb.height > MAX_OUTPUT_HEIGHT ||
        fb.pitch < fb.width * ((fb.bpp + 7u) / 8u)) {
        /* The card would not take the requested geometry. Put back the mode
         * it was showing: the DISPI registers are already pointing at the
         * refused size, and leaving them there stranded the desktop on a
         * screen nothing could undo. */
        restore_last_good_mode();
        return false;
    }

    if (fb.bpp != bpp) {
        /* The depth is not the one that was asked for: re-derive the pixel
         * format the present step decodes with before showing anything. */
        set_default_framebuffer_format(fb.bpp);
    }

    update_present_maps();
    remember_usable_mode();
    return true;
}

static bool set_output_mode(const SettingsState *state) {
    return set_framebuffer_mode_raw(output_width_for_settings(state),
                                    output_height_for_settings(state),
                                    output_bpp_for_settings(state));
}

static void enter_boot_text_mode(void) {
    boot_text_mode = true;
    if (video_backend == VIDEO_BACKEND_BGA) {
        /*
         * QEMU's std VGA (Bochs VBE): keep the linear framebuffer and draw
         * the boot menu as emulated text into it, exactly like a
         * bootloader-framebuffer machine does.
         *
         * Disabling DISPI to drop back to the legacy VGA text mode is not a
         * usable path on current QEMU builds: the text buffer at 0xB8000 is
         * written correctly (the whole menu is there), but the scanout keeps
         * fetching from the linear framebuffer, so the display stays black.
         * That is why the DOS-style boot menu was invisible on QEMU only,
         * while real hardware and the other hypervisors showed it.
         *
         * A BGA boot that started in text mode (a 4bpp build whose GRUB
         * gfxpayload is "text") has no live linear framebuffer to draw into,
         * so that case keeps the native VGA text path - pressing 1
         * programs a real DISPI graphics mode from there.
         */
        if ((bga_read(VBE_DISPI_INDEX_ENABLE) & VBE_DISPI_ENABLED) != 0) {
            if (fb.bpp == 8) {
                /* Indexed output: the menu's palette indices need the OS
                 * palette in the DAC to land on the intended colours. */
                program_vga_palette();
            }
            return;
        }
        bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        vga_enter_text_mode();
    } else if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
        vga_enter_text_mode();
    } else if (video_backend == VIDEO_BACKEND_MULTIBOOT && fb.bpp == 8) {
        /*
         * Emulated text menu on an indexed VBE LFB: the DAC still holds the
         * bootloader's palette, so the menu's palette indices land on
         * arbitrary colors (cyan headers, invisible gray text). Program the
         * OS palette into the DAC so the indices render as intended.
         */
        program_vga_palette();
    } else if (video_backend == VIDEO_BACKEND_VGA && vga_graphics_active && !vga_native_text_mode_active()) {
        vga_save_and_enter_text_mode();
    }
}

static void enter_main_graphics_mode(void) {
    bool switched = set_framebuffer_mode_raw(HALOXOS_CONFIG_SCREEN_WIDTH,
                                             HALOXOS_CONFIG_SCREEN_HEIGHT,
                                             output_bpp_for_settings(&settings_applied));

    if (switched) {
        boot_text_mode = false;
        program_vga_palette();
    } else {
        serial_trace("ERROR", "graphics mode unavailable");
        serial_trace_video_mode("Graphics mode unavailable state");
    }

    /*
     * The desktop is the mode that is really being presented: the whole screen
     * is desktop, natively at 1:1, with nothing left over.
     *
     * That is also what keeps the two kinds of machine in step. Where a mode
     * can be programmed the desktop follows the configured mode, and on a
     * bootloader framebuffer - real hardware, or a VM without the BGA/SVGA
     * register interfaces - the mode GRUB set is the display, so following it
     * means the screen is exactly the resolution that was asked for: no
     * letterbox border, no scaling, no second size to keep in sync.
     *
     * `switched` cannot be used to tell those apart (adopting a handed-over
     * framebuffer reports success without programming anything), but it does
     * not have to be: the readback in fb is the truth in both cases.
     *
     * The maps are refreshed explicitly as well, because set_desktop_size()
     * returns early when the request has not changed and at boot the request
     * already is the base canvas - the bootloader-framebuffer path would
     * otherwise keep the maps derived for the geometry of the boot menu.
     */
    set_desktop_size((int)fb.width, (int)fb.height);
    update_present_maps();

    /* The settings app speaks about the desktop that is on screen, so its
     * resolution list starts from the size this boot really produced (see
     * snap_settings_resolution_to_desktop). */
    snap_settings_resolution_to_desktop();

    if (switched) {
        serial_trace_video_mode("Graphics mode entered");
    }
}

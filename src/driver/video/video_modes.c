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
     * The desktop is a 640x480 logical canvas.  Keep it 1:1 when the active
     * framebuffer is larger; this avoids advertising a larger VBE mode while
     * merely magnifying the old desktop.  Only native low-resolution modes
     * sample the canvas down to their real hardware dimensions.
     */
    present_content_width = fb.width < OS_WIDTH ? fb.width : OS_WIDTH;
    present_content_height = fb.height < OS_HEIGHT ? fb.height : OS_HEIGHT;
    present_offset_x = (fb.width - present_content_width) / 2u;
    present_offset_y = (fb.height - present_content_height) / 2u;

    for (uint32_t x = 0; x < present_content_width; ++x) {
        present_x_map[present_offset_x + x] =
            (uint16_t)((x * OS_WIDTH) / present_content_width);
    }

    for (uint32_t y = 0; y < present_content_height; ++y) {
        present_y_map[present_offset_y + y] =
            (uint16_t)((y * OS_HEIGHT) / present_content_height);
    }
}

static uint16_t output_width_for_settings(const SettingsState *state) {
    static const uint16_t four_three[] = {960, 640, 480, 320, 192};
    static const uint16_t wide[] = {1280, 854, 640, 426, 256};
    return (state->widescreen ? wide : four_three)[state->resolution_mode % 5];
}

static uint16_t output_height_for_settings(const SettingsState *state) {
    static const uint16_t heights[] = {720, 480, 360, 240, 144};
    return heights[state->resolution_mode % 5];
}

static uint8_t output_bpp_for_settings(const SettingsState *state) {
    if (state->palette_mode == 2) {
        return 16;
    }
    return state->palette_mode == 1 && video_backend == VIDEO_BACKEND_VGA ? 4 : 8;
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
    if (!vmware_svga.fifo_ready) {
        return false;
    }

    vmware_write_reg(SVGA_REG_SYNC, 1);
    while (vmware_read_reg(SVGA_REG_BUSY) != 0) {
    }
    return true;
}

static bool vmware_fifo_write_words(const uint32_t *words, uint32_t count) {
    volatile uint32_t *fifo = vmware_svga.fifo;
    uint32_t bytes_needed = count * 4u;

    if (!vmware_svga.fifo_ready || fifo == NULL || count == 0) {
        return false;
    }

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

        if (!vmware_fifo_sync()) {
            return false;
        }
    }
}

static void vmware_update_screen(void) {
    const uint32_t command[5] = {SVGA_CMD_UPDATE, 0, 0, fb.width, fb.height};

    if (!vmware_fifo_write_words(command, 5)) {
        return;
    }

    vmware_fifo_sync();
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
    fb.width = OS_WIDTH;
    fb.height = OS_HEIGHT;
    fb.pitch = OS_WIDTH;
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
    return video_backend == VIDEO_BACKEND_BGA ||
           video_backend == VIDEO_BACKEND_VMWARE_SVGA ||
           video_backend == VIDEO_BACKEND_VGA;
}

static const char *video_backend_name(void) {
    switch (video_backend) {
        case VIDEO_BACKEND_MULTIBOOT: return "Multiboot framebuffer";
        case VIDEO_BACKEND_BGA: return "Bochs/QEMU BGA";
        case VIDEO_BACKEND_VMWARE_SVGA: return "VMware SVGA";
        case VIDEO_BACKEND_VGA: return "Legacy VGA";
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

static bool set_framebuffer_mode_raw(uint16_t width, uint16_t height, uint16_t bpp) {
    uint16_t actual_bpp = bpp;

    if (width > MAX_OUTPUT_WIDTH || height > MAX_OUTPUT_HEIGHT) {
        return false;
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

        vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
        vmware_write_reg(SVGA_REG_WIDTH, width);
        vmware_write_reg(SVGA_REG_HEIGHT, height);
        vmware_write_reg(SVGA_REG_BITS_PER_PIXEL, actual_bpp);
        vmware_svga.fb_offset = vmware_read_reg(SVGA_REG_FB_OFFSET);
        fb.pitch = vmware_read_reg(SVGA_REG_BYTES_PER_LINE);
        fb.width = vmware_read_reg(SVGA_REG_WIDTH);
        fb.height = vmware_read_reg(SVGA_REG_HEIGHT);
        fb.bpp = (uint8_t)vmware_read_reg(SVGA_REG_BITS_PER_PIXEL);
        fb.address = (uint8_t *)(uintptr_t)(vmware_svga.fb_start + vmware_svga.fb_offset);
        vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_ENABLE);

        if (fb.width != width || fb.height != height || fb.bpp != actual_bpp) {
            return false;
        }

        update_present_maps();
        return true;
    }

    if (video_backend == VIDEO_BACKEND_MULTIBOOT) {
        if (fb.address == NULL || width != fb.width || height != fb.height) {
            return false;
        }
        /* Indexed palettes are rendered in software on an RGB boot buffer. */
        return bpp == 4 || bpp == 8 || (bpp == 16 && fb.bpp >= 15);
    }

    if (video_backend == VIDEO_BACKEND_VGA) {
        return vga_set_legacy_mode(width, height, bpp);
    }

    if (video_backend == VIDEO_BACKEND_NONE) {
        return false;
    }

    if (!video_mode_switch_available) {
        return false;
    }

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

    fb.width = bga_read(VBE_DISPI_INDEX_XRES);
    fb.height = bga_read(VBE_DISPI_INDEX_YRES);
    fb.bpp = (uint8_t)bga_read(VBE_DISPI_INDEX_BPP);
    fb.pitch = fb.width * ((fb.bpp + 7u) / 8u);

    if (fb.width != width || fb.height != height || fb.bpp != bpp) {
        return false;
    }

    update_present_maps();
    return true;
}

static bool set_output_mode(const SettingsState *state) {
    return set_framebuffer_mode_raw(output_width_for_settings(state),
                                    output_height_for_settings(state),
                                    output_bpp_for_settings(state));
}

static void enter_boot_text_mode(void) {
    boot_text_mode = true;
    if (video_backend == VIDEO_BACKEND_BGA && video_mode_switch_available) {
        bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    } else if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
    } else if (video_backend == VIDEO_BACKEND_VGA) {
        vga_restore_text_mode();
    }
}

static void enter_main_graphics_mode(void) {
    if (!set_framebuffer_mode_raw(HALOXOS_CONFIG_SCREEN_WIDTH,
                                  HALOXOS_CONFIG_SCREEN_HEIGHT,
                                  output_bpp_for_settings(&settings_applied))) {
        serial_trace("ERROR", "graphics mode unavailable");
        serial_trace_video_mode("Graphics mode unavailable state");
        return;
    }
    boot_text_mode = false;
    program_vga_palette();
    serial_trace_video_mode("Graphics mode entered");
}

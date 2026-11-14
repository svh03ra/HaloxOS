// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: disk_info.c, disk info and etc.

// This repository is licensed under the GNU General Public License.

static const char *disk_physical_type_label(void) {
    if (boot_medium_is_usb) {
        return "USB";
    }
    if (boot_drive_valid) {
        if (boot_drive_number >= 0xE0u || boot_drive_number >= 0x9Fu) {
            return "CD-ROM";
        }
        if (boot_drive_number >= 0x80u) {
            return "Hard Disk";
        }
        return "Floppy";
    }
    return "CD-ROM";
}

static void detect_boot_drive_info(const MultibootInfo *mbi) {
    boot_drive_info_available = false;
    boot_drive_mode = 0;
    boot_drive_cylinders = 0;
    boot_drive_heads = 0;
    boot_drive_sectors = 0;
    boot_drive_storage_bytes = 0;

    if (mbi == NULL || (mbi->flags & (1u << 7)) == 0 || mbi->drives_addr == 0 || mbi->drives_length == 0) {
        return;
    }

    uintptr_t cursor = (uintptr_t)mbi->drives_addr;
    uintptr_t end = cursor + mbi->drives_length;
    while (cursor + sizeof(MultibootDriveInfo) <= end) {
        const MultibootDriveInfo *drive = (const MultibootDriveInfo *)cursor;
        uint64_t storage_bytes;

        if (drive->size < sizeof(MultibootDriveInfo) || cursor + drive->size > end) {
            break;
        }

        if (!boot_drive_valid || drive->drive_number == boot_drive_number) {
            boot_drive_info_available = true;
            boot_drive_number = drive->drive_number;
            boot_drive_valid = true;
            boot_drive_mode = drive->drive_mode;
            boot_drive_cylinders = drive->drive_cylinders;
            boot_drive_heads = drive->drive_heads;
            boot_drive_sectors = drive->drive_sectors;
            storage_bytes = (uint64_t)drive->drive_cylinders *
                            (uint64_t)drive->drive_heads *
                            (uint64_t)drive->drive_sectors * 512u;
            boot_drive_storage_bytes = storage_bytes > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)storage_bytes;
            return;
        }

        cursor += drive->size;
    }
}

/*
 * INT 13h AH=48h returns an EDD 3.0 device-path block.  Its interface type is
 * an 8-byte ASCII field, so a USB stick reports "USB" no matter which image
 * layout (disk, ISO or floppy) was flashed onto it.  The call is made only
 * after the video path has installed the real-mode bridge; the return value
 * says whether the block itself was present, not merely whether it was USB,
 * so an ATA disk is never mislabelled by the fallback.
 */
static bool disk_boot_edd_interface_is_usb(const MultibootInfo *mbi,
                                           bool *edd_answered) {
    volatile uint32_t *regs;
    volatile uint8_t *buf;
    volatile uint16_t *vector_slot;
    uint32_t flags;
    uint32_t i;

    *edd_answered = false;
    if (!boot_drive_valid || boot_drive_number >= 0x9Fu) {
        return false;
    }
    if (mbi == NULL ||
        !realmode_range_is_free(mbi, rm_layout[RM_L_BASE],
                                rm_layout[RM_L_STACK])) {
        return false;
    }

    realmode_configure_stack(mbi);
    realmode_install_gdt();

    regs = (volatile uint32_t *)(uintptr_t)rm_layout[RM_L_REGS];
    buf = (volatile uint8_t *)(uintptr_t)rm_layout[RM_L_BUF];
    vector_slot = (volatile uint16_t *)(void *)((uintptr_t)regs +
                                                rm_layout[RM_L_INT]);

    for (i = 0; i < 10u; ++i) {
        regs[i] = 0;
    }
    for (i = 0; i < 66u; ++i) {
        buf[i] = 0;
    }
    buf[0] = 66u; /* EDD 3.0 device-path block size */
    *(volatile uint32_t *)(void *)((uintptr_t)regs + rm_layout[RM_L_EAX]) = 0x4800u;
    *(volatile uint32_t *)(void *)((uintptr_t)regs + rm_layout[RM_L_EDX]) = boot_drive_number;
    *(volatile uint32_t *)(void *)((uintptr_t)regs + rm_layout[RM_L_ESI]) = rm_layout[RM_L_BUF];
    *(volatile uint32_t *)(void *)((uintptr_t)regs + rm_layout[RM_L_DS]) = 0;
    *vector_slot = 0x13u;

    if (realmode_call() != 0) {
        return false;
    }
    flags = *(volatile uint32_t *)(void *)((uintptr_t)regs + rm_layout[RM_L_FLAGS]);
    if ((flags & 1u) != 0) {
        return false;
    }
    if (buf[30] != 0xDDu || buf[31] != 0xBEu) {
        return false;
    }

    *edd_answered = true;
    return buf[40] == 'U' && buf[41] == 'S' && buf[42] == 'B';
}

static void detect_boot_medium_type(const MultibootInfo *mbi) {
    bool edd_answered = false;
    bool usb;

    if (!boot_drive_valid) {
        return;
    }
    usb = disk_boot_edd_interface_is_usb(mbi, &edd_answered);
    if (edd_answered) {
        boot_medium_type_known = true;
        boot_medium_is_usb = usb;
        serial_trace("INFO", usb
                              ? "boot medium: USB (INT 13h EDD device path)"
                              : "boot medium: non-USB (INT 13h EDD device path)");
    } else {
        serial_trace("INFO", "boot medium: type not reported by INT 13h EDD");
    }
}

static void serial_trace_disk_details(void) {
    if (!debug) {
        return;
    }

    serial_trace_begin("INFO");
    serial_write_string("initialize disk: drive=");
    serial_write_string("0x");
    serial_write_hex8(boot_drive_number);
    serial_write_string(" type=");
    serial_write_string(disk_physical_type_label());
    serial_write_string(" info=");
    serial_write_string(boot_drive_info_available ? "available" : "limited");
    serial_write_string("\n");

    if (boot_drive_info_available) {
        serial_trace_begin("INFO");
        serial_write_string("disk geometry: mode=");
        serial_write_uint(boot_drive_mode);
        serial_write_string(" cyl=");
        serial_write_uint(boot_drive_cylinders);
        serial_write_string(" heads=");
        serial_write_uint(boot_drive_heads);
        serial_write_string(" sectors=");
        serial_write_uint(boot_drive_sectors);
        serial_write_string(" bytes=");
        serial_write_uint(boot_drive_storage_bytes);
        serial_write_string("\n");
    }
}


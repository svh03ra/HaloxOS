// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: disk_info.c, disk info and etc.

// This repository is licensed under the GNU General Public License.

static const char *disk_physical_type_label(void) {
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


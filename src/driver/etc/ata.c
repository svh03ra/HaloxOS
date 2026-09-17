// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: ata.c, ATA PATA storage driver.

// The legacy IDE command block is 0x1F0 (primary) / 0x170 (secondary),
// control block at +0x206. Commands are polled (no IRQ), 28-bit LBA.
// The DOOM WAD stamp can live on ANY attached drive, so the driver
// exposes per-device reads (channel + device select) plus ATAPI packet
// helpers (READ CAPACITY / READ(12)) so a CD-ROM with the ISO-embedded
// WAD blob can be read too.

#define ATA_PRIMARY_IO   0x1F0
#define ATA_SECONDARY_IO 0x170
#define ATA_CTL_OFFSET   0x206

#define ATA_DEV_MASTER   0xE0
#define ATA_DEV_SLAVE    0xF0

/* Device type per (channel, drive). Sending a command a device does
 * not implement wedges the bus: an ATA disk that receives an ATAPI
 * PACKET command (or a CD-ROM that receives READ SECTORS) never raises
 * DRQ, and the caller spins to the full timeout - or worse, the
 * emulated drive hangs outright (VMware, 86Box). IDENTIFY DEVICE /
 * IDENTIFY PACKET DEVICE classify every present drive up front and
 * every command path refuses the wrong type. */
typedef enum {
    ATA_DEV_NONE = 0,
    ATA_DEV_PATA,   /* IDENTIFY DEVICE works (word0 bit15 = 0) */
    ATA_DEV_ATAPI   /* IDENTIFY PACKET DEVICE works (word0 bit15 = 1) */
} AtaDevKind;

static AtaDevKind ata_dev_kind[2 /* channels */ ][2 /* master/slave */ ];
static bool ata_devices_detected = false;

static AtaDevKind ata_kind(uint16_t io_base, uint8_t devsel_head);
static const char *ata_kind_name(AtaDevKind kind);
static bool ata_identify_dev(uint16_t io_base, uint8_t devsel_head, bool packet_cmd);
static void ata_detect_devices(void);
static void ata_select_dev(uint16_t io_base, uint8_t devsel_head);
static bool ata_wait_ready(uint16_t status_port);
static bool ata_wait_drq(uint16_t status_port);
static bool ata_pio_read_sectors_dev(uint16_t io_base, uint8_t devsel_head, uint32_t lba,
                                     uint8_t *buffer, uint8_t sectors);

/* 400ns settle delay: four alternate-status reads. Required after a
 * device select before the status register is meaningful - 86Box and
 * real hardware reject commands issued too early after the select. */
static void ata_io_delay(uint16_t io_base) {
    const uint16_t ctl_port = io_base + ATA_CTL_OFFSET;
    inb(ctl_port);
    inb(ctl_port);
    inb(ctl_port);
    inb(ctl_port);
}

static AtaDevKind ata_kind(uint16_t io_base, uint8_t devsel_head) {
    return ata_dev_kind[io_base == ATA_SECONDARY_IO ? 1 : 0]
                       [(devsel_head & 0x10u) != 0 ? 1 : 0];
}

static const char *ata_kind_name(AtaDevKind kind) {
    switch (kind) {
        case ATA_DEV_PATA: return "ATA disk";
        case ATA_DEV_ATAPI: return "ATAPI (CD-ROM)";
        default: return "absent";
    }
}

/* One IDENTIFY pass. Returns false fast when the device is absent
 * (floating bus reads 0xFF: BSY sticks) or rejects the command (ERR
 * bit set), so the detection sweep costs microseconds, not seconds. */
static bool ata_identify_dev(uint16_t io_base, uint8_t devsel_head, bool packet_cmd) {
    const uint16_t status_port = io_base + 7;
    uint16_t word0 = 0;

    ata_select_dev(io_base, devsel_head);
    if (!ata_wait_ready(status_port)) {
        return false;
    }

    outb(io_base + 2, 0);
    outb(io_base + 3, 0);
    outb(io_base + 4, 0);
    outb(io_base + 5, 0);
    outb(status_port, packet_cmd ? 0xA1 : 0xEC);

    if (!ata_wait_drq(status_port)) {
        return false;
    }
    for (int i = 0; i < 256; ++i) {
        uint16_t word = inw(io_base);
        if (i == 0) {
            word0 = word;
        }
    }

    /* General configuration word: bit 15 set = packet (ATAPI) device. */
    return packet_cmd ? ((word0 & 0x8000u) != 0) : ((word0 & 0x8000u) == 0);
}

/* Classify all four drive positions once, on the first storage access.
 * The result table gates every later command so the wrong command type
 * is never sent to a present drive. */
static void ata_detect_devices(void) {
    static const uint16_t bases[2] = { ATA_PRIMARY_IO, ATA_SECONDARY_IO };
    static const uint8_t sels[2] = { ATA_DEV_MASTER, ATA_DEV_SLAVE };

    if (ata_devices_detected) {
        return;
    }
    ata_devices_detected = true;

    for (int ch = 0; ch < 2; ++ch) {
        for (int d = 0; d < 2; ++d) {
            AtaDevKind kind = ATA_DEV_NONE;
            if (ata_identify_dev(bases[ch], sels[d], true)) {
                kind = ATA_DEV_ATAPI;
            } else if (ata_identify_dev(bases[ch], sels[d], false)) {
                kind = ATA_DEV_PATA;
            }
            ata_dev_kind[ch][d] = kind;
            serial_trace_concat("INFO", "ATA device: ", ata_kind_name(kind));
        }
    }
}

/* Select a device on a channel and settle the bus. */
static void ata_select_dev(uint16_t io_base, uint8_t devsel_head) {
    outb(io_base + 6, devsel_head);
    ata_io_delay(io_base);
}

static bool ata_wait_ready(uint16_t status_port) {
    for (int i = 0; i < 100000; ++i) {
        uint8_t status = inb(status_port);
        if ((status & 0x80u) == 0) {
            return true;
        }
    }
    return false;
}

static bool ata_wait_drq(uint16_t status_port) {
    for (int i = 0; i < 100000; ++i) {
        uint8_t status = inb(status_port);
        if ((status & 0x01u) != 0) {
            return false;
        }
        if ((status & 0x08u) != 0 && (status & 0x80u) == 0) {
            return true;
        }
    }
    return false;
}

/* LBA28 READ SECTORS.  A single command can transfer several adjacent
 * sectors, which removes repeated device-select and command-poll overhead
 * from WAD and asset streaming.  It remains strictly PIO (no DMA setup or
 * IRQ dependency), keeping it reliable on old chipsets and 86Box. */
static bool ata_pio_read_sectors_dev(uint16_t io_base, uint8_t devsel_head, uint32_t lba,
                                     uint8_t *buffer, uint8_t sectors) {
    const uint16_t status_port = io_base + 7;
    const uint8_t base_devsel = (uint8_t)(devsel_head & 0xF0u);

    ata_detect_devices();
    if (sectors == 0 || ata_kind(io_base, base_devsel) != ATA_DEV_PATA) {
        return false; /* packet devices never understand READ SECTORS */
    }

    /* Complete LBA28 device/head value, including bits 24..27. The WAD
     * layout uses smaller LBAs today, but omitting this nibble makes the
     * reader fragile on real ATA media as soon as the image grows. */
    const uint8_t devsel = (uint8_t)(base_devsel | ((lba >> 24) & 0x0Fu));

    /* Older PATA controllers and 86Box can occasionally report a transient
     * status race immediately after switching devices. Retry the complete
     * command before reporting a real media failure. */
    for (int attempt = 0; attempt < 3; ++attempt) {
        ata_select_dev(io_base, devsel);
        if (!ata_wait_ready(status_port)) {
            continue;
        }

        outb(io_base + 2, sectors);
        outb(io_base + 3, (uint8_t)(lba & 0xFFu));
        outb(io_base + 4, (uint8_t)((lba >> 8) & 0xFFu));
        outb(io_base + 5, (uint8_t)((lba >> 16) & 0xFFu));
        outb(status_port, 0x20);

        for (uint32_t sector = 0; sector < sectors; ++sector) {
            if (!ata_wait_drq(status_port)) {
                break;
            }
            for (int i = 0; i < 256; ++i) {
                uint16_t word = inw(io_base);
                size_t byte = (size_t)sector * 512u + (size_t)i * 2u;
                buffer[byte + 0] = (uint8_t)(word & 0xFFu);
                buffer[byte + 1] = (uint8_t)(word >> 8);
            }
            if ((inb(status_port) & (0x01u | 0x20u)) != 0) {
                break;
            }
            if (sector + 1u == sectors) {
                return true;
            }
        }
    }
    return false;
}

static bool ata_pio_read_sector_dev(uint16_t io_base, uint8_t devsel_head, uint32_t lba,
                                    uint8_t *buffer) {
    return ata_pio_read_sectors_dev(io_base, devsel_head, lba, buffer, 1);
}

/* Primary master read: used by the desktop layout stamp and other
 * fixed-location kernel reads. */
static bool ata_pio_read_sector(uint32_t lba, uint8_t *buffer) {
    return ata_pio_read_sector_dev(ATA_PRIMARY_IO, ATA_DEV_MASTER, lba, buffer);
}

static bool ata_pio_write_sector(uint32_t lba, const uint8_t *buffer) {
    const uint16_t io_base = ATA_PRIMARY_IO;
    const uint16_t status_port = io_base + 7;

    ata_select_dev(io_base, ATA_DEV_MASTER);
    if (!ata_wait_ready(status_port)) {
        return false;
    }

    outb(io_base + 2, 1);
    outb(io_base + 3, (uint8_t)(lba & 0xFFu));
    outb(io_base + 4, (uint8_t)((lba >> 8) & 0xFFu));
    outb(io_base + 5, (uint8_t)((lba >> 16) & 0xFFu));
    outb(status_port, 0x30);

    if (!ata_wait_drq(status_port)) {
        return false;
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t word = (uint16_t)buffer[i * 2 + 0] | ((uint16_t)buffer[i * 2 + 1] << 8);
        outw(io_base, word);
    }

    outb(status_port, 0xE7);
    return ata_wait_ready(status_port);
}

/* ---- ATAPI packet layer (CD-ROM) ------------------------------------
 * The ISO ships the DOOM WAD as a raw blob past its last volume sector,
 * so the kernel reads it with ATAPI READ(12) packets. All multi-byte
 * packet fields are big-endian per the MMC command set. */

static bool ata_packet_io(uint16_t io_base, uint8_t devsel_head,
                          const uint8_t *packet, uint16_t byte_count,
                          uint8_t *data, uint32_t data_bytes) {
    const uint16_t status_port = io_base + 7;

    ata_detect_devices();
    if (ata_kind(io_base, devsel_head) != ATA_DEV_ATAPI) {
        return false; /* PACKET to an ATA disk hangs the bus (VMware/86Box) */
    }
    ata_select_dev(io_base, devsel_head);
    if (!ata_wait_ready(status_port)) {
        return false;
    }

    outb(io_base + 1, 0);                              /* feature */
    outb(io_base + 4, (uint8_t)(byte_count & 0xFFu));  /* byte count low */
    outb(io_base + 5, (uint8_t)((byte_count >> 8) & 0xFFu));
    outb(status_port, 0xA0);                           /* PACKET */

    if (!ata_wait_drq(status_port)) {
        return false;                                  /* ready for the packet */
    }

    for (int i = 0; i < 6; ++i) {
        outw(io_base, (uint16_t)packet[i * 2 + 0] | ((uint16_t)packet[i * 2 + 1] << 8));
    }

    if (data == NULL || data_bytes == 0) {
        return ata_wait_ready(status_port);
    }

    if (!ata_wait_drq(status_port)) {
        return false;                                  /* data ready */
    }

    uint32_t words = data_bytes / 2;
    for (uint32_t i = 0; i < words; ++i) {
        uint16_t word = inw(io_base);
        data[i * 2 + 0] = (uint8_t)(word & 0xFFu);
        data[i * 2 + 1] = (uint8_t)(word >> 8);
    }

    return true;
}

/* READ CAPACITY: last logical block address + block size. */
static bool ata_packet_capacity_dev(uint16_t io_base, uint8_t devsel_head,
                                    uint32_t *last_lba, uint32_t *block_bytes) {
    static const uint8_t packet[12] = { 0x25 };
    uint8_t response[8];

    if (!ata_packet_io(io_base, devsel_head, packet, 8, response, sizeof(response))) {
        return false;
    }
    *last_lba = ((uint32_t)response[0] << 24) | ((uint32_t)response[1] << 16) |
                ((uint32_t)response[2] << 8) | (uint32_t)response[3];
    *block_bytes = ((uint32_t)response[4] << 24) | ((uint32_t)response[5] << 16) |
                   ((uint32_t)response[6] << 8) | (uint32_t)response[7];
    return *block_bytes != 0;
}

/* READ(12): read `blocks` 2048-byte CD sectors at `lba` into buffer. */
static bool ata_packet_read_dev(uint16_t io_base, uint8_t devsel_head,
                                uint32_t lba, uint16_t blocks, uint8_t *buffer) {
    uint8_t packet[12] = { 0xA8, 0 };
    packet[2] = (uint8_t)((lba >> 24) & 0xFFu);
    packet[3] = (uint8_t)((lba >> 16) & 0xFFu);
    packet[4] = (uint8_t)((lba >> 8) & 0xFFu);
    packet[5] = (uint8_t)(lba & 0xFFu);
    packet[6] = 0;
    packet[7] = 0;
    packet[8] = (uint8_t)((blocks >> 8) & 0xFFu);
    packet[9] = (uint8_t)(blocks & 0xFFu);
    packet[10] = 0;
    packet[11] = 0;
    return ata_packet_io(io_base, devsel_head, packet, (uint16_t)(blocks * 2048u),
                         buffer, (uint32_t)blocks * 2048u);
}

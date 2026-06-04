// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: ata.c, ATA PATA storage driver.

// This repository is licensed under the GNU General Public License.

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

static bool ata_pio_read_sector(uint32_t lba, uint8_t *buffer) {
    const uint16_t io_base = 0x1F0;
    const uint16_t status_port = io_base + 7;

    if (!ata_wait_ready(status_port)) {
        return false;
    }

    outb(io_base + 6, (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    outb(io_base + 2, 1);
    outb(io_base + 3, (uint8_t)(lba & 0xFFu));
    outb(io_base + 4, (uint8_t)((lba >> 8) & 0xFFu));
    outb(io_base + 5, (uint8_t)((lba >> 16) & 0xFFu));
    outb(status_port, 0x20);

    if (!ata_wait_drq(status_port)) {
        return false;
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t word = inw(io_base);
        buffer[i * 2 + 0] = (uint8_t)(word & 0xFFu);
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }

    return true;
}

static bool ata_pio_write_sector(uint32_t lba, const uint8_t *buffer) {
    const uint16_t io_base = 0x1F0;
    const uint16_t status_port = io_base + 7;

    if (!ata_wait_ready(status_port)) {
        return false;
    }

    outb(io_base + 6, (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
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


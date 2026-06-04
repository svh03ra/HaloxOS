// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: pci.c, PCI configuration driver.

// This repository is licensed under the GNU General Public License.

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       (offset & 0xFCu);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       (offset & 0xFCu);
    outl(0xCF8, address);
    outl(0xCFC, value);
}

static bool find_vga_framebuffer_bar(uint32_t *base_out) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t function = 0; function < 8; ++function) {
                uint32_t id = pci_config_read32((uint8_t)bus, slot, function, 0x00);
                uint32_t class_reg;
                uint32_t bar0;

                if (id == 0xFFFFFFFFu) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                class_reg = pci_config_read32((uint8_t)bus, slot, function, 0x08);
                if (((class_reg >> 24) & 0xFFu) != 0x03u || ((class_reg >> 16) & 0xFFu) != 0x00u) {
                    continue;
                }

                bar0 = pci_config_read32((uint8_t)bus, slot, function, 0x10);
                if ((bar0 & 0x1u) == 0 && (bar0 & ~0x0Fu) != 0) {
                    *base_out = bar0 & ~0x0Fu;
                    return true;
                }
            }
        }
    }
    return false;
}

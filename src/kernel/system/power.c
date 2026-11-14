// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: power.c, power management.

// This repository is licensed under the GNU General Public License.

#define ACPI_PM1_SCI_EN 0x0001u
#define ACPI_PM1_SLP_EN 0x2000u
#define ACPI_PM1_WAK_STS 0x8000u
/* Fixed power-button event: status bit and enable bit are both PM1 bit 8. */
#define ACPI_PM1_PWRBTN_STS 0x0100u
#define ACPI_PM1_PWRBTN_EN 0x0100u

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) AcpiRsdp;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) AcpiSdtHeader;

typedef struct {
    AcpiSdtHeader header;
    uint32_t firmware_control;
    uint32_t dsdt;
    uint8_t reserved0;
    uint8_t preferred_pm_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command_port;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pm_timer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t pm1_event_length;
    uint8_t pm1_control_length;
    uint8_t pm2_control_length;
    uint8_t pm_timer_length;
} __attribute__((packed)) AcpiFadt;

/* Generic Address Structure (ACPI 2.0+). The reset register in the FADT
 * is one of these plus the byte value to write: the firmware's own
 * platform reset, which resets the chipset as well as the CPU. */
typedef struct {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed)) AcpiGas;

/* FADT field offsets (ACPI 6.x). FLAGS is at 0x70, RESET_REG at 0x74 and
 * RESET_VALUE at 0x80; they are read by offset because an ACPI 1.0 FADT
 * (length 116) is still valid for poweroff and sizeof(AcpiFadt) only
 * covers the fields this kernel uses through PM_TMR_LEN. */
#define FADT_FLAGS_OFFSET        112u
#define FADT_RESET_REG_OFFSET    116u
#define FADT_RESET_VALUE_OFFSET  128u
#define FADT_FLAG_RESET_REG_SUP  (1u << 10)

typedef struct {
    bool available;
    bool reset_available;
    uint8_t reset_address_space;
    uint8_t reset_access_size;
    uint8_t reset_value;
    uint64_t reset_address;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint8_t pm1_event_length;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint8_t pm1_control_length;
    uint32_t smi_command_port;
    uint8_t acpi_enable;
    uint8_t slp_typa;
    uint8_t slp_typb;
    uint16_t sci_interrupt;
} AcpiPowerInfo;

static AcpiPowerInfo acpi_power;
static bool acpi_power_scanned = false;

static bool memory_equals(const char *a, const char *b, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static bool acpi_checksum_valid(const void *table, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)table;
    uint8_t sum = 0;

    if (table == NULL || length == 0) {
        return false;
    }

    for (uint32_t i = 0; i < length; ++i) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0;
}

static uint16_t read_physical_u16(uint32_t address) {
    uint16_t value;
    __asm__ volatile ("movw (%1), %0" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static const AcpiRsdp *find_rsdp_in_range(uint32_t start, uint32_t length) {
    uint32_t end = start + length;

    for (uint32_t address = start; address + sizeof(AcpiRsdp) <= end; address += 16) {
        const AcpiRsdp *rsdp = (const AcpiRsdp *)(uintptr_t)address;

        if (!memory_equals(rsdp->signature, "RSD PTR ", 8)) {
            continue;
        }
        if (!acpi_checksum_valid(rsdp, 20)) {
            continue;
        }
        if (rsdp->revision >= 2 && rsdp->length >= 36 && !acpi_checksum_valid(rsdp, rsdp->length)) {
            continue;
        }
        return rsdp;
    }

    return NULL;
}

static const AcpiRsdp *find_rsdp(void) {
    uint16_t ebda_segment = read_physical_u16(0x40E);
    uint32_t ebda_address = (uint32_t)ebda_segment << 4;
    const AcpiRsdp *rsdp = NULL;

    if (ebda_address >= 0x80000u && ebda_address < 0xA0000u) {
        rsdp = find_rsdp_in_range(ebda_address, 1024);
        if (rsdp != NULL) {
            return rsdp;
        }
    }

    return find_rsdp_in_range(0xE0000u, 0x20000u);
}

static bool acpi_sdt_valid(const AcpiSdtHeader *header) {
    if (header == NULL || header->length < sizeof(AcpiSdtHeader)) {
        return false;
    }
    return acpi_checksum_valid(header, header->length);
}

static const AcpiSdtHeader *acpi_find_table(const AcpiRsdp *rsdp, const char *signature) {
    const AcpiSdtHeader *rsdt;
    const uint32_t *entries;
    uint32_t entry_count;

    if (rsdp == NULL || rsdp->rsdt_address == 0) {
        return NULL;
    }

    rsdt = (const AcpiSdtHeader *)(uintptr_t)rsdp->rsdt_address;
    if (!memory_equals(rsdt->signature, "RSDT", 4) || !acpi_sdt_valid(rsdt)) {
        return NULL;
    }

    entries = (const uint32_t *)((const uint8_t *)rsdt + sizeof(AcpiSdtHeader));
    entry_count = (rsdt->length - sizeof(AcpiSdtHeader)) / sizeof(uint32_t);
    for (uint32_t i = 0; i < entry_count; ++i) {
        const AcpiSdtHeader *header;

        if (entries[i] == 0) {
            continue;
        }
        header = (const AcpiSdtHeader *)(uintptr_t)entries[i];
        if (memory_equals(header->signature, signature, 4) && acpi_sdt_valid(header)) {
            return header;
        }
    }

    return NULL;
}

static uint32_t aml_read_pkg_length(const uint8_t *ptr, const uint8_t *end, size_t *consumed_out) {
    uint8_t lead;
    uint8_t byte_count;
    uint32_t length;

    if (ptr >= end) {
        *consumed_out = 0;
        return 0;
    }

    lead = ptr[0];
    byte_count = (uint8_t)(lead >> 6);
    length = byte_count == 0 ? (lead & 0x3Fu) : (lead & 0x0Fu);
    *consumed_out = 1;

    for (uint8_t i = 0; i < byte_count; ++i) {
        if (ptr + 1 + i >= end) {
            *consumed_out = 0;
            return 0;
        }
        length |= (uint32_t)ptr[1 + i] << (4 + i * 8);
        ++(*consumed_out);
    }

    return length;
}

static bool aml_read_integer_byte(const uint8_t **cursor_inout, const uint8_t *end, uint8_t *value_out) {
    const uint8_t *cursor = *cursor_inout;
    uint8_t opcode;

    if (cursor >= end) {
        return false;
    }

    opcode = *cursor++;
    switch (opcode) {
        case 0x00:
            *value_out = 0;
            break;
        case 0x01:
            *value_out = 1;
            break;
        case 0x0A:
            if (cursor >= end) {
                return false;
            }
            *value_out = *cursor++;
            break;
        case 0x0B:
            if (cursor + 1 >= end) {
                return false;
            }
            *value_out = cursor[0];
            cursor += 2;
            break;
        case 0x0C:
            if (cursor + 3 >= end) {
                return false;
            }
            *value_out = cursor[0];
            cursor += 4;
            break;
        case 0x0E:
            if (cursor + 7 >= end) {
                return false;
            }
            *value_out = cursor[0];
            cursor += 8;
            break;
        case 0xFF:
            *value_out = 0xFF;
            break;
        default:
            if (opcode <= 0x3F) {
                *value_out = opcode;
                break;
            }
            return false;
    }

    *cursor_inout = cursor;
    return true;
}

static bool acpi_dsdt_find_s5(const AcpiSdtHeader *dsdt, uint8_t *slp_typa_out, uint8_t *slp_typb_out) {
    const uint8_t *table;
    const uint8_t *end;

    if (dsdt == NULL || !memory_equals(dsdt->signature, "DSDT", 4) || !acpi_sdt_valid(dsdt)) {
        return false;
    }

    table = (const uint8_t *)dsdt + sizeof(AcpiSdtHeader);
    end = (const uint8_t *)dsdt + dsdt->length;
    for (const uint8_t *name = table; name + 5 < end; ++name) {
        const uint8_t *package;
        const uint8_t *cursor;
        size_t package_length_bytes = 0;
        uint32_t package_length;
        uint8_t element_count;
        uint8_t slp_typa = 0;
        uint8_t slp_typb = 0;
        bool has_name_op = false;

        if (name[0] != '_' || name[1] != 'S' || name[2] != '5' || name[3] != '_') {
            continue;
        }

        if (name >= table + 1 && name[-1] == 0x08) {
            has_name_op = true;
        } else if (name >= table + 2 && name[-2] == 0x08 && name[-1] == '\\') {
            has_name_op = true;
        }
        if (!has_name_op) {
            continue;
        }

        package = name + 4;
        if (package >= end || package[0] != 0x12) {
            continue;
        }

        cursor = package + 1;
        package_length = aml_read_pkg_length(cursor, end, &package_length_bytes);
        if (package_length == 0 || package_length_bytes == 0) {
            continue;
        }
        cursor += package_length_bytes;
        if (cursor >= end) {
            continue;
        }

        element_count = *cursor++;
        if (element_count < 2) {
            continue;
        }

        if (!aml_read_integer_byte(&cursor, end, &slp_typa)) {
            continue;
        }
        if (!aml_read_integer_byte(&cursor, end, &slp_typb)) {
            continue;
        }

        *slp_typa_out = slp_typa;
        *slp_typb_out = slp_typb;
        return true;
    }

    return false;
}

static uint32_t acpi_fadt_dsdt_address(const AcpiFadt *fadt) {
    if (fadt->dsdt != 0) {
        return fadt->dsdt;
    }

    if (fadt->header.length >= 148) {
        uint64_t x_dsdt = *(const uint64_t *)((const uint8_t *)fadt + 140);
        if (x_dsdt <= 0xFFFFFFFFull) {
            return (uint32_t)x_dsdt;
        }
    }

    return 0;
}

/* The FADT RESET_REG describes the platform's own reset mechanism. It is
 * usually a byte write to 0xCF9, which is a full chipset reset: unlike an
 * INIT pulse, USB controllers and firmware-owned state go down with the
 * CPU. Parse it separately from the S5 data so a machine without a usable
 * _S5 object can still restart through ACPI. */
static void fadt_read_reset_register(const AcpiFadt *fadt) {
    const uint8_t *raw;
    AcpiGas gas;
    uint8_t *gas_bytes = (uint8_t *)(void *)&gas;
    uint32_t flags = 0;

    acpi_power.reset_available = false;
    if (fadt == NULL || fadt->header.length < FADT_RESET_VALUE_OFFSET + 1u) {
        return;
    }
    if (fadt->header.revision < 2u) {
        return;
    }

    raw = (const uint8_t *)fadt;
    for (uint32_t i = 0; i < 4; ++i) {
        ((uint8_t *)(void *)&flags)[i] = raw[FADT_FLAGS_OFFSET + i];
    }
    if ((flags & FADT_FLAG_RESET_REG_SUP) == 0) {
        return;
    }
    for (uint32_t i = 0; i < sizeof(gas); ++i) {
        gas_bytes[i] = raw[FADT_RESET_REG_OFFSET + i];
    }
    if (gas.address == 0 || gas.access_size == 0 || gas.access_size > 4 ||
        gas.bit_width == 0 || gas.bit_width > 64 || gas.address_space > 2) {
        return;
    }

    acpi_power.reset_available = true;
    acpi_power.reset_address_space = gas.address_space;
    acpi_power.reset_access_size = gas.access_size;
    acpi_power.reset_value = raw[FADT_RESET_VALUE_OFFSET];
    acpi_power.reset_address = gas.address;
}

static void init_acpi_power(void) {
    const AcpiRsdp *rsdp;
    const AcpiFadt *fadt;
    const AcpiSdtHeader *dsdt;
    uint32_t dsdt_address;
    uint8_t slp_typa = 0;
    uint8_t slp_typb = 0;

    if (acpi_power_scanned) {
        return;
    }

    acpi_power_scanned = true;
    memset_local(&acpi_power, 0, sizeof(acpi_power));
    serial_trace("INFO", "scan ACPI power tables");

    rsdp = find_rsdp();
    if (rsdp == NULL) {
        serial_trace("WARNING", "ACPI RSDP not found");
        return;
    }

    fadt = (const AcpiFadt *)acpi_find_table(rsdp, "FACP");
    if (fadt == NULL || fadt->header.length < sizeof(AcpiFadt)) {
        serial_trace("WARNING", "ACPI FADT unavailable");
        return;
    }

    fadt_read_reset_register(fadt);
    if (acpi_power.reset_available) {
        serial_trace("INFO", "ACPI reset register ready");
        serial_trace_hex_value("INFO", "ACPI reset address low",
                               (uint32_t)acpi_power.reset_address);
        serial_trace_hex_value("INFO", "ACPI reset address high",
                               (uint32_t)(acpi_power.reset_address >> 32));
        serial_trace_hex_value("INFO", "ACPI reset address space",
                               acpi_power.reset_address_space);
        serial_trace_hex_value("INFO", "ACPI reset access size",
                               acpi_power.reset_access_size);
        serial_trace_hex_value("INFO", "ACPI reset value",
                               acpi_power.reset_value);
    } else {
        serial_trace("WARNING", "ACPI reset register unavailable");
    }

    dsdt_address = acpi_fadt_dsdt_address(fadt);
    if (dsdt_address == 0) {
        serial_trace("WARNING", "ACPI DSDT address unavailable");
        return;
    }

    dsdt = (const AcpiSdtHeader *)(uintptr_t)dsdt_address;
    if (!acpi_dsdt_find_s5(dsdt, &slp_typa, &slp_typb)) {
        serial_trace("WARNING", "ACPI _S5 object unavailable");
        return;
    }

    if (fadt->pm1a_control_block == 0 || fadt->pm1_control_length < 2) {
        serial_trace("WARNING", "ACPI PM1 control block unavailable");
        return;
    }

    acpi_power.pm1a_event_block = fadt->pm1a_event_block;
    acpi_power.pm1b_event_block = fadt->pm1b_event_block;
    acpi_power.pm1_event_length = fadt->pm1_event_length;
    acpi_power.pm1a_control_block = fadt->pm1a_control_block;
    acpi_power.pm1b_control_block = fadt->pm1b_control_block;
    acpi_power.pm1_control_length = fadt->pm1_control_length;
    acpi_power.smi_command_port = fadt->smi_command_port;
    acpi_power.acpi_enable = fadt->acpi_enable;
    acpi_power.sci_interrupt = fadt->sci_interrupt;
    acpi_power.slp_typa = slp_typa;
    acpi_power.slp_typb = slp_typb;
    acpi_power.available = true;

    serial_trace("INFO", "ACPI poweroff data ready");
    serial_trace_hex_value("INFO", "ACPI RSDP address", (uint32_t)(uintptr_t)rsdp);
    serial_trace_hex_value("INFO", "ACPI RSDT address", rsdp->rsdt_address);
    serial_trace_hex_value("INFO", "ACPI FADT address", (uint32_t)(uintptr_t)fadt);
    serial_trace_hex_value("INFO", "ACPI DSDT address", dsdt_address);
    serial_trace_hex_value("INFO", "ACPI SMI command port", acpi_power.smi_command_port);
    serial_trace_hex_value("INFO", "ACPI enable value", acpi_power.acpi_enable);
    serial_trace_hex_value("INFO", "ACPI PM1a event block", acpi_power.pm1a_event_block);
    serial_trace_hex_value("INFO", "ACPI PM1b event block", acpi_power.pm1b_event_block);
    serial_trace_hex_value("INFO", "ACPI PM1a control block", acpi_power.pm1a_control_block);
    serial_trace_hex_value("INFO", "ACPI PM1b control block", acpi_power.pm1b_control_block);
    serial_trace_hex_value("INFO", "ACPI PM1 event length", acpi_power.pm1_event_length);
    serial_trace_hex_value("INFO", "ACPI PM1 control length", acpi_power.pm1_control_length);
    serial_trace_hex_value("INFO", "ACPI S5 SLP_TYPa", acpi_power.slp_typa);
    serial_trace_hex_value("INFO", "ACPI S5 SLP_TYPb", acpi_power.slp_typb);
}

static bool acpi_enable_if_needed(void) {
    uint16_t pm1a_control = inw((uint16_t)acpi_power.pm1a_control_block);

    if ((pm1a_control & ACPI_PM1_SCI_EN) != 0) {
        serial_trace("INFO", "ACPI already enabled (SCI_EN set)");
        return true;
    }

    if (acpi_power.smi_command_port == 0 ||
        acpi_power.smi_command_port > 0xFFFFu ||
        acpi_power.acpi_enable == 0) {
        serial_trace("WARNING", "ACPI SMI command port unavailable, cannot enable");
        return false;
    }

    serial_trace_hex_value("INFO", "ACPI PM1a control before enable", pm1a_control);
    serial_trace("INFO", "ACPI enable through SMI command port");
    outb((uint16_t)acpi_power.smi_command_port, acpi_power.acpi_enable);
    for (uint32_t i = 0; i < 1000000u; ++i) {
        if ((inw((uint16_t)acpi_power.pm1a_control_block) & ACPI_PM1_SCI_EN) != 0) {
            serial_trace_hex_value("INFO", "ACPI PM1a control after enable",
                                   inw((uint16_t)acpi_power.pm1a_control_block));
            return true;
        }
        io_wait();
    }

    serial_trace_hex_value("WARNING", "ACPI enable timed out, PM1a control",
                           inw((uint16_t)acpi_power.pm1a_control_block));
    return (inw((uint16_t)acpi_power.pm1a_control_block) & ACPI_PM1_SCI_EN) != 0;
}

static void wait_for_power_transition(uint32_t ticks) {
    uint32_t start = timer_ticks;

    if (!timer_irq_verified) {
        /* No PIT tick means HLT would never wake.  Keep the shutdown
         * path bounded with a port-0x80 delay instead. */
        uint32_t spins = ticks * 10000u;
        while (spins-- > 0) {
            io_wait();
        }
        return;
    }

    while ((uint32_t)(timer_ticks - start) < ticks) {
        cpu_halt_once();
    }
}

static void acpi_clear_wake_status(void) {
    if (acpi_power.pm1_event_length < 4) {
        return;
    }

    if (acpi_power.pm1a_event_block != 0 && acpi_power.pm1a_event_block <= 0xFFFFu) {
        outw((uint16_t)acpi_power.pm1a_event_block, ACPI_PM1_WAK_STS);
    }
    if (acpi_power.pm1b_event_block != 0 && acpi_power.pm1b_event_block <= 0xFFFFu) {
        outw((uint16_t)acpi_power.pm1b_event_block, ACPI_PM1_WAK_STS);
    }
}

/* ---- ACPI power button (fixed hardware event) --------------------------
 *
 * Arming the physical power button is what turns a press on a real
 * machine into the graceful shutdown state ("Shutting down..." screen,
 * boot-log flush, ACPI S5) instead of the firmware's instant hard cut -
 * the same request the Power app's Shutdown button and QEMU's
 * `system_powerdown` make, honored on the boot menu, the login screen
 * and the desktop alike: the SCI interrupt arrives whichever state is
 * live, and the state switch itself is what the render loop and the
 * poweroff attempt react to.
 */

/* Defined further down in this fragment. */
static void shutdown_system(void);
/* Defined with the native xHCI driver later in this translation unit. */
static void xhci_poweroff_handoff(void);

/* The PM1 event blocks we track (bit 8 = PWRBTN). Both status and enable
 * registers are 16-bit; with PM1_EVT_LEN >= 4 the enable register lives
 * at event_block + 2 (the layout QEMU, PIIX and ICH all use). */
static bool acpi_button_pm1_sane(void) {
    return acpi_power.available && acpi_power.pm1_event_length >= 4 &&
           acpi_power.pm1a_event_block != 0 &&
           acpi_power.pm1a_event_block <= 0xFFFFu;
}

/* SCI ISR (installed on the FADT's IRQ line, 9 on QEMU/PIIX, sometimes
 * 10 or 11 elsewhere). Everything currently pending is cleared - the
 * SCI is level-triggered, so an event left set would re-assert the line
 * forever - and the power button becomes a shutdown request. Called from
 * interrupts.asm; must keep its external name. */
void acpi_sci_from_isr(void) {
    bool button = false;

    if (!acpi_button_pm1_sane()) {
        return;
    }

    {
        uint16_t status = inw((uint16_t)acpi_power.pm1a_event_block);
        outw((uint16_t)acpi_power.pm1a_event_block, status);
        if ((status & ACPI_PM1_PWRBTN_STS) != 0) {
            button = true;
        }
    }
    if (acpi_power.pm1b_event_block != 0 && acpi_power.pm1b_event_block <= 0xFFFFu) {
        uint16_t status = inw((uint16_t)acpi_power.pm1b_event_block);
        outw((uint16_t)acpi_power.pm1b_event_block, status);
        if ((status & ACPI_PM1_PWRBTN_STS) != 0) {
            button = true;
        }
    }

    if (button && system_state != STATE_SHUTDOWN) {
        serial_trace("INFO", "ACPI power button pressed - shutting down");
        shutdown_system();
    }
}

/* Called from kernel_main once the IDT and the PIC are up: parses the
 * ACPI tables, switches the chipset into ACPI mode, clears stale fixed
 * events, arms PWRBTN_EN and hooks the SCI. On a machine without usable
 * ACPI the button simply keeps its firmware behavior (a hard cut), and
 * the power-off paths are unchanged. */
static void acpi_power_button_init(void) {
    uint16_t enable_value;
    uint8_t pic_mask;

    init_acpi_power();
    if (!acpi_button_pm1_sane()) {
        serial_trace("WARNING", "ACPI PM1 event block unusable: physical power button not armed");
        return;
    }
    if (!acpi_enable_if_needed()) {
        serial_trace("WARNING", "ACPI could not be enabled: physical power button not armed");
        return;
    }

    /* Drop stale fixed events before the line is unmasked, so an old
     * pending press does not fire the moment we arm. */
    outw((uint16_t)acpi_power.pm1a_event_block,
         (uint16_t)inw((uint16_t)acpi_power.pm1a_event_block));
    if (acpi_power.pm1b_event_block != 0 && acpi_power.pm1b_event_block <= 0xFFFFu) {
        outw((uint16_t)acpi_power.pm1b_event_block,
             (uint16_t)inw((uint16_t)acpi_power.pm1b_event_block));
    }

    enable_value = inw((uint16_t)acpi_power.pm1a_event_block + 2);
    enable_value |= ACPI_PM1_PWRBTN_EN;
    outw((uint16_t)acpi_power.pm1a_event_block + 2, enable_value);
    if (acpi_power.pm1b_event_block != 0 && acpi_power.pm1b_event_block <= 0xFFFFu) {
        enable_value = inw((uint16_t)acpi_power.pm1b_event_block + 2);
        enable_value |= ACPI_PM1_PWRBTN_EN;
        outw((uint16_t)acpi_power.pm1b_event_block + 2, enable_value);
    }

    /* Hook the SCI's IRQ line. 9/10/11 are the only lines with a stub
     * (every real platform puts the SCI there); anything else leaves the
     * event disabled rather than firing into a default stub. */
    switch (acpi_power.sci_interrupt) {
        case 9:
            set_idt_gate(41, irq9_stub, 0x8E);
            break;
        case 10:
            set_idt_gate(42, irq10_stub, 0x8E);
            break;
        case 11:
            set_idt_gate(43, irq11_stub, 0x8E);
            break;
        default:
            serial_trace_hex_value("WARNING",
                                   "ACPI SCI on an unsupported IRQ line, power button not armed",
                                   acpi_power.sci_interrupt);
            outw((uint16_t)acpi_power.pm1a_event_block + 2,
                 (uint16_t)(inw((uint16_t)acpi_power.pm1a_event_block + 2) & ~ACPI_PM1_PWRBTN_EN));
            return;
    }

    /* Handler installed, so the line can be opened: all candidates are
     * slave lines, unmask on PIC2 (and the cascade is already open). */
    pic_mask = inb(0xA1);
    pic_mask &= (uint8_t)~(1u << (acpi_power.sci_interrupt - 8));
    outb(0xA1, pic_mask);

    serial_trace_uint_value("INFO", "ACPI power button armed on IRQ", acpi_power.sci_interrupt);
}


static bool attempt_acpi_poweroff(void) {
    uint16_t pm1a_value;
    uint16_t pm1b_value;

    init_acpi_power();
    if (!acpi_power.available) {
        return false;
    }

    if (acpi_power.pm1a_control_block > 0xFFFFu ||
        (acpi_power.pm1b_control_block != 0 && acpi_power.pm1b_control_block > 0xFFFFu)) {
        serial_trace("WARNING", "ACPI PM1 control block is not system I/O");
        return false;
    }

    if (!acpi_enable_if_needed()) {
        serial_trace("WARNING", "ACPI could not be enabled");
        return false;
    }

    serial_trace("INFO", "ACPI S5 poweroff I/O sequence");
    serial_trace_hex_value("INFO", "ACPI PM1a control before S5",
                           inw((uint16_t)acpi_power.pm1a_control_block));
    acpi_clear_wake_status();
    pm1a_value = (uint16_t)(((uint16_t)acpi_power.slp_typa << 10) | ACPI_PM1_SLP_EN);
    pm1b_value = (uint16_t)(((uint16_t)acpi_power.slp_typb << 10) | ACPI_PM1_SLP_EN);
    serial_trace_hex_value("INFO", "ACPI S5 PM1a write value", pm1a_value);
    outw((uint16_t)acpi_power.pm1a_control_block, pm1a_value);
    if (acpi_power.pm1b_control_block != 0) {
        serial_trace_hex_value("INFO", "ACPI S5 PM1b write value", pm1b_value);
        outw((uint16_t)acpi_power.pm1b_control_block, pm1b_value);
    }

    wait_for_power_transition(TIMER_HZ);

    serial_trace_hex_value("WARNING", "ACPI S5 poweroff failed, PM1a control",
                           inw((uint16_t)acpi_power.pm1a_control_block));
    return false;
}

static void attempt_legacy_poweroff_ports(void) {
    serial_trace("INFO", "legacy emulator poweroff I/O sequence");
    serial_trace("INFO", "write port 0x0604 <- 0x2000 (Bochs/QEMU old)");
    outw(0x604, 0x2000);
    serial_trace("INFO", "write port 0xB004 <- 0x2000 (VirtualBox old)");
    outw(0xB004, 0x2000);
    serial_trace("INFO", "write port 0x4004 <- 0x3400 (old newer QEMU)");
    outw(0x4004, 0x3400);
}

static bool poweroff_handoff_done;

static bool attempt_poweroff(void) {
    /* The trace outlives the machine: shut the xHCI controller down before
     * the S5 sequence, because standby power can preserve OSOWNED across a
     * shutdown just as it does across a warm reset.  The hand-off flushes
     * the boot log with the "power off" reason before it detaches the
     * medium, so a failed S5 still leaves the final trace on disk. */
    if (!poweroff_handoff_done) {
        poweroff_handoff_done = true;
        xhci_poweroff_handoff();
    }
    if (attempt_acpi_poweroff()) {
        return true;
    }

    attempt_legacy_poweroff_ports();
    wait_for_power_transition(TIMER_HZ);

    return false;
}

static void shutdown_system(void) {
    serial_trace("INFO", "shutdown requested");
    shutdown_pending = true;
    shutdown_poweroff_failed = false;
    system_state = STATE_SHUTDOWN;
}

/* Bounded settle time for a reset that should be immediate. Port 0x80
 * I/O is not optimised away and gives the chipset time to assert
 * RESET#/INIT# before the next fallback runs. */
static void reset_settle(uint32_t loops) {
    while (loops-- > 0) {
        io_wait();
        __asm__ volatile ("pause");
    }
}

/* Preferred reset: the address the firmware published in the FADT. This
 * is a full platform reset on the boards that provide it, so the chipset
 * and the USB controllers are reset with the CPU instead of surviving an
 * INIT-only pulse. */
static void try_acpi_reset(void) {
    uint64_t address;
    uint8_t value;

    init_acpi_power();
    if (!acpi_power.reset_available) {
        serial_trace("INFO", "restart: ACPI reset register unavailable");
        return;
    }

    address = acpi_power.reset_address;
    value = acpi_power.reset_value;
    serial_trace("INFO", "restart: ACPI reset register write");
    serial_trace_hex_value("INFO", "restart: reset address low", (uint32_t)address);
    serial_trace_hex_value("INFO", "restart: reset address high",
                           (uint32_t)(address >> 32));
    serial_trace_hex_value("INFO", "restart: reset value", value);

    if (acpi_power.reset_address_space == 0) {
        volatile uint8_t *base = (volatile uint8_t *)(uintptr_t)address;

        if (acpi_power.reset_access_size == 1) {
            *base = value;
        } else if (acpi_power.reset_access_size == 2) {
            *(volatile uint16_t *)(void *)base = (uint16_t)value;
        } else if (acpi_power.reset_access_size == 3) {
            *(volatile uint32_t *)(void *)base = (uint32_t)value;
        } else {
            *(volatile uint64_t *)(void *)base = (uint64_t)value;
        }
    } else if (acpi_power.reset_address_space == 1) {
        uint16_t port = (uint16_t)address;

        if (acpi_power.reset_access_size == 1) {
            outb(port, value);
        } else if (acpi_power.reset_access_size == 2) {
            outw(port, (uint16_t)value);
        } else if (acpi_power.reset_access_size == 3) {
            outl(port, (uint32_t)value);
        } else {
            serial_trace("WARNING", "restart: ACPI reset I/O width unsupported");
            return;
        }
    } else if (acpi_power.reset_address_space == 2) {
        uint32_t config = (uint32_t)address;

        outl(0xCF8, 0x80000000u | (config & 0xFFFFFFFCu));
        if (acpi_power.reset_access_size == 1) {
            outb((uint16_t)(0xCFC + (config & 3u)), value);
        } else if (acpi_power.reset_access_size == 2) {
            outw((uint16_t)(0xCFC + (config & 2u)), (uint16_t)value);
        } else if (acpi_power.reset_access_size == 3) {
            outl(0xCFC, (uint32_t)value);
        } else {
            serial_trace("WARNING", "restart: ACPI reset PCI width unsupported");
            return;
        }
    } else {
        serial_trace("WARNING", "restart: ACPI reset address space unsupported");
        return;
    }

    reset_settle(100000);
    serial_trace("WARNING", "restart: ACPI reset did not take effect");
}

/* Reset Control Register: bit 1 selects a hard reset, bit 2 starts the
 * CPU reset and bit 3 requests the full chipset reset. Writing only the
 * CPU-reset value can leave the chipset (and an OS-owned USB controller)
 * alive, which is exactly what makes the next firmware boot hang. */
static void try_cf9_reset(void) {
    serial_trace("INFO", "restart: port 0xCF9 full reset");
    outb(0xCF9, 0x02);
    reset_settle(1000);
    outb(0xCF9, 0x06);
    reset_settle(1000);
    outb(0xCF9, 0x0E);
    reset_settle(100000);
    serial_trace("WARNING", "restart: port 0xCF9 reset did not take effect");
}

/* Keyboard controller pulse. Not present on a USB-only board, so the
 * status is checked first: 0xFF means nothing decoded the port and the
 * pulse would be meaningless. */
static void try_8042_reset(void) {
    uint32_t timeout = 100000u;
    uint8_t status;

    do {
        status = inb(0x64);
        --timeout;
    } while ((status & 0x02) != 0 && timeout != 0);

    serial_trace_hex_value("INFO", "restart: 8042 status", status);
    if (status == 0xFFu) {
        serial_trace("INFO", "restart: no 8042 present");
        return;
    }
    if (timeout == 0) {
        serial_trace("WARNING", "restart: 8042 input buffer still busy");
        return;
    }

    serial_trace("INFO", "restart: 8042 pulse reset");
    outb(0x64, 0xFE);
    reset_settle(100000);
    serial_trace("WARNING", "restart: 8042 reset did not take effect");
}

/* System Control Port A INIT pulse. Bit 0 requests INIT; bit 1 is the
 * A20 gate. The previous code wrote 0x01, clearing A20 in the same write,
 * so a CPU that came back without a chipset reset could hang before the
 * firmware re-enabled it. Keep A20 on. */
static void try_port92_reset(void) {
    uint8_t value = inb(0x92);

    serial_trace_hex_value("INFO", "restart: port 0x92 before reset", value);
    value |= 0x03u;
    serial_trace_hex_value("INFO", "restart: port 0x92 write", value);
    outb(0x92, value);
    reset_settle(100000);
    serial_trace("WARNING", "restart: port 0x92 INIT did not reset the machine");
}

static void restart_system(void) {
    serial_trace("INFO", "restart requested");
    /* The trace survives the reset: flush the boot log before the
     * machine goes down, then leave the xHCI controller in the state
     * firmware expects on the other side.  A warm reset that keeps PCI
     * state must not keep OSOWNED set, or the next boot's firmware skips
     * USB initialization and the kernel finds no HID slots. */
    xhci_restart_handoff();

    /* A firmware SMI or an interrupt between the reset stages can turn a
     * fallback write into a resume instead of a reset. */
    __asm__ volatile ("cli");
    serial_trace("INFO", "restart: interrupts disabled");

    try_acpi_reset();
    try_cf9_reset();
    try_8042_reset();
    try_port92_reset();

    /* Last resort: a null IDT and an interrupt triple fault, which the
     * chipset turns into a platform reset on any x86 machine. */
    serial_trace("WARNING", "restart: platform reset failed, triple fault reset");
    {
        static const uint16_t null_idt[5] = {0, 0, 0, 0, 0};
        __asm__ volatile (
            "lidt %0\n"
            "int $0x03\n"
            :
            : "m"(null_idt)
            : "memory"
        );
    }

    serial_trace("ERROR", "restart: triple fault reset failed");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

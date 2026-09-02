// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: power.c, power management.

// This repository is licensed under the GNU General Public License.

#define ACPI_PM1_SCI_EN 0x0001u
#define ACPI_PM1_SLP_EN 0x2000u
#define ACPI_PM1_WAK_STS 0x8000u

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

typedef struct {
    bool available;
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

static bool attempt_poweroff(void) {
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

static void restart_system(void) {
    uint32_t timeout;
    uint8_t status;

    serial_trace("INFO", "restart requested");

    /*
     * Stage 1: keyboard controller (8042) pulse-reset. On some real
     * machines there is no 8042 at all (USB-only board) and port 0x64
     * reads 0xFF forever, so the input-buffer wait is bounded by timeout.
     */
    timeout = 100000;
    do {
        status = inb(0x64);
        --timeout;
    } while ((status & 0x02) != 0 && timeout > 0);

    serial_trace_hex_value("INFO", "8042 status register", status);
    if (timeout == 0) {
        serial_trace("WARNING", "8042 input buffer wait timed out");
    } else {
        serial_trace("INFO", "8042 pulse reset");
        outb(0x64, 0xFE);
    }

    /* Give the pulse a moment to take effect before further attempts. */
    timeout = 1000000;
    while (timeout > 0) {
        --timeout;
    }

    /*
     * Stage 2: System Control Port A fast reset (AT+ machines).
     */
    status = inb(0x92);
    serial_trace_hex_value("INFO", "port 0x92 value before reset", status);
    serial_trace("INFO", "port 0x92 fast reset");
    outb(0x92, 0x01);

    timeout = 1000000;
    while (timeout > 0) {
        --timeout;
    }

    /*
     * Stage 3: last resort, load a null IDT and raise an interrupt, which
     * triple faults and forces the CPU to reset on any x86 machine.
     */
    serial_trace("WARNING", "reset not triggered, triple fault reset");
    {
        static const uint16_t null_idt[3] = {0, 0, 0};
        __asm__ volatile (
            "lidt %0\n"
            "int $0x03\n"
            :
            : "m"(null_idt)
            : "memory"
        );
    }

    serial_trace("ERROR", "triple fault reset failed");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

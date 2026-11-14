// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: usb_uhci.c, USB 1.1 input through a UHCI host controller.

// This repository is licensed under the GNU General Public License.

/*
 * UHCI is the controller a USB 2.0 machine's keyboard is usually attached to.
 *
 * A USB 2.0 chipset pairs one EHCI block with one or more UHCI companions, and
 * the hardware routes every full or low-speed device on a shared port to a
 * companion: the EHCI's port then reports "owned by companion" and never sees
 * the device at all.  Keyboards and mice are almost always full or low-speed,
 * so on those machines - which is every chipset from the late 1990s up to the
 * USB 3.0 era, and a number of laptops with a USB 2.0-only chipset - this is
 * the driver that actually receives the keys.  The same controller is also
 * the only USB host on a machine older than USB 2.0.
 *
 * The registers live in I/O space rather than MMIO.  Two schedules matter:
 * a frame list of 1024 pointers, and within it queue heads whose element
 * pointers name a chain of transfer descriptors.  Everything is scheduled
 * from the frame list here (every frame entry points at the head of the one
 * queue-head chain), which is legal at any size and keeps the controller
 * scanning one list instead of many.
 *
 * Transfer descriptors are 16 bytes with software-maintained data toggles, so
 * the toggle of the interrupt endpoint is tracked here rather than by the
 * hardware.  Control transfers are the usual setup/data/statuschain; a HID
 * report is a single interrupt IN descriptor that is re-armed as soon as it
 * completes.  Reports go to the same decoder as the xHCI and EHCI drivers.
 */

#define UHCI_DEVICES            2u      /* one keyboard and one mouse each */
#define UHCI_TD_MAX             4u      /* 3 for a control transfer, 1 for HID */
#define UHCI_QH_DWORDS          4u
#define UHCI_TD_DWORDS          4u
#define UHCI_FRAME_LIST_SIZE    1024u   /* 4 KB, the only size we program */
/*
 * How many port registers to probe.  A UHCI block has two to six ports (the
 * larger numbers on the server chipsets), and the count is not in a register
 * this driver can read, so the ports are probed one by one and an address
 * that does not answer as a port is skipped: six is the largest number any
 * UHCI block implements, and a register past the last one fails the port test
 * rather than reporting a device.
 */
#define UHCI_MAX_PORTS          6u

/* Registers, relative to the I/O base. */
#define UHCI_REG_USBCMD         0x00u
#define UHCI_REG_USBSTS         0x02u
#define UHCI_REG_USBINTR        0x04u
#define UHCI_REG_FRNUM          0x06u
#define UHCI_REG_FLBASEADD      0x08u
#define UHCI_REG_SOF            0x0Cu
#define UHCI_REG_PORTSC1        0x10u

#define UHCI_CMD_RUN            0x0001u
#define UHCI_CMD_HCRESET        0x0002u
#define UHCI_CMD_GRESET         0x0004u
#define UHCI_CMD_CF             0x0040u   /* configuration flag (software) */
#define UHCI_CMD_MAXP           0x0080u   /* 64-byte packets on the full-speed bus */

#define UHCI_STS_HCH            0x0020u

#define UHCI_PORTSC_CCS         0x0001u
#define UHCI_PORTSC_CSC         0x0002u
#define UHCI_PORTSC_PE          0x0004u
#define UHCI_PORTSC_PEC         0x0008u
#define UHCI_PORTSC_LSDA        0x0100u   /* low-speed device attached */
#define UHCI_PORTSC_PR          0x0200u
#define UHCI_PORTSC_OCC         0x0800u
#define UHCI_PORTSC_SUSP        0x1000u
/*
 * The port register is 16 bits and only some of it may be written back:
 *
 *   - the change bits (connect, enable, over-current) clear when written as
 *     one, so a plain read-modify-write would lose them:
 *   - the reserved bits must be written as zero, and several of them read as
 *     one on real parts (bit 7 is the one this driver's port probe keys on),
 *     so echoing a read value puts ones where the specification wants zeroes.
 *
 * Only the state bits this driver actually owns are ever written; a raw read
 * value is never sent back to the port.
 */
#define UHCI_PORTSC_CHANGE      (UHCI_PORTSC_CSC | UHCI_PORTSC_PEC | \
                                 UHCI_PORTSC_OCC)
#define UHCI_PORTSC_OWNED       (UHCI_PORTSC_PE | UHCI_PORTSC_PR | \
                                 UHCI_PORTSC_SUSP)

/* Link pointer flags, both in the frame list and inside queue heads. */
#define UHCI_LINK_TERMINATE     0x00000001u
#define UHCI_LINK_QH            0x00000002u
#define UHCI_LINK_DEPTH         0x00000004u   /* follow the link within this pass */

/* Transfer descriptor words: link, status, token, buffer. */
#define UHCI_TD_ACTIVE          0x00800000u
#define UHCI_TD_STALLED         0x00400000u
#define UHCI_TD_DBUFERR         0x00200000u
#define UHCI_TD_BABBLE          0x00100000u
#define UHCI_TD_NAK             0x00080000u
#define UHCI_TD_CRCTIMEO        0x00040000u
#define UHCI_TD_BITSTUFF        0x00020000u
#define UHCI_TD_IOC             0x01000000u
#define UHCI_TD_LS              0x04000000u
#define UHCI_TD_SPD             0x20000000u   /* short packet completes the TD */
#define UHCI_TD_CERR_3          (3u << 27)
#define UHCI_TD_ACTLEN_MASK     0x000007FFu
#define UHCI_TD_ERRORS          (UHCI_TD_STALLED | UHCI_TD_DBUFERR | \
                                 UHCI_TD_BABBLE | UHCI_TD_CRCTIMEO | \
                                 UHCI_TD_BITSTUFF)

#define UHCI_PID_IN             0x69u
#define UHCI_PID_OUT            0xE1u
#define UHCI_PID_SETUP          0x2Du

typedef struct {
    uint16_t io;                    /* I/O base from BAR0 */
    uint32_t ports;
    bool started;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t next_address;
} Uhci;

static Uhci uhci_all[USB_UHCI_MAX_CONTROLLERS];
static uint32_t uhci_cur = 0u;
static uint32_t uhci_count = 0u;
#define uhci uhci_all[uhci_cur]

typedef struct {
    bool in_use;
    bool low_speed;
    bool toggle;                    /* data toggle of the interrupt endpoint */
    uint8_t controller;
    uint8_t address;
    uint8_t port;
    uint8_t kind;
    uint8_t ep_number;
    uint16_t ep_mps;
    uint8_t interval;
    uint16_t transfer_bytes;
    uint32_t errors;
    UsbHidRoute route;
    volatile uint32_t *qh_control;      /* element = control TD chain */
    volatile uint32_t *qh_interrupt;    /* element = the HID report TD */
    volatile uint32_t *td_control;
    volatile uint32_t *td_interrupt;
    volatile uint8_t *report;
} UhciDevice;

static UhciDevice uhci_device[USB_UHCI_MAX_CONTROLLERS][UHCI_DEVICES];
static uint32_t uhci_devices_total = 0u;

static volatile uint32_t uhci_frame_list[USB_UHCI_MAX_CONTROLLERS][UHCI_FRAME_LIST_SIZE]
    __attribute__((aligned(4096)));
static volatile uint32_t uhci_qh[USB_UHCI_MAX_CONTROLLERS][UHCI_DEVICES][2][UHCI_QH_DWORDS]
    __attribute__((aligned(16)));
static volatile uint32_t uhci_td[USB_UHCI_MAX_CONTROLLERS][UHCI_DEVICES][UHCI_TD_MAX][UHCI_TD_DWORDS]
    __attribute__((aligned(16)));
static volatile uint8_t uhci_report[USB_UHCI_MAX_CONTROLLERS][UHCI_DEVICES][USB_REPORT_BYTES]
    __attribute__((aligned(64)));
static uint8_t uhci_setup_packet[8] __attribute__((aligned(8)));

static void uhci_schedule_rebuild(void);
static void uhci_scan_ports(void);

/* ---------------------------------------------------------------------- */
/* Register access                                                        */
/* ---------------------------------------------------------------------- */

static uint16_t uhci_readw(uint16_t reg) {
    return inw((uint16_t)(uhci.io + reg));
}

static void uhci_writew(uint16_t reg, uint16_t value) {
    outw((uint16_t)(uhci.io + reg), value);
}

static void uhci_writel(uint16_t reg, uint32_t value) {
    outl((uint16_t)(uhci.io + reg), value);
}

static uint16_t uhci_portsc_reg(uint32_t port) {
    return (uint16_t)(UHCI_REG_PORTSC1 + 2u * (port - 1u));
}

/* ---------------------------------------------------------------------- */
/* Transfer descriptors and queue heads                                   */
/* ---------------------------------------------------------------------- */

/*
 * One transfer descriptor.  The expected length field stores the length minus
 * one, so a zero-length transfer needs the all-ones encoding the hardware
 * uses for "no data" - writing zero there would ask for one byte.
 */
static void uhci_td_fill(volatile uint32_t *td, uint32_t link, uint32_t pid,
                         uint8_t address, uint8_t endpoint, bool toggle,
                         bool low_speed, uint16_t length, uint32_t buffer,
                         bool active, bool ioc, bool short_packet) {
    uint32_t expected = length == 0u ? 0x7FFu : (uint32_t)(length - 1u);
    uint32_t status = UHCI_TD_CERR_3;

    td[0] = link;
    if (active) {
        status |= UHCI_TD_ACTIVE;
    }
    if (ioc) {
        status |= UHCI_TD_IOC;
    }
    if (short_packet) {
        status |= UHCI_TD_SPD;
    }
    if (low_speed) {
        status |= UHCI_TD_LS;
    }
    td[1] = status;
    td[2] = (uint32_t)pid | ((uint32_t)address << 8) |
            ((uint32_t)endpoint << 15) | (toggle ? (1u << 19) : 0u) |
            ((expected & 0x7FFu) << 21);
    td[3] = buffer;
}

static void uhci_qh_clear(volatile uint32_t *qh) {
    qh[0] = UHCI_LINK_TERMINATE;    /* link: end of the queue-head chain */
    qh[1] = UHCI_LINK_TERMINATE;    /* element: nothing queued */
    qh[2] = 0u;
    qh[3] = 0u;
}

/* Wait for a TD to stop being active.  Bounded by both timer ticks and a spin
 * cap so a controller that never answers cannot hold up the boot. */
static bool uhci_wait_td(volatile uint32_t *td, uint32_t ticks) {
    uint32_t start = timer_ticks;
    uint32_t spins = 0u;

    while ((td[1] & UHCI_TD_ACTIVE) != 0u) {
        if (timer_ticks - start >= ticks || ++spins > 40000000u) {
            serial_trace("WARNING", "USB UHCI transfer timed out");
            serial_trace_hex_value("WARNING", "  usbsts",
                                   (uint32_t)uhci_readw(UHCI_REG_USBSTS));
            serial_trace_hex_value("WARNING", "  frame number",
                                   (uint32_t)uhci_readw(UHCI_REG_FRNUM));
            serial_trace_hex_value("WARNING", "  td status", td[1]);
            serial_trace_hex_value("WARNING", "  td token", td[2]);
            serial_trace_hex_value("WARNING", "  td link", td[0]);
            return false;
        }
    }
    return true;
}

/*
 * One control transfer as the standard three descriptors: setup, an optional
 * data stage, and a status stage that always runs the other way from the data
 * stage.  The links say "depth first", which is what makes the controller run
 * the whole chain inside one frame instead of one descriptor per frame.
 */
static bool uhci_control_transfer(void *context, uint8_t request_type,
                                  uint8_t request, uint16_t value, uint16_t index,
                                  uint16_t length, uint8_t *buffer) {
    UhciDevice *device = (UhciDevice *)context;
    volatile uint32_t *chain = device->td_control;
    volatile uint32_t *setup = &chain[0];
    volatile uint32_t *data = &chain[UHCI_TD_DWORDS];
    volatile uint32_t *last;
    uint32_t data_link = ((uint32_t)(uintptr_t)data) | UHCI_LINK_DEPTH;
    bool in = (request_type & 0x80u) != 0u;

    uhci_setup_packet[0] = request_type;
    uhci_setup_packet[1] = request;
    uhci_setup_packet[2] = (uint8_t)(value & 0xFFu);
    uhci_setup_packet[3] = (uint8_t)(value >> 8);
    uhci_setup_packet[4] = (uint8_t)(index & 0xFFu);
    uhci_setup_packet[5] = (uint8_t)(index >> 8);
    uhci_setup_packet[6] = (uint8_t)(length & 0xFFu);
    uhci_setup_packet[7] = (uint8_t)(length >> 8);

    if (length != 0u) {
        /* Data stage, then the status stage running the other way.  The data
         * toggle of a control transfer is DATA1 for both. */
        last = &chain[2u * UHCI_TD_DWORDS];
        uhci_td_fill(data, ((uint32_t)(uintptr_t)last) | UHCI_LINK_DEPTH,
                     in ? UHCI_PID_IN : UHCI_PID_OUT, device->address, 0u, true,
                     device->low_speed, length, (uint32_t)(uintptr_t)buffer,
                     true, false, false);
        uhci_td_fill(last, UHCI_LINK_TERMINATE, in ? UHCI_PID_OUT : UHCI_PID_IN,
                     device->address, 0u, true, device->low_speed, 0u, 0u,
                     true, true, false);
    } else {
        /* No data stage: the status stage is always an IN (USB 2.0 8.5.3). */
        last = data;
        uhci_td_fill(last, UHCI_LINK_TERMINATE, UHCI_PID_IN, device->address, 0u,
                     true, device->low_speed, 0u, 0u, true, true, false);
    }
    uhci_td_fill(setup, data_link, UHCI_PID_SETUP, device->address, 0u, false,
                 device->low_speed, 8u, (uint32_t)(uintptr_t)uhci_setup_packet,
                 true, false, false);

    device->qh_control[1] = (uint32_t)(uintptr_t)setup;
    if (!uhci_wait_td(last, 40u)) {
        return false;
    }
    if ((last[1] & (UHCI_TD_ACTIVE | UHCI_TD_ERRORS)) != 0u) {
        serial_trace_hex_value("WARNING", "USB UHCI transfer failed, status",
                               last[1]);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Ports and enumeration                                                  */
/* ---------------------------------------------------------------------- */

/*
 * Reset a port and start it running.
 *
 * The port enable bit is software's, and that is the whole point of this
 * function: UHCI does not raise it on its own after a reset the way earlier
 * host controllers do, so a driver that only resets the port and then reads
 * it back finds the enable bit clear, concludes the device did not come up,
 * and leaves a perfectly good keyboard dead on a running port.  Linux's UHCI
 * driver enables the port explicitly after every reset for the same reason.
 *
 * The change bits raised by the reset (connect, and enable for a port that
 * was already running) are cleared, so the next scan does not see the same
 * event twice.
 */
static bool uhci_port_reset(uint32_t port, bool *low_speed) {
    uint16_t reg = uhci_portsc_reg(port);
    uint16_t value = uhci_readw(reg);
    uint32_t attempt;

    if ((value & UHCI_PORTSC_CCS) == 0u) {
        return false;
    }
    /* Hold the reset for the 10 ms the specification asks for, then let the
     * device settle before reading the port again. */
    uhci_writew(reg, (uint16_t)((value & UHCI_PORTSC_OWNED) | UHCI_PORTSC_PR));
    usb_wait_ticks(2);
    value = uhci_readw(reg);
    uhci_writew(reg, (uint16_t)((value & UHCI_PORTSC_OWNED) | UHCI_PORTSC_CHANGE));
    usb_wait_ticks(1);

    for (attempt = 0u; attempt < 3u; ++attempt) {
        value = uhci_readw(reg);
        uhci_writew(reg, (uint16_t)((value & UHCI_PORTSC_OWNED) |
                                    UHCI_PORTSC_PE | UHCI_PORTSC_CHANGE));
        usb_wait_ticks(1);
        value = uhci_readw(reg);
        if ((value & (UHCI_PORTSC_CCS | UHCI_PORTSC_PE)) ==
            (UHCI_PORTSC_CCS | UHCI_PORTSC_PE)) {
            *low_speed = (value & UHCI_PORTSC_LSDA) != 0u;
            return true;
        }
        if ((value & UHCI_PORTSC_CCS) == 0u) {
            return false;                    /* the device went away */
        }
    }
    serial_trace_hex_value("WARNING", "USB UHCI port stayed disabled, PORTSC",
                           (uint32_t)value);
    return false;
}

static void uhci_post_interrupt(UhciDevice *device) {
    uhci_td_fill(device->td_interrupt, UHCI_LINK_TERMINATE, UHCI_PID_IN,
                 device->address, device->ep_number, device->toggle,
                 device->low_speed, device->transfer_bytes,
                 (uint32_t)(uintptr_t)device->report, true, true, true);
    device->qh_interrupt[1] = (uint32_t)(uintptr_t)device->td_interrupt;
}

static bool uhci_enumerate(uint32_t port, bool low_speed) {
    UhciDevice *device;
    uint8_t descriptor[8];
    uint8_t device_mps = 0u;
    uint8_t ep_number = 0u;
    uint16_t ep_mps = 0u;
    uint8_t interval = 0u;
    uint8_t kind = 0u;
    uint8_t interface_number = 0u;
    uint32_t index;
    uint32_t i;

    for (index = 0u; index < UHCI_DEVICES; ++index) {
        if (!uhci_device[uhci_cur][index].in_use) {
            break;
        }
    }
    if (index >= UHCI_DEVICES) {
        return false;
    }
    device = &uhci_device[uhci_cur][index];
    for (i = 0u; i < (uint32_t)sizeof(*device); ++i) {
        ((uint8_t *)device)[i] = 0u;
    }
    device->in_use = true;
    device->low_speed = low_speed;
    device->controller = (uint8_t)uhci_cur;
    device->port = (uint8_t)port;
    device->ep_mps = 8u;
    device->qh_control = uhci_qh[uhci_cur][index][0];
    device->qh_interrupt = uhci_qh[uhci_cur][index][1];
    device->td_control = uhci_td[uhci_cur][index][0];
    device->td_interrupt = uhci_td[uhci_cur][index][UHCI_TD_MAX - 1u];
    device->report = uhci_report[uhci_cur][index];
    uhci_qh_clear(device->qh_control);
    uhci_qh_clear(device->qh_interrupt);
    uhci_schedule_rebuild();

    usb_last_stage = USB_STAGE_DESCRIPTOR;
    serial_trace_uint_value("INFO", "USB UHCI device on port", port);
    serial_trace_uint_value("INFO", "  port speed (1 = low)",
                            low_speed ? 1u : 0u);
    /* Eight bytes of the device descriptor are enough to learn endpoint 0's
     * packet size, which the address stage depends on. */
    if (!uhci_control_transfer(device, 0x80u, USB_REQ_GET_DESCRIPTOR,
                               (uint16_t)(USB_DESC_DEVICE << 8), 0u, 8u, descriptor)) {
        serial_trace_uint_value("WARNING", "USB UHCI device descriptor failed, port",
                                port);
        device->in_use = false;
        return false;
    }
    if (descriptor[7] >= 8u) {
        device->ep_mps = descriptor[7];
    }

    usb_last_stage = USB_STAGE_ADDRESS;
    if (++uhci.next_address == 0u || uhci.next_address > 127u) {
        uhci.next_address = 1u;
    }
    if (!uhci_control_transfer(device, 0x00u, 5u, (uint16_t)uhci.next_address, 0u,
                               0u, NULL)) {
        serial_trace("WARNING", "USB UHCI set address failed");
        device->in_use = false;
        return false;
    }
    device->address = uhci.next_address;
    usb_wait_ticks((TIMER_HZ / 100u) == 0u ? 1u : (TIMER_HZ / 100u));   /* 10 ms */

    if (!usb_hid_identify(device, uhci_control_transfer, &device_mps, &ep_number,
                          &ep_mps, &interval, &kind, &interface_number)) {
        serial_trace_uint_value("WARNING", "USB UHCI device is not a boot HID, port",
                                port);
        device->in_use = false;
        return false;
    }
    (void)device_mps;
    usb_last_stage = USB_STAGE_CONFIG;
    if (!uhci_control_transfer(device, 0x00u, USB_REQ_SET_CONFIG, 1u, 0u, 0u, NULL)) {
        device->in_use = false;
        return false;
    }
    /* Boot protocol fixes the report layout at eight bytes for a keyboard and
     * three for a mouse, whatever the report descriptor would have said. */
    (void)uhci_control_transfer(device, 0x21u, USB_REQ_SET_PROTOCOL, 0u,
                                interface_number, 0u, NULL);

    device->ep_number = ep_number;
    device->ep_mps = ep_mps;
    device->interval = interval;
    device->kind = kind;
    device->toggle = false;
    device->transfer_bytes = kind == USB_HID_KIND_KEYBOARD ? 8u : 3u;
    uhci_post_interrupt(device);
    usb_last_stage = USB_STAGE_NONE;

    ++uhci_devices_total;
    if (kind == USB_HID_KIND_KEYBOARD) {
        ++usb_keyboards_total;
        serial_trace("INFO", "USB UHCI keyboard ready");
    } else {
        ++usb_mice_total;
        serial_trace("INFO", "USB UHCI mouse ready");
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Schedule maintenance                                                   */
/* ---------------------------------------------------------------------- */

/*
 * The whole schedule as one chain: control queue heads first, then the
 * interrupt queue heads, and every frame list entry points at the head of it.
 * Following the links within a pass is what the depth-first bit on each link
 * asks for, so one frame entry polls every endpoint; the terminating link
 * ends the pass.
 */
static void uhci_schedule_rebuild(void) {
    volatile uint32_t *first = NULL;
    volatile uint32_t *previous = NULL;
    uint32_t i;
    uint32_t d;

    for (d = 0u; d < UHCI_DEVICES; ++d) {
        if (!uhci_device[uhci_cur][d].in_use) {
            continue;
        }
        for (i = 0u; i < 2u; ++i) {
            volatile uint32_t *qh = uhci_qh[uhci_cur][d][i];

            if (first == NULL) {
                first = qh;
            }
            if (previous != NULL) {
                previous[0] = ((uint32_t)(uintptr_t)qh) | UHCI_LINK_QH |
                              UHCI_LINK_DEPTH;
            }
            previous = qh;
        }
    }
    if (previous != NULL) {
        previous[0] = UHCI_LINK_TERMINATE;
    }
    for (i = 0u; i < UHCI_FRAME_LIST_SIZE; ++i) {
        uhci_frame_list[uhci_cur][i] =
            first != NULL ? (((uint32_t)(uintptr_t)first) | UHCI_LINK_QH)
                          : UHCI_LINK_TERMINATE;
    }
}

/* ---------------------------------------------------------------------- */
/* Controller bring-up                                                    */
/* ---------------------------------------------------------------------- */

static bool uhci_attach(uint8_t bus, uint8_t slot, uint8_t function) {
    /*
     * A UHCI register block lives in I/O space, and the controller puts it in
     * BAR4 (offset 0x20) - BAR0 is unused on the Intel parts and reads as a
     * zero-sized memory BAR.  Reading BAR0 alone therefore finds nothing on
     * exactly the machines this driver exists for, so BAR4 is tried first and
     * BAR0 is kept as a fallback for parts that do put it first.
     */
    uint32_t bar4 = pci_config_read32(bus, slot, function, 0x20);
    uint32_t bar0 = pci_config_read32(bus, slot, function, 0x10);
    uint32_t bar = (bar4 & 0x1u) != 0u ? bar4 : bar0;
    uint32_t command;

    if ((bar & 0x1u) == 0u || (bar & ~0x1Fu) == 0u || (bar & ~0x1Fu) > 0xFFE0u) {
        return false;
    }
    serial_trace_hex_value("INFO", "USB UHCI BAR4/BAR0", bar4);
    serial_trace_hex_value("INFO", "USB UHCI BAR0", bar0);
    uhci.io = (uint16_t)(bar & ~0x1Fu);
    uhci.bus = bus;
    uhci.slot = slot;
    uhci.function = function;
    command = pci_config_read32(bus, slot, function, 0x04);
    pci_config_write32(bus, slot, function, 0x04, command | 0x00000005u);
    return true;
}

/* Bounded poll of a 16-bit port register, in the style of usb_wait_bits:
 * spins when the timer never ran, a tick deadline otherwise, because a
 * port I/O read costs about a microsecond and forty million of them are
 * tens of seconds of boot time on real silicon. */
static bool uhci_wait_bits(uint16_t reg, uint16_t mask, uint16_t value,
                           uint32_t spins) {
    uint32_t start = timer_ticks;
    uint32_t i;

    for (i = 0u; i < spins; ++i) {
        if ((uhci_readw(reg) & mask) == value) {
            return true;
        }
        if ((i & 0xFFFFu) == 0u && timer_ticks - start >= 16u) {
            return false;
        }
    }
    return false;
}

static bool uhci_start(void) {
    uint32_t i;

    /* A global reset first (it resets the bus, not just the controller), then
     * the host controller reset: firmware does not always leave the block in
     * a usable state, and this is cheap enough to always do. */
    uhci_writew(UHCI_REG_USBCMD, UHCI_CMD_GRESET);
    usb_wait_ticks(5);
    uhci_writew(UHCI_REG_USBCMD, 0u);
    usb_wait_ticks(1);
    uhci_writew(UHCI_REG_USBCMD, UHCI_CMD_HCRESET);
    if (!uhci_wait_bits(UHCI_REG_USBCMD, UHCI_CMD_HCRESET, 0u, 40000000u)) {
        serial_trace("WARNING", "USB UHCI reset never cleared");
        return false;
    }

    for (i = 0u; i < UHCI_FRAME_LIST_SIZE; ++i) {
        uhci_frame_list[uhci_cur][i] = UHCI_LINK_TERMINATE;
    }
    uhci_qh_clear(uhci_qh[uhci_cur][0][0]);
    uhci_writel(UHCI_REG_FLBASEADD, (uint32_t)(uintptr_t)uhci_frame_list[uhci_cur]);
    uhci_writew(UHCI_REG_FRNUM, 0u);
    uhci_writew(UHCI_REG_SOF, 64u);          /* one millisecond per frame */
    uhci_writew(UHCI_REG_USBINTR, 0u);       /* polled, never interrupt driven */

    uhci_writew(UHCI_REG_USBCMD, UHCI_CMD_RUN | UHCI_CMD_CF | UHCI_CMD_MAXP);
    if (!uhci_wait_bits(UHCI_REG_USBSTS, UHCI_STS_HCH, 0u, 40000000u)) {
        serial_trace("WARNING", "USB UHCI did not start");
        return false;
    }
    uhci.next_address = 0u;
    uhci.started = true;
    serial_trace("INFO", "USB UHCI controller started");
    usb_wait_ticks(2);
    return true;
}

static void uhci_scan_ports(void) {
    uint32_t port;
    uint32_t connected = 0u;

    for (port = 1u; port <= UHCI_MAX_PORTS; ++port) {
        uint16_t reg = uhci_portsc_reg(port);
        uint16_t status = uhci_readw(reg);
        bool low_speed = false;
        uint32_t i;
        bool known = false;

        /*
         * A port register that is not implemented reads as an open bus.  Bit
         * 7 is always one on a real port while the top three bits are "write
         * zeroes", so a value that disagrees with that is not a port - which
         * is what keeps a controller with two ports from being scanned as if
         * it had four and "finding" a device on each of the empty ones.
         */
        if ((status & 0xE080u) != 0x0080u || (status & UHCI_PORTSC_CCS) == 0u) {
            continue;
        }
        ++connected;
        for (i = 0u; i < UHCI_DEVICES; ++i) {
            if (uhci_device[uhci_cur][i].in_use &&
                uhci_device[uhci_cur][i].port == (uint8_t)port) {
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        serial_trace_uint_value("INFO", "USB UHCI scan port", port);
        serial_trace_hex_value("INFO", "  PORTSC", (uint32_t)status);
        if (!uhci_port_reset(port, &low_speed)) {
            continue;
        }
        uhci_enumerate(port, low_speed);
    }
    uhci.ports = connected;
    (void)uhci.ports;
    if (usb_ports_connected < connected) {
        usb_ports_connected = connected;
    }
}

static void usb_uhci_init_all(void) {
    UsbPciCursor cursor;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;

    usb_pci_cursor_reset(&cursor);
    while (uhci_count < USB_UHCI_MAX_CONTROLLERS &&
           usb_pci_find(&cursor, USB_PCI_PROGIF_UHCI, &bus, &slot, &function)) {
        ++usb_bus_found[USB_BUS_UHCI];
        uhci_cur = uhci_count;
        uhci_all[uhci_cur].started = false;
        if (!uhci_attach(bus, slot, function)) {
            continue;
        }
        serial_trace_hex_value("INFO", "USB UHCI controller at",
                               ((uint32_t)bus << 24) | ((uint32_t)slot << 16) |
                               ((uint32_t)function << 8));
        if (!uhci_start()) {
            continue;
        }
        ++uhci_count;
        ++usb_bus_started[USB_BUS_UHCI];
        uhci_scan_ports();
    }
    uhci_cur = 0u;
    serial_trace_uint_value("INFO", "USB UHCI devices", uhci_devices_total);
}

/* ---------------------------------------------------------------------- */
/* Frame loop                                                             */
/* ---------------------------------------------------------------------- */

static void uhci_device_poll(UhciDevice *device) {
    volatile uint32_t *td = device->td_interrupt;
    uint32_t status;

    if (!device->in_use) {
        return;
    }
    status = td[1];
    if ((status & UHCI_TD_ACTIVE) != 0u) {
        return;                              /* still waiting for a report */
    }
    if ((status & UHCI_TD_ERRORS) != 0u) {
        if (++device->errors <= 2u) {
            serial_trace_hex_value("WARNING", "USB UHCI report failed, status",
                                   status);
        }
        if (device->errors >= 8u) {
            return;                          /* give up on this endpoint */
        }
    } else if ((status & UHCI_TD_NAK) == 0u) {
        /* A NAK means "no report yet", which is not a failure and must not
         * flip the endpoint's data toggle. */
        uint32_t delivered = ((status & UHCI_TD_ACTLEN_MASK) + 1u) &
                             UHCI_TD_ACTLEN_MASK;

        device->toggle = !device->toggle;
        if (device->kind == USB_HID_KIND_KEYBOARD) {
            if (delivered != 0u) {
                usb_route_keyboard(&device->route, device->report, delivered);
            }
        } else if (device->kind == USB_HID_KIND_MOUSE && delivered != 0u) {
            usb_mouse_report(device->report, delivered);
        }
    }
    uhci_post_interrupt(device);
}

static void usb_uhci_poll_all(void) {
    uint32_t controller;

    for (controller = 0u; controller < uhci_count; ++controller) {
        uint32_t i;

        uhci_cur = controller;
        if (!uhci.started) {
            continue;
        }
        for (i = 0u; i < UHCI_DEVICES; ++i) {
            uhci_device_poll(&uhci_device[uhci_cur][i]);
        }
    }
    uhci_cur = 0u;
}

static void usb_uhci_rescan(void) {
    uint32_t controller;

    for (controller = 0u; controller < uhci_count; ++controller) {
        uhci_cur = controller;
        if (uhci.started) {
            uhci_scan_ports();
        }
    }
    uhci_cur = 0u;
}

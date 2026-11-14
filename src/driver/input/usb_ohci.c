// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: usb_ohci.c, USB 1.1 input through an OHCI host controller.

// This repository is licensed under the GNU General Public License.

/*
 * OHCI is the controller whose ports a keyboard is attached to on every
 * chipset that pairs its USB 2.0 block with OHCI companions instead of UHCI
 * ones - which is the AMD, VIA and nVidia families.  That split is the whole
 * reason this file exists: a USB 2.0 chipset routes each full or low-speed
 * device on a shared port to a companion controller, the EHCI's own port then
 * reads as "nothing attached" (or as owned by a companion), and keyboards
 * and mice are full or low-speed devices.  The EHCI driver therefore leaves
 * those ports alone on purpose, so without an OHCI driver a machine whose
 * keyboard sits on a companion port boots with no input at all while the boot
 * menu reports a perfectly healthy EHCI.
 *
 * Unlike UHCI - where the frame list holds everything - OHCI has three
 * schedules and two descriptor types.  Endpoint descriptors (EDs) are the
 * per-endpoint queue heads and transfer descriptors (TDs) are the individual
 * packets; the control list walks EDs and runs each ED's whole TD chain
 * within one frame, and a 32-entry interrupt table in the HCCA names the EDs
 * to poll each frame.  Both are used here: control transfers run on the
 * control list, and a HID report sits in the periodic list as one TD that is
 * re-armed as soon as it completes - the same shape as the UHCI and EHCI
 * drivers, and the reports go to the same decoder.
 *
 * Everything the controller walks by DMA is volatile and aligned as the
 * specification requires (EDs and TDs to 16 bytes, the HCCA to 256), because
 * the kernel runs with paging off and the controller reads these structures
 * behind the driver's back.
 */

#define OHCI_DEVICES            2u      /* one keyboard and one mouse each */
#define OHCI_TD_MAX             4u      /* 3 for a control transfer, 1 for HID */
#define OHCI_ED_DWORDS          4u
#define OHCI_TD_DWORDS          4u
#define OHCI_HCCA_DWORDS        64u     /* 256 bytes, the writeback area */
#define OHCI_MAX_PORTS          15u     /* the specification caps a root hub here */

/* Registers, relative to the memory BAR. */
#define OHCI_REG_REVISION       0x00u
#define OHCI_REG_CONTROL        0x04u
#define OHCI_REG_CMDSTATUS      0x08u
#define OHCI_REG_INTRSTATUS     0x0Cu
#define OHCI_REG_INTRENABLE     0x10u
#define OHCI_REG_INTRDISABLE    0x14u
#define OHCI_REG_HCCA           0x18u
#define OHCI_REG_ED_CONTROLHEAD 0x20u
#define OHCI_REG_ED_CONTROLCUR  0x24u
#define OHCI_REG_DONEHEAD       0x30u
#define OHCI_REG_FMINTERVAL     0x34u
#define OHCI_REG_FMNUMBER       0x3Cu
#define OHCI_REG_PERIODICSTART  0x40u
#define OHCI_REG_LSTHRESH       0x44u
#define OHCI_REG_RHDESCA        0x48u
#define OHCI_REG_RHDESCB        0x4Cu
#define OHCI_REG_RHSTATUS       0x50u
#define OHCI_REG_RHPORT(n)      (0x54u + 4u * ((n) - 1u))

/* HcControl */
#define OHCI_CTRL_CBSR          (3u << 0)
#define OHCI_CTRL_PLE           (1u << 2)
#define OHCI_CTRL_CLE           (1u << 4)
#define OHCI_CTRL_BLE           (1u << 5)
#define OHCI_CTRL_HCFS_MASK     (3u << 6)
#define OHCI_CTRL_HCFS_RESET    (0u << 6)
#define OHCI_CTRL_HCFS_OPER     (2u << 6)

/* HcCommandStatus: the list-filled bits are write-one-to-set, and the
 * controller clears a list's bit once it has walked to the end of that list -
 * so the bit is raised again with every new transfer. */
#define OHCI_CMD_HCR            (1u << 0)
#define OHCI_CMD_CLF            (1u << 1)
#define OHCI_CMD_BLF            (1u << 2)

/* HcRhDescriptorA */
#define OHCI_RHA_NDP_MASK       0x000000FFu
#define OHCI_RHA_PSM            (1u << 8)     /* port power switching mode */
#define OHCI_RHA_NPS            (1u << 9)     /* no power switching */
#define OHCI_RHA_POTPGT_SHIFT   24u           /* power-on-to-power-good, ms */

/* HcRhStatus */
#define OHCI_RHS_LPSC           (1u << 16)    /* power every port on */

/* HcRhPortStatus */
#define OHCI_PORT_CCS           (1u << 0)
#define OHCI_PORT_PES           (1u << 1)
#define OHCI_PORT_PSS           (1u << 2)
#define OHCI_PORT_PRS           (1u << 4)
#define OHCI_PORT_PPS           (1u << 8)
#define OHCI_PORT_LSDA          (1u << 9)
#define OHCI_PORT_CSC           (1u << 16)
#define OHCI_PORT_PESC          (1u << 17)
#define OHCI_PORT_PSSC          (1u << 18)
#define OHCI_PORT_OCIC          (1u << 19)
#define OHCI_PORT_PRSC          (1u << 20)
/* The five change bits clear when written as one. */
#define OHCI_PORT_CHANGES       (OHCI_PORT_CSC | OHCI_PORT_PESC | \
                                 OHCI_PORT_PSSC | OHCI_PORT_OCIC | \
                                 OHCI_PORT_PRSC)
#define OHCI_PORT_OWNED         (OHCI_PORT_PPS)

/* Endpoint descriptor: word 0. */
#define OHCI_ED_DIR_SHIFT       11u
#define OHCI_ED_DIR_SETUP       0u
#define OHCI_ED_DIR_OUT         1u
#define OHCI_ED_DIR_IN          2u
#define OHCI_ED_LOWSPEED        (1u << 13)
#define OHCI_ED_MPS_SHIFT       16u
#define OHCI_ED_MPS_MASK        0x07FFu

/* Transfer descriptor: word 0.  The data toggle is carried either by the ED
 * (T = 00) or stated per descriptor (10 = DATA0, 11 = DATA1); this driver
 * states it, exactly as the UHCI one does, so the endpoint's toggle is
 * software state and a NAK cannot disturb it. */
#define OHCI_TD_R               (1u << 18)
#define OHCI_TD_DP_SHIFT        19u
#define OHCI_TD_DP_SETUP        0u
#define OHCI_TD_DP_OUT          1u
#define OHCI_TD_DP_IN           2u
#define OHCI_TD_DI_SHIFT        21u
#define OHCI_TD_TOGGLE_DATA0    0x02000000u
#define OHCI_TD_TOGGLE_DATA1    0x03000000u
#define OHCI_TD_EC_SHIFT        26u
#define OHCI_TD_CC_SHIFT        28u
#define OHCI_TD_CC_MASK         0xFu
#define OHCI_TD_CC_NOTACCESSED  0xFu     /* the value the descriptor starts at */

typedef struct {
    volatile uint32_t *mmio;
    uint32_t ports;
    bool started;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t next_address;
} Ohci;

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
    volatile uint32_t *ed_control;      /* the endpoint's queue head */
    volatile uint32_t *ed_interrupt;
    volatile uint32_t *td_control;      /* setup / data / status chain */
    volatile uint32_t *td_interrupt;    /* the HID report descriptor */
    volatile uint8_t *report;
} OhciDevice;

static Ohci ohci_all[USB_OHCI_MAX_CONTROLLERS];
static uint32_t ohci_cur = 0u;
static uint32_t ohci_count = 0u;
#define ohci ohci_all[ohci_cur]

static OhciDevice ohci_device[USB_OHCI_MAX_CONTROLLERS][OHCI_DEVICES];
static uint32_t ohci_devices_total = 0u;

/* The HCCA: 32 interrupt-table entries, then the frame number and done head
 * the controller writes back. */
static volatile uint32_t ohci_hcca[USB_OHCI_MAX_CONTROLLERS][OHCI_HCCA_DWORDS]
    __attribute__((aligned(256)));
static volatile uint32_t ohci_ed[USB_OHCI_MAX_CONTROLLERS][OHCI_DEVICES][2][OHCI_ED_DWORDS]
    __attribute__((aligned(16)));
static volatile uint32_t ohci_td[USB_OHCI_MAX_CONTROLLERS][OHCI_DEVICES][OHCI_TD_MAX][OHCI_TD_DWORDS]
    __attribute__((aligned(16)));
static volatile uint8_t ohci_report[USB_OHCI_MAX_CONTROLLERS][OHCI_DEVICES][USB_REPORT_BYTES]
    __attribute__((aligned(64)));
static uint8_t ohci_setup_packet[8] __attribute__((aligned(8)));

static void ohci_schedule_rebuild(void);
static void ohci_scan_ports(void);

/* ---------------------------------------------------------------------- */
/* Register access                                                        */
/* ---------------------------------------------------------------------- */

static volatile uint32_t *ohci_reg(uint32_t offset) {
    return (volatile uint32_t *)((uint8_t *)ohci.mmio + offset);
}

static uint32_t ohci_read(uint32_t offset) {
    return usb_read32(ohci_reg(offset));
}

static void ohci_write(uint32_t offset, uint32_t value) {
    usb_write32(ohci_reg(offset), value);
}

/* Bounded poll of a register, in the style of usb_wait_bits: the same
 * double bound applies (spins when the timer never ran, ticks because a
 * bare spin cap on a real MMIO register is tens of seconds). */
static bool ohci_wait_bits(uint32_t offset, uint32_t mask, uint32_t value,
                           uint32_t spins) {
    return usb_wait_bits(ohci_reg(offset), mask, value, spins, 16u);
}

/* ---------------------------------------------------------------------- */
/* Endpoint and transfer descriptors                                      */
/* ---------------------------------------------------------------------- */

/*
 * An endpoint descriptor is four words: configuration, tail, head, next ED.
 * The head and tail are the queue: the controller runs the descriptors from
 * head up to (and not including) tail, so a chain terminated by a null next
 * pointer ends at tail = 0 and nothing is left queued afterwards.
 */
static void ohci_ed_fill(volatile uint32_t *ed, uint8_t address, uint8_t endpoint,
                         uint32_t direction, bool low_speed, uint16_t mps,
                         uint32_t head, uint32_t tail, uint32_t next) {
    ed[0] = (uint32_t)address | ((uint32_t)endpoint << 7) |
            ((direction & 3u) << OHCI_ED_DIR_SHIFT) |
            (low_speed ? OHCI_ED_LOWSPEED : 0u) |
            (((uint32_t)mps & OHCI_ED_MPS_MASK) << OHCI_ED_MPS_SHIFT);
    ed[1] = tail & 0xFFFFFFF0u;
    ed[2] = head & 0xFFFFFFF0u;
    ed[3] = next & 0xFFFFFFF0u;
}

/*
 * A transfer descriptor is four words too: status bits, buffer pointer, next
 * descriptor, buffer end.  The controller writes the first word back with
 * the condition code of the finished packet, which is how completion is
 * observed here - so the code is put back to NOTACCESSED on every fill.  A
 * descriptor left at "no error" would report itself finished before the
 * controller had even looked at it.
 */
static void ohci_td_fill(volatile uint32_t *td, uint32_t next, uint32_t direction,
                         uint32_t toggle, uint16_t length,
                         const volatile uint8_t *buffer) {
    uint32_t info = ((direction & 3u) << OHCI_TD_DP_SHIFT) | toggle |
                    (3u << OHCI_TD_EC_SHIFT) |
                    (OHCI_TD_CC_NOTACCESSED << OHCI_TD_CC_SHIFT);

    if (length != 0u && direction != OHCI_TD_DP_OUT &&
        direction != OHCI_TD_DP_SETUP) {
        info |= OHCI_TD_R;                   /* a short packet still arrives */
    }
    td[0] = info;
    td[1] = length == 0u ? 0u : (uint32_t)(uintptr_t)buffer;
    td[2] = next & 0xFFFFFFF0u;
    td[3] = length == 0u ? 0u : (uint32_t)((uintptr_t)buffer + length - 1u);
}

static uint32_t ohci_td_condition(volatile uint32_t *td) {
    return (td[0] >> OHCI_TD_CC_SHIFT) & OHCI_TD_CC_MASK;
}

/*
 * Wait for the controller to touch a descriptor.  Bounded by both timer ticks
 * and a spin cap: a controller that never answers must cost boot time, never
 * the boot itself.
 */
static bool ohci_wait_td(volatile uint32_t *td, uint32_t ticks) {
    uint32_t start = timer_ticks;
    uint32_t spins = 0u;

    while (ohci_td_condition(td) == OHCI_TD_CC_NOTACCESSED) {
        if (timer_ticks - start >= ticks || ++spins > 40000000u) {
            serial_trace("WARNING", "USB OHCI transfer timed out");
            serial_trace_hex_value("WARNING", "  HcControl", ohci_read(OHCI_REG_CONTROL));
            serial_trace_hex_value("WARNING", "  HcCommandStatus",
                                   ohci_read(OHCI_REG_CMDSTATUS));
            serial_trace_hex_value("WARNING", "  done head",
                                   ohci_read(OHCI_REG_DONEHEAD));
            serial_trace_hex_value("WARNING", "  td first word", td[0]);
            return false;
        }
    }
    return true;
}

/*
 * One control transfer as the usual three descriptors.  A control endpoint's
 * descriptor names no direction of its own (the D field is SETUP), which is
 * what lets each descriptor in the chain choose its own packet identifier:
 * SETUP, then the data stage, then the status stage running the other way.
 * Programmed into the control endpoint's head pointer, which is also how the
 * list is filled - HcCommandStatus.CLF tells the controller to look.
 */
static bool ohci_control_transfer(void *context, uint8_t request_type,
                                  uint8_t request, uint16_t value, uint16_t index,
                                  uint16_t length, uint8_t *buffer) {
    OhciDevice *device = (OhciDevice *)context;
    volatile uint32_t *setup = &device->td_control[0];
    volatile uint32_t *data = &device->td_control[OHCI_TD_DWORDS];
    volatile uint32_t *status = &device->td_control[2u * OHCI_TD_DWORDS];
    uint32_t data_pointer = (uint32_t)(uintptr_t)data;
    uint32_t status_pointer = (uint32_t)(uintptr_t)status;
    bool in = (request_type & 0x80u) != 0u;
    uint32_t condition;

    ohci_setup_packet[0] = request_type;
    ohci_setup_packet[1] = request;
    ohci_setup_packet[2] = (uint8_t)(value & 0xFFu);
    ohci_setup_packet[3] = (uint8_t)(value >> 8);
    ohci_setup_packet[4] = (uint8_t)(index & 0xFFu);
    ohci_setup_packet[5] = (uint8_t)(index >> 8);
    ohci_setup_packet[6] = (uint8_t)(length & 0xFFu);
    ohci_setup_packet[7] = (uint8_t)(length >> 8);

    if (length != 0u) {
        /* Data stage, then the status stage running the other way.  DATA1 is
         * the toggle of both after the DATA0 setup packet. */
        ohci_td_fill(data, status_pointer,
                     in ? OHCI_TD_DP_IN : OHCI_TD_DP_OUT,
                     OHCI_TD_TOGGLE_DATA1, length, buffer);
        ohci_td_fill(status, 0u, in ? OHCI_TD_DP_OUT : OHCI_TD_DP_IN,
                     OHCI_TD_TOGGLE_DATA1, 0u, NULL);
    } else {
        /* No data stage: the status stage is an IN, and it is the only
         * descriptor in the chain after setup. */
        status = data;
        status_pointer = data_pointer;
        ohci_td_fill(status, 0u, OHCI_TD_DP_IN, OHCI_TD_TOGGLE_DATA1, 0u, NULL);
    }
    ohci_td_fill(setup, data_pointer, OHCI_TD_DP_SETUP, OHCI_TD_TOGGLE_DATA0,
                 8u, ohci_setup_packet);

    device->ed_control[1] = 0u;                                  /* tail */
    device->ed_control[2] = (uint32_t)(uintptr_t)setup;          /* head */
    ohci_write(OHCI_REG_CMDSTATUS, OHCI_CMD_CLF);

    if (!ohci_wait_td(status, 40u)) {
        return false;
    }
    device->ed_control[2] = 0u;
    condition = ohci_td_condition(status);
    if (condition != 0u && condition != 9u) {      /* 9 = data underrun */
        serial_trace_hex_value("WARNING", "USB OHCI transfer failed, condition",
                               condition);
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
 * Two details of OHCI's root hub differ from the other controller families:
 * the port is only powered when the board asks software to switch power
 * (HcRhDescriptorA.PSM without NPS), which is handled once in ohci_start, and
 * the low-speed bit is only valid after the reset has finished - the port
 * reports the device's speed once it has come out of the reset sequence, not
 * while it is in it.
 *
 * A port reset ends when the controller raises port-enable, and the change
 * bits the reset raised are cleared so the next scan does not read the same
 * event twice.
 */
static bool ohci_port_reset(uint32_t port, bool *low_speed) {
    uint32_t reg = OHCI_REG_RHPORT(port);
    uint32_t value = ohci_read(reg);

    if ((value & OHCI_PORT_CCS) == 0u) {
        return false;
    }
    ohci_write(reg, OHCI_PORT_OWNED | OHCI_PORT_PRS);
    usb_wait_ticks(2);                        /* the 10ms the spec asks for */
    ohci_write(reg, OHCI_PORT_OWNED | OHCI_PORT_CHANGES);
    usb_wait_ticks(1);
    if (!ohci_wait_bits(reg, OHCI_PORT_PES, OHCI_PORT_PES, 40000000u)) {
        serial_trace_hex_value("WARNING", "USB OHCI port stayed disabled, status",
                               ohci_read(reg));
        return false;
    }
    value = ohci_read(reg);
    if ((value & OHCI_PORT_CCS) == 0u) {
        return false;                            /* the device went away */
    }
    *low_speed = (value & OHCI_PORT_LSDA) != 0u;
    return true;
}

/*
 * Arm the interrupt endpoint with one report descriptor.
 *
 * The descriptor is a single IN packet, and it is re-armed only after the
 * controller reports it finished: while it is queued the controller keeps
 * retrying it every frame, so a device that has nothing new to say (a NAK)
 * costs nothing, and the moment a report arrives the descriptor carries it
 * into the buffer.  The buffer pointer and end pointer are rewritten here
 * because the controller advances them as it transfers.
 */
static void ohci_post_interrupt(OhciDevice *device) {
    ohci_td_fill(device->td_interrupt, 0u, OHCI_TD_DP_IN,
                 device->toggle ? OHCI_TD_TOGGLE_DATA1 : OHCI_TD_TOGGLE_DATA0,
                 device->transfer_bytes, device->report);
    device->ed_interrupt[1] = 0u;                                  /* tail */
    device->ed_interrupt[2] = (uint32_t)(uintptr_t)device->td_interrupt;
}

static bool ohci_enumerate(uint32_t port, bool low_speed) {
    OhciDevice *device;
    uint8_t descriptor[8];
    uint8_t device_mps = 0u;
    uint8_t ep_number = 0u;
    uint16_t ep_mps = 0u;
    uint8_t interval = 0u;
    uint8_t kind = 0u;
    uint8_t interface_number = 0u;
    uint32_t index;
    uint32_t i;

    for (index = 0u; index < OHCI_DEVICES; ++index) {
        if (!ohci_device[ohci_cur][index].in_use) {
            break;
        }
    }
    if (index >= OHCI_DEVICES) {
        return false;
    }
    device = &ohci_device[ohci_cur][index];
    for (i = 0u; i < (uint32_t)sizeof(*device); ++i) {
        ((uint8_t *)device)[i] = 0u;
    }
    device->in_use = true;
    device->low_speed = low_speed;
    device->controller = (uint8_t)ohci_cur;
    device->port = (uint8_t)port;
    device->ep_mps = 8u;
    device->ed_control = ohci_ed[ohci_cur][index][0];
    device->ed_interrupt = ohci_ed[ohci_cur][index][1];
    device->td_control = ohci_td[ohci_cur][index][0];
    device->td_interrupt = ohci_td[ohci_cur][index][OHCI_TD_MAX - 1u];
    device->report = ohci_report[ohci_cur][index];
    ohci_ed_fill(device->ed_control, 0u, 0u, OHCI_ED_DIR_SETUP, low_speed, 8u,
                 0u, 0u, 0u);
    ohci_ed_fill(device->ed_interrupt, 0u, 0u, OHCI_ED_DIR_IN, low_speed, 8u,
                 0u, 0u, 0u);
    ohci_schedule_rebuild();

    usb_last_stage = USB_STAGE_DESCRIPTOR;
    serial_trace_uint_value("INFO", "USB OHCI device on port", port);
    serial_trace_uint_value("INFO", "  port speed (1 = low)",
                            low_speed ? 1u : 0u);
    /* Eight bytes of the device descriptor are enough to learn endpoint 0's
     * packet size, which the addressing stage depends on. */
    if (!ohci_control_transfer(device, 0x80u, USB_REQ_GET_DESCRIPTOR,
                               (uint16_t)(USB_DESC_DEVICE << 8), 0u, 8u, descriptor)) {
        serial_trace_uint_value("WARNING", "USB OHCI device descriptor failed, port",
                                port);
        device->in_use = false;
        return false;
    }
    if (descriptor[7] >= 8u) {
        device->ep_mps = descriptor[7];
    }
    device->ed_control[0] &= ~OHCI_ED_MPS_MASK;
    device->ed_control[0] |= ((uint32_t)device->ep_mps & OHCI_ED_MPS_MASK)
                             << OHCI_ED_MPS_SHIFT;

    usb_last_stage = USB_STAGE_ADDRESS;
    if (++ohci.next_address == 0u || ohci.next_address > 127u) {
        ohci.next_address = 1u;
    }
    if (!ohci_control_transfer(device, 0x00u, 5u, (uint16_t)ohci.next_address, 0u,
                               0u, NULL)) {
        serial_trace("WARNING", "USB OHCI set address failed");
        device->in_use = false;
        return false;
    }
    device->address = ohci.next_address;
    device->ed_control[0] = (device->ed_control[0] & ~0x7Fu) | device->address;
    device->ed_interrupt[0] = (device->ed_interrupt[0] & ~0x7Fu) | device->address;

    if (!usb_hid_identify(device, ohci_control_transfer, &device_mps, &ep_number,
                          &ep_mps, &interval, &kind, &interface_number)) {
        serial_trace_uint_value("WARNING", "USB OHCI device is not a boot HID, port",
                                port);
        device->in_use = false;
        return false;
    }
    (void)device_mps;
    usb_last_stage = USB_STAGE_CONFIG;
    if (!ohci_control_transfer(device, 0x00u, USB_REQ_SET_CONFIG, 1u, 0u, 0u, NULL)) {
        device->in_use = false;
        return false;
    }
    /* Boot protocol fixes the report layout at eight bytes for a keyboard and
     * three for a mouse, whatever the report descriptor would have said. */
    (void)ohci_control_transfer(device, 0x21u, USB_REQ_SET_PROTOCOL, 0u,
                                interface_number, 0u, NULL);

    device->ep_number = ep_number;
    device->ep_mps = ep_mps;
    device->interval = interval;
    device->kind = kind;
    device->toggle = false;
    device->transfer_bytes = kind == USB_HID_KIND_KEYBOARD ? 8u : 3u;
    ohci_ed_fill(device->ed_interrupt, device->address, ep_number, OHCI_ED_DIR_IN,
                 low_speed, ep_mps, 0u, 0u, 0u);
    ohci_post_interrupt(device);
    ohci_schedule_rebuild();
    usb_last_stage = USB_STAGE_NONE;

    ++ohci_devices_total;
    if (kind == USB_HID_KIND_KEYBOARD) {
        ++usb_keyboards_total;
        serial_trace("INFO", "USB OHCI keyboard ready");
    } else {
        ++usb_mice_total;
        serial_trace("INFO", "USB OHCI mouse ready");
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Schedule maintenance                                                   */
/* ---------------------------------------------------------------------- */

/*
 * Both schedules are rebuilt from the list of live devices: the control list
 * chains every control queue head, and every entry of the HCCA's interrupt
 * table names the first interrupt queue head.  Pointing all 32 entries at the
 * same chain polls the interrupt endpoints once per frame, which is well
 * inside what a keyboard needs, and each chain is terminated by a null link
 * so an empty schedule stays legal.
 *
 * The control list's head register is only written when the chain actually
 * changes; the list-filled bit is raised here as well as on every transfer,
 * because a controller that has walked to the end of the list clears it.
 */
static void ohci_schedule_rebuild(void) {
    volatile uint32_t *first_control = NULL;
    volatile uint32_t *previous_control = NULL;
    volatile uint32_t *first_interrupt = NULL;
    volatile uint32_t *previous_interrupt = NULL;
    uint32_t entry;
    uint32_t d;

    for (d = 0u; d < OHCI_DEVICES; ++d) {
        OhciDevice *device = &ohci_device[ohci_cur][d];

        if (!device->in_use) {
            continue;
        }
        if (first_control == NULL) {
            first_control = device->ed_control;
        }
        if (previous_control != NULL) {
            previous_control[3] = (uint32_t)(uintptr_t)device->ed_control;
        }
        previous_control = device->ed_control;

        if (first_interrupt == NULL) {
            first_interrupt = device->ed_interrupt;
        }
        if (previous_interrupt != NULL) {
            previous_interrupt[3] = (uint32_t)(uintptr_t)device->ed_interrupt;
        }
        previous_interrupt = device->ed_interrupt;
    }
    if (previous_control != NULL) {
        previous_control[3] = 0u;
    }
    if (previous_interrupt != NULL) {
        previous_interrupt[3] = 0u;
    }

    for (entry = 0u; entry < 32u; ++entry) {
        ohci_hcca[ohci_cur][entry] =
            first_interrupt != NULL ? (uint32_t)(uintptr_t)first_interrupt : 0u;
    }
    ohci_write(OHCI_REG_ED_CONTROLHEAD,
               first_control != NULL ? (uint32_t)(uintptr_t)first_control : 0u);

    {
        /* The periodic list is enabled only once there is something to poll,
         * and the control list always is. */
        uint32_t control = OHCI_CTRL_CBSR | OHCI_CTRL_CLE;

        if (first_interrupt != NULL) {
            control |= OHCI_CTRL_PLE;
        }
        ohci_write(OHCI_REG_CONTROL, control | OHCI_CTRL_HCFS_OPER);
    }
    ohci_write(OHCI_REG_CMDSTATUS, OHCI_CMD_CLF);
}

/* ---------------------------------------------------------------------- */
/* Controller bring-up                                                    */
/* ---------------------------------------------------------------------- */

static bool ohci_attach(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t bar = pci_config_read32(bus, slot, function, 0x10);
    uint32_t command;

    if ((bar & 0x1u) != 0u || (bar & ~0x0Fu) == 0u) {
        return false;                        /* no memory window here */
    }
    serial_trace_hex_value("INFO", "USB OHCI BAR0", bar);
    ohci.mmio = (volatile uint32_t *)(uintptr_t)(bar & ~0x0Fu);
    ohci.bus = bus;
    ohci.slot = slot;
    ohci.function = function;
    command = pci_config_read32(bus, slot, function, 0x04);
    pci_config_write32(bus, slot, function, 0x04, command | 0x00000006u);
    return true;
}

/*
 * Bring the controller up.
 *
 * The order follows the specification's initialisation sequence: put the
 * controller into the reset state, reset it, wait for it to declare itself
 * ready (it clears HcCommandStatus.HCR and then leaves the reset state on its
 * own), program the frame timing, hand it the HCCA, then switch it to the
 * operational state.
 *
 * Interrupts are left masked: the frame loop polls, so the controller is
 * never asked to raise one.  That is what keeps this driver compatible with
 * a machine whose firmware left its interrupt routing somewhere unexpected.
 */
static bool ohci_start(void) {
    uint32_t control;
    uint32_t rhdesca;
    uint32_t spi;
    uint32_t potpgt;
    uint32_t fminterval;
    uint32_t i;

    control = ohci_read(OHCI_REG_CONTROL);
    if ((control & OHCI_CTRL_HCFS_MASK) != OHCI_CTRL_HCFS_RESET) {
        ohci_write(OHCI_REG_CONTROL, (control & ~OHCI_CTRL_HCFS_MASK) |
                                     OHCI_CTRL_HCFS_RESET);
        usb_wait_ticks(2);
    }
    ohci_write(OHCI_REG_CMDSTATUS, OHCI_CMD_HCR);
    for (i = 0u; i < 40000000u; ++i) {
        if ((ohci_read(OHCI_REG_CMDSTATUS) & OHCI_CMD_HCR) == 0u) {
            break;
        }
    }
    if (i >= 40000000u) {
        serial_trace("WARNING", "USB OHCI reset never cleared");
        return false;
    }
    /* The state the controller comes out of a software reset in is its own
     * business - parts sit in the reset state, others report suspend - and
     * the frame timing below is what settles it either way. */

    /* 12 Mbit timing: the frame interval and the largest packet a frame may
     * carry, with the toggle bit the specification asks software to flip.
     * PeriodicStart is placed at nine tenths of a frame and the low-speed
     * threshold at the specification's value. */
    fminterval = (ohci_read(OHCI_REG_FMINTERVAL) & 0x80000000u) ^ 0x80000000u;
    ohci_write(OHCI_REG_FMINTERVAL, fminterval | 0x2edfu | (0x2778u << 16));
    ohci_write(OHCI_REG_PERIODICSTART, (9u * 0x2edfu) / 10u);
    ohci_write(OHCI_REG_LSTHRESH, 0x628u);

    for (i = 0u; i < OHCI_HCCA_DWORDS; ++i) {
        ohci_hcca[ohci_cur][i] = 0u;
    }
    ohci_write(OHCI_REG_HCCA, (uint32_t)(uintptr_t)ohci_hcca[ohci_cur]);
    ohci_write(OHCI_REG_INTRSTATUS, 0xFFFFFFFFu);
    ohci_write(OHCI_REG_INTRDISABLE, 0xFFFFFFFFu);   /* polled, never interrupt */

    rhdesca = ohci_read(OHCI_REG_RHDESCA);
    ohci.ports = rhdesca & OHCI_RHA_NDP_MASK;
    serial_trace_hex_value("INFO", "USB OHCI root hub descriptor", rhdesca);
    if (ohci.ports == 0u || ohci.ports > OHCI_MAX_PORTS) {
        ohci.ports = ohci.ports == 0u ? 0u : OHCI_MAX_PORTS;
    }
    /* Power switching is software's job only when the board says so: with no
     * power switching the ports are always powered and the power bit reads
     * back set. */
    if ((rhdesca & OHCI_RHA_PSM) != 0u && (rhdesca & OHCI_RHA_NPS) == 0u) {
        ohci_write(OHCI_REG_RHSTATUS, OHCI_RHS_LPSC);
        potpgt = (rhdesca >> OHCI_RHA_POTPGT_SHIFT) & 0xFFu;
        if (potpgt != 0u) {
            usb_wait_ticks(potpgt * (TIMER_HZ / 1000u) + 1u);
        }
    }

    ohci_write(OHCI_REG_INTRSTATUS, 0xFFFFFFFFu);
    ohci_write(OHCI_REG_CONTROL, OHCI_CTRL_CBSR | OHCI_CTRL_CLE |
                                 OHCI_CTRL_HCFS_OPER);
    ohci.next_address = 0u;
    spi = ohci_read(OHCI_REG_REVISION);
    serial_trace_hex_value("INFO", "USB OHCI revision", spi);
    ohci.started = true;
    serial_trace("INFO", "USB OHCI controller started");
    usb_wait_ticks(2);
    return true;
}

static void ohci_scan_ports(void) {
    uint32_t port;
    uint32_t connected = 0u;

    for (port = 1u; port <= ohci.ports; ++port) {
        uint32_t status = ohci_read(OHCI_REG_RHPORT(port));
        bool low_speed = false;
        uint32_t i;
        bool known = false;

        if ((status & OHCI_PORT_CCS) == 0u) {
            continue;
        }
        ++connected;
        for (i = 0u; i < OHCI_DEVICES; ++i) {
            if (ohci_device[ohci_cur][i].in_use &&
                ohci_device[ohci_cur][i].port == (uint8_t)port) {
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        serial_trace_uint_value("INFO", "USB OHCI scan port", port);
        serial_trace_hex_value("INFO", "  port status", status);
        if (!ohci_port_reset(port, &low_speed)) {
            continue;
        }
        ohci_enumerate(port, low_speed);
    }
    if (usb_ports_connected == 0u && connected != 0u) {
        usb_ports_connected = connected;
    }
}

static void usb_ohci_init_all(void) {
    UsbPciCursor cursor;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;

    usb_pci_cursor_reset(&cursor);
    while (ohci_count < USB_OHCI_MAX_CONTROLLERS &&
           usb_pci_find(&cursor, USB_PCI_PROGIF_OHCI, &bus, &slot, &function)) {
        ++usb_bus_found[USB_BUS_OHCI];
        ohci_cur = ohci_count;
        ohci_all[ohci_cur].started = false;
        if (!ohci_attach(bus, slot, function)) {
            continue;
        }
        serial_trace_hex_value("INFO", "USB OHCI controller at",
                               ((uint32_t)bus << 24) | ((uint32_t)slot << 16) |
                               ((uint32_t)function << 8));
        if (!ohci_start()) {
            continue;
        }
        ++ohci_count;
        ++usb_bus_started[USB_BUS_OHCI];
        ohci_schedule_rebuild();
        ohci_scan_ports();
    }
    ohci_cur = 0u;
    serial_trace_uint_value("INFO", "USB OHCI devices", ohci_devices_total);
}

/* ---------------------------------------------------------------------- */
/* Frame loop                                                             */
/* ---------------------------------------------------------------------- */

static void ohci_device_poll(OhciDevice *device) {
    volatile uint32_t *td = device->td_interrupt;
    uint32_t condition;

    if (!device->in_use) {
        return;
    }
    condition = ohci_td_condition(td);
    if (condition == OHCI_TD_CC_NOTACCESSED) {
        return;                        /* still queued, or nothing to report */
    }
    if (condition != 0u && condition != 9u) {      /* 9 = data underrun */
        if (++device->errors <= 2u) {
            serial_trace_hex_value("WARNING", "USB OHCI report failed, condition",
                                   condition);
        }
        if (device->errors >= 8u) {
            return;                    /* give up on this endpoint */
        }
    } else {
        uint32_t delivered = device->transfer_bytes;

        device->toggle = !device->toggle;
        if (device->kind == USB_HID_KIND_KEYBOARD) {
            usb_route_keyboard(&device->route, device->report, delivered);
        } else if (device->kind == USB_HID_KIND_MOUSE) {
            usb_mouse_report(device->report, delivered);
        }
    }
    ohci_post_interrupt(device);
}

static void usb_ohci_poll_all(void) {
    uint32_t controller;

    for (controller = 0u; controller < ohci_count; ++controller) {
        uint32_t i;

        ohci_cur = controller;
        if (!ohci.started) {
            continue;
        }
        for (i = 0u; i < OHCI_DEVICES; ++i) {
            ohci_device_poll(&ohci_device[ohci_cur][i]);
        }
    }
    ohci_cur = 0u;
}

static void usb_ohci_rescan(void) {
    uint32_t controller;

    for (controller = 0u; controller < ohci_count; ++controller) {
        ohci_cur = controller;
        if (ohci.started) {
            ohci_scan_ports();
        }
    }
    ohci_cur = 0u;
}

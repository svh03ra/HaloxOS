// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: usb_ehci.c, USB 2.0 input through an EHCI host controller.

// This repository is licensed under the GNU General Public License.

/*
 * USB 2.0 machines put their high-speed devices on an EHCI controller, and
 * this is generally the last host controller family an x86 machine has:
 * xHCI covers USB 3 plus every port wired to a modern chipset, EHCI covers
 * the high-speed half of a USB 2.0 chipset, and UHCI (see usb_uhci.c) covers
 * the full and low-speed half, which is where keyboards usually are.
 *
 * The split is not arbitrary.  A USB 2.0 chipset has one EHCI block and up to
 * six UHCI companions, and the hardware routes a full or low-speed device
 * attached to a shared port to a companion by itself - the EHCI then reports
 * the port as "owned by companion" and never sees the keyboard, which is why
 * the two drivers have to exist side by side instead of one of them winning.
 * A machine with no companion at all leaves its full-speed devices with the
 * EHCI, so both cases are handled here.
 *
 * Transfers go through the controller's two schedules:
 *
 *   - control transfers through the asynchronous schedule, which starts at a
 *     queue head with the "head of reclamation list" bit set;
 *   - the interrupt IN endpoint of a HID device through the periodic
 *     schedule, reached from the frame list rather than by a link.
 *
 * Everything at boot is synchronous with bounded waits, so a controller that
 * never answers costs boot time rather than the boot itself; the frame loop
 * only re-arms completed interrupt transfers and reads their reports.  Those
 * reports go to the same decoder the xHCI driver uses, so a key is a key
 * whichever controller delivered it.
 */

#define EHCI_MAX_PORTS          15u
#define EHCI_DEVICES            2u      /* one keyboard and one mouse each */
#define EHCI_FRAME_LIST_SIZE    1024u   /* 4 KB, the only size we program */
#define EHCI_QH_DWORDS          32u     /* 18 in use, padded to 32-byte steps */
#define EHCI_QTD_DWORDS         8u
#define EHCI_QTD_MAX            4u

/* Operational registers, relative to the operation base (BAR + CAPLENGTH). */
#define EHCI_OP_USBCMD          0x00u
#define EHCI_OP_USBSTS          0x04u
#define EHCI_OP_USBINTR         0x08u
#define EHCI_OP_FRINDEX         0x0Cu
#define EHCI_OP_CTRLDSSEG       0x10u
#define EHCI_OP_PERIODICBASE    0x14u
#define EHCI_OP_ASYNCLISTADDR   0x18u
#define EHCI_OP_CONFIGFLAG      0x40u
#define EHCI_OP_PORTSC          0x44u

/* Capability registers, relative to the BAR. */
#define EHCI_CAP_CAPLENGTH      0x00u
#define EHCI_CAP_HCSPARAMS      0x04u
#define EHCI_CAP_HCCPARAMS      0x08u

#define EHCI_CMD_RUN            0x00000001u
#define EHCI_CMD_RESET          0x00000002u
#define EHCI_CMD_PSE            0x00000010u
#define EHCI_CMD_ASE            0x00000020u
#define EHCI_CMD_ITC_8          0x00080000u   /* interrupt every 8 microframes */

#define EHCI_STS_HALT           0x00001000u

/*
 * HCSPARAMS: bits 3:0 number of ports, bit 4 port power control, bits 11:8
 * ports per companion controller, bits 15:12 companion controllers.
 *
 * The companion fields matter more than they look: they are how the driver
 * knows whether the ports are shared with the OHCI or UHCI companions that a
 * USB 2.0 chipset pairs with its EHCI block, and therefore whether claiming
 * the ports here would take the keyboard away from the controller that can
 * actually talk to it.  Reading them from the wrong bits (they sit at 15:12,
 * not 7:4) makes every board look companion-less.
 */
#define EHCI_HCS_N_PORTS        0x0000000Fu
#define EHCI_HCS_PPC            0x00000010u
#define EHCI_HCS_N_PCC          0x00000F00u   /* bits 11:8 ports per companion */
#define EHCI_HCS_N_CC           0x0000F000u   /* bits 15:12 companions */
#define EHCI_HCS_N_CC_POS       12u
#define EHCI_HCC_EXT_CAPS       0x0000FF00u   /* bits 15:8: extended cap offset */

#define EHCI_PORT_CONNECT       0x00000001u
#define EHCI_PORT_CSC           0x00000002u
#define EHCI_PORT_PE            0x00000004u
#define EHCI_PORT_PEC           0x00000008u
#define EHCI_PORT_OCC           0x00000020u
#define EHCI_PORT_OWNER         0x00002000u
#define EHCI_PORT_POWER         0x00001000u
#define EHCI_PORT_RESET         0x00000100u
#define EHCI_PORT_SUSPEND       0x00000080u
#define EHCI_PORT_RESUME        0x00000040u
#define EHCI_PORT_SPEED_MASK    0x0C000000u   /* bits 27:26: 0 full, 1 low, 2 high */
#define EHCI_PORT_SPEED_POS     26u
/* Queue head, first part (EHCI 3.6). */
#define EHCI_QH_TYPE            0x00000002u   /* type tag in the link word */
#define EHCI_QH_LINK(ptr)       (((uint32_t)(uintptr_t)(ptr) & ~0x1Fu) | EHCI_QH_TYPE)
#define EHCI_LIST_END           0x00000001u   /* "null pointer" to the hardware */

#define EHCI_QH_C               0x08000000u   /* control endpoint flag */
#define EHCI_QH_HEAD            0x00008000u   /* head of reclamation list */
#define EHCI_QH_TOGGLE_CTL      0x00004000u   /* data toggle comes from the qTD */
#define EHCI_QH_RL(n)           ((uint32_t)(n) << 28)
#define EHCI_QH_SPEED_FULL      (0u << 12)
#define EHCI_QH_SPEED_LOW       (1u << 12)
#define EHCI_QH_SPEED_HIGH      (2u << 12)
#define EHCI_QH_MULT_1          (1u << 30)    /* one packet per micro-frame */

/* Queue element transfer descriptor (EHCI 3.5). */
#define EHCI_QTD_ACTIVE         0x00000080u
#define EHCI_QTD_HALT           0x00000040u
#define EHCI_QTD_TOGGLE         0x80000000u
#define EHCI_QTD_IOC            0x00008000u
#define EHCI_QTD_CERR(n)        ((uint32_t)(n) << 10)
#define EHCI_QTD_PID(p)         ((uint32_t)(p) << 8)
#define EHCI_QTD_PID_OUT        0u
#define EHCI_QTD_PID_IN         1u
#define EHCI_QTD_PID_SETUP      2u
#define EHCI_QTD_LENGTH(token)  (((token) >> 16) & 0x7FFFu)

typedef struct {
    volatile uint32_t *cap;
    volatile uint32_t *op;
    uint32_t ports;
    uint32_t hcs_params;
    uint32_t companions;            /* companion controllers sharing the ports */
    bool ppc;                       /* port power is software controlled */
    bool started;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t next_address;
} Ehci;

static Ehci ehci_all[USB_EHCI_MAX_CONTROLLERS];
static uint32_t ehci_cur = 0u;
static uint32_t ehci_count = 0u;
#define ehci ehci_all[ehci_cur]

typedef struct {
    bool in_use;
    bool high_speed;
    bool low_speed;
    uint8_t controller;
    uint8_t address;
    uint8_t port;
    uint8_t kind;
    uint8_t ep_number;
    uint16_t ep0_mps;                   /* endpoint 0 packet size */
    uint16_t ep_mps;                    /* interrupt endpoint packet size */
    uint8_t interval;
    uint16_t transfer_bytes;
    uint32_t errors;
    UsbHidRoute route;
    volatile uint32_t *qh_control;      /* async schedule: endpoint 0 */
    volatile uint32_t *qh_interrupt;    /* periodic schedule: the HID IN */
    volatile uint32_t *qtd;             /* control transfer qTD chain */
    volatile uint32_t *qtd_interrupt;
    volatile uint8_t *report;
} EhciDevice;

static EhciDevice ehci_device[USB_EHCI_MAX_CONTROLLERS][EHCI_DEVICES];
static uint32_t ehci_devices_total = 0u;

static volatile uint32_t ehci_frame_list[USB_EHCI_MAX_CONTROLLERS][EHCI_FRAME_LIST_SIZE]
    __attribute__((aligned(4096)));
static volatile uint32_t ehci_async_head[USB_EHCI_MAX_CONTROLLERS][EHCI_QH_DWORDS]
    __attribute__((aligned(32)));
static volatile uint32_t ehci_control_qh[USB_EHCI_MAX_CONTROLLERS][EHCI_DEVICES][EHCI_QH_DWORDS]
    __attribute__((aligned(32)));
static volatile uint32_t ehci_interrupt_qh[USB_EHCI_MAX_CONTROLLERS][EHCI_DEVICES][EHCI_QH_DWORDS]
    __attribute__((aligned(32)));
static volatile uint32_t ehci_transfer_qtd[USB_EHCI_MAX_CONTROLLERS][EHCI_DEVICES][EHCI_QTD_MAX][EHCI_QTD_DWORDS]
    __attribute__((aligned(32)));
static volatile uint32_t ehci_interrupt_qtd[USB_EHCI_MAX_CONTROLLERS][EHCI_DEVICES][EHCI_QTD_DWORDS]
    __attribute__((aligned(32)));
static volatile uint8_t ehci_report[USB_EHCI_MAX_CONTROLLERS][EHCI_DEVICES][USB_REPORT_BYTES]
    __attribute__((aligned(64)));
static uint8_t ehci_setup_packet[8] __attribute__((aligned(8)));
static uint8_t ehci_descriptor_probe[18] __attribute__((aligned(8)));

/*
 * EHCI diagnostics for the boot menu, in the same spirit as the xHCI ones:
 * what the last port this driver touched looked like, why the attempt on it
 * ended, and the qTD token of the last transfer that failed.  A machine whose
 * keyboard sits on an EHCI port reads as "EHCI 2/2 and nothing else", which
 * is indistinguishable from a machine whose keyboard is unplugged until these
 * say which port was seen and what the hardware answered.
 */
uint32_t usb_ehci_routed_ports = 0u;    /* pins handed to the xHCI on purpose */
static uint32_t ehci_last_port = 0u;
static uint32_t ehci_last_portsc = 0u;
static uint32_t ehci_last_speed = 0u;
static const char *ehci_last_reason = "none";
static uint32_t ehci_last_token = 0u;

static void ehci_note_port(uint32_t port, uint32_t portsc, const char *reason) {
    ehci_last_port = port;
    ehci_last_portsc = portsc;
    ehci_last_reason = reason;
    ehci_last_speed = (portsc & EHCI_PORT_SPEED_MASK) >> EHCI_PORT_SPEED_POS;
}

/* Per-port retry bookkeeping: a port whose enumeration failed is left alone
 * for a growing while (2 s doubling to 16 s), instead of being retried every
 * half-second for the rest of the boot.  A full-speed keyboard on a root port
 * of a companion-less EHCI never succeeds, and retrying it forever blocked
 * the frame loop for two thirds of a second every half-second - the "sudden
 * lag frame drops" a machine with a dead USB port showed. */
#define EHCI_PORT_MAX 16u
static uint32_t ehci_port_next_try[USB_EHCI_MAX_CONTROLLERS][EHCI_PORT_MAX];
static uint8_t ehci_port_fails[USB_EHCI_MAX_CONTROLLERS][EHCI_PORT_MAX];

static void ehci_port_backoff(uint32_t port, const char *reason) {
    uint32_t slot = port - 1u < EHCI_PORT_MAX ? port - 1u : EHCI_PORT_MAX - 1u;
    uint32_t fails;

    if (ehci_port_fails[ehci_cur][slot] < 4u) {
        ++ehci_port_fails[ehci_cur][slot];
    }
    fails = ehci_port_fails[ehci_cur][slot];
    ehci_port_next_try[ehci_cur][slot] =
        timer_ticks + (2u << (fails - 1u < 3u ? fails - 1u : 3u)) * TIMER_HZ;
    ehci_last_reason = reason;
}

static void ehci_relink_async(void);
static void ehci_frame_list_rebuild(void);
static void ehci_scan_ports(void);

/* ---------------------------------------------------------------------- */
/* Register access (the MMIO helpers themselves are shared with xHCI)     */
/* ---------------------------------------------------------------------- */

static volatile uint32_t *ehci_reg(uint32_t offset) {
    return ehci.op + offset / 4u;
}

static volatile uint32_t *ehci_portsc(uint32_t port) {
    return ehci.op + (EHCI_OP_PORTSC + 4u * (port - 1u)) / 4u;
}

/* ---------------------------------------------------------------------- */
/* Taking the controller over from the firmware                           */
/* ---------------------------------------------------------------------- */

/*
 * USB Legacy Support (PCI extended capability 1).  As on xHCI, the firmware
 * can keep the ports behind an SMM driver; until it releases them, the ports
 * report what its own driver left behind rather than what is attached.
 *
 * The capability lives in PCI configuration space and has the same three
 * dword layout the xHCI one has in the MMIO window:
 *
 *   cap+0  header (capability ID and next pointer)
 *   cap+4  USBLEGSUP: bit 16 BIOS Owned Semaphore, bit 24 OS Owned Semaphore
 *   cap+8  USBLEGCTLSTS: the SMI generation enables
 *
 * The OS owned semaphore is requested at cap+4, the firmware clears its own
 * bit there when it lets go, and - if it will not - the SMI enables are
 * zeroed and the BIOS owned bit is cleared by force, exactly as the xHCI
 * handoff does it.  Writing the semaphore bits into the header dword, which
 * is what reading "set the OS owned semaphore" naively produces, talks to
 * no register at all while appearing to succeed: the header's reserved bits
 * read zero, so the wait for the BIOS owned bit to clear passes instantly.
 */
#define EHCI_LEGACY_BIOS_OWNED 0x00010000u
#define EHCI_LEGACY_OS_OWNED   0x01000000u
#define EHCI_LEGACY_WAIT_TICKS 64u

static void ehci_take_legacy(uint32_t hccparams) {
    uint32_t cap = (hccparams & EHCI_HCC_EXT_CAPS) >> 8;
    uint32_t guard;

    for (guard = 0u; guard < 32u && cap >= 0x40u; ++guard) {
        uint32_t header = pci_config_read32(ehci.bus, ehci.slot, ehci.function, cap);
        uint32_t id = header & 0xFFu;
        uint32_t next = (header >> 8) & 0xFFu;

        if (id == 1u) {
            uint32_t value = pci_config_read32(ehci.bus, ehci.slot, ehci.function,
                                               cap + 4u);
            uint32_t ticks;

            serial_trace_hex_value("INFO", "USB EHCI legacy ownership", value);
            if ((value & EHCI_LEGACY_BIOS_OWNED) != 0u) {
                pci_config_write32(ehci.bus, ehci.slot, ehci.function, cap + 4u,
                                   value | EHCI_LEGACY_OS_OWNED);
                for (ticks = 0u; ticks < EHCI_LEGACY_WAIT_TICKS; ++ticks) {
                    if ((pci_config_read32(ehci.bus, ehci.slot, ehci.function,
                                            cap + 4u) &
                         EHCI_LEGACY_BIOS_OWNED) == 0u) {
                        /* Ownership moved: disarm every SMI trap now.  An
                         * armed trap that fires on this driver's own port
                         * writes is how the firmware's handler re-grabs a
                         * controller mid-enumeration (measured on an
                         * AMI/C SM board: port stuck in reset, command ring
                         * silent, status register healthy). */
                        pci_config_write32(ehci.bus, ehci.slot, ehci.function,
                                           cap + 8u, 0u);
                        serial_trace("INFO", "USB EHCI: firmware handed the ports over");
                        return;
                    }
                    usb_wait_ticks(1u);
                }
                /* It would not let go: take the controller over anyway,
                 * with SMI generation off so its handler cannot interrupt
                 * the bring-up that follows. */
                pci_config_write32(ehci.bus, ehci.slot, ehci.function, cap + 8u, 0u);
                value = pci_config_read32(ehci.bus, ehci.slot, ehci.function,
                                          cap + 4u);
                pci_config_write32(ehci.bus, ehci.slot, ehci.function, cap + 4u,
                                   (value & ~EHCI_LEGACY_BIOS_OWNED) |
                                   EHCI_LEGACY_OS_OWNED);
                serial_trace("WARNING",
                             "USB EHCI: firmware kept ownership, taken by force");
                return;
            }
            /* Nobody owns the ports, but SMI traps can still be armed. */
            pci_config_write32(ehci.bus, ehci.slot, ehci.function, cap + 8u, 0u);
            return;
        }
        if (next == 0u || next == cap) {
            return;
        }
        cap = next;
    }
}

/* ---------------------------------------------------------------------- */
/* Queue heads and transfer descriptors                                   */
/* ---------------------------------------------------------------------- */

static void ehci_qh_clear(volatile uint32_t *qh) {
    uint32_t i;

    for (i = 0u; i < EHCI_QH_DWORDS; ++i) {
        qh[i] = 0u;
    }
    qh[0] = EHCI_LIST_END;          /* horizontal link: end of the list */
    qh[4] = EHCI_LIST_END;          /* next qTD: nothing queued */
    qh[5] = EHCI_LIST_END;          /* alternate next qTD */
}

/*
 * Point one control queue head at a device address.  The endpoint
 * characteristics carry the address in bits 6:0, the endpoint number in
 * 11:8, the maximum packet size in 26:16 and the speed in 13:12.  A full or
 * low-speed control endpoint also needs the control-endpoint flag, and the
 * data toggle has to come from the qTD rather than be maintained by the
 * controller, or the first packet of every transfer is sent with the wrong
 * toggle.
 */
static void ehci_control_qh_configure(EhciDevice *device, uint8_t address) {
    volatile uint32_t *qh = device->qh_control;
    uint32_t info1 = (uint32_t)address | ((uint32_t)device->ep0_mps << 16);

    device->address = address;
    if (device->high_speed) {
        info1 |= EHCI_QH_SPEED_HIGH | EHCI_QH_TOGGLE_CTL | EHCI_QH_RL(3u);
    } else {
        info1 |= EHCI_QH_TOGGLE_CTL | EHCI_QH_C | EHCI_QH_RL(3u);
        info1 |= device->low_speed ? EHCI_QH_SPEED_LOW : EHCI_QH_SPEED_FULL;
    }
    qh[1] = info1;
    qh[2] = 0u;                    /* no TT: the root port owns the device */
}

/* Fill one qTD.  A buffer that runs off the end of a page continues at the
 * start of the next one, which is what the buffer list inside a qTD is for;
 * nothing here is larger than a descriptor, so two entries are the most any
 * transfer needs - but two entries are always enough. */
static void ehci_qtd_fill(volatile uint32_t *qtd, uint32_t next, uint32_t buffer,
                          uint32_t length, uint32_t pid, bool toggle, bool ioc) {
    uint32_t first_page = 0x1000u - (buffer & 0x0FFFu);
    uint32_t i;

    qtd[0] = next;
    qtd[1] = EHCI_LIST_END;        /* alternate next: this chain never branches */
    qtd[2] = (length << 16) | EHCI_QTD_CERR(3u) | EHCI_QTD_PID(pid) |
             (toggle ? EHCI_QTD_TOGGLE : 0u) | (ioc ? EHCI_QTD_IOC : 0u) |
             EHCI_QTD_ACTIVE;
    for (i = 3u; i < EHCI_QTD_DWORDS; ++i) {
        qtd[i] = 0u;
    }
    qtd[3] = buffer;
    if (length > first_page) {
        qtd[4] = (buffer + first_page) & ~0x0FFFu;
    }
}

static uint32_t ehci_qtd_next(volatile uint32_t *qtd) {
    return (uint32_t)(uintptr_t)qtd & ~0x1Fu;
}

/* Wait for the last qTD of a chain to leave the active state, or for any qTD
 * in it to halt.  Bounded twice over - by timer ticks and by a spin cap - so
 * a device that never answers cannot hold up the boot or the frame loop. */
static bool ehci_wait_chain(volatile uint32_t *chain, uint32_t count,
                            volatile uint32_t *last, uint32_t ticks) {
    uint32_t start = timer_ticks;
    uint32_t spins = 0u;

    for (;;) {
        uint32_t i;

        if ((last[2] & EHCI_QTD_ACTIVE) == 0u) {
            return true;
        }
        for (i = 0u; i < count; ++i) {
            if ((chain[i * EHCI_QTD_DWORDS + 2u] & EHCI_QTD_HALT) != 0u) {
                return false;              /* the controller gave up on it */
            }
        }
        if (timer_ticks - start >= ticks || ++spins > 40000000u) {
            serial_trace("WARNING", "USB EHCI transfer timed out");
            return false;
        }
    }
}

/*
 * One control transfer: setup, optional data, status, as a single chain of
 * qTDs.  The status stage always runs the other way from the data stage, and
 * the last qTD carries the interrupt-on-complete that says the whole thing is
 * done - which is why the status qTD is the one that is waited on.
 */
static bool ehci_control_transfer(EhciDevice *device, uint8_t request_type,
                                  uint8_t request, uint16_t value, uint16_t index,
                                  uint16_t length, uint8_t *buffer) {
    volatile uint32_t *qh = device->qh_control;
    volatile uint32_t *chain = device->qtd;
    volatile uint32_t *last;
    uint32_t count;
    bool in = (request_type & 0x80u) != 0u;

    ehci_setup_packet[0] = request_type;
    ehci_setup_packet[1] = request;
    ehci_setup_packet[2] = (uint8_t)(value & 0xFFu);
    ehci_setup_packet[3] = (uint8_t)(value >> 8);
    ehci_setup_packet[4] = (uint8_t)(index & 0xFFu);
    ehci_setup_packet[5] = (uint8_t)(index >> 8);
    ehci_setup_packet[6] = (uint8_t)(length & 0xFFu);
    ehci_setup_packet[7] = (uint8_t)(length >> 8);

    ehci_qtd_fill(&chain[0], 0u, (uint32_t)(uintptr_t)ehci_setup_packet, 8u,
                  EHCI_QTD_PID_SETUP, false, false);
    /*
     * The three stages are three transfers descriptors, each a fixed number
     * of double words apart.  They are addressed with that stride rather than
     * as neighbouring words: writing "the second descriptor" as chain[1]
     * lands inside the first one (its second word is the alternate-next
     * pointer), which leaves a chain the controller reads as one malformed
     * descriptor - it runs, reports completion, and moves no data at all.
     */
    if (length != 0u) {
        ehci_qtd_fill(&chain[EHCI_QTD_DWORDS], 0u, (uint32_t)(uintptr_t)buffer,
                      length, in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT, true, false);
        ehci_qtd_fill(&chain[2u * EHCI_QTD_DWORDS], EHCI_LIST_END, 0u, 0u,
                      in ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN, true, true);
        chain[0] = ehci_qtd_next(&chain[EHCI_QTD_DWORDS]);
        chain[EHCI_QTD_DWORDS] = ehci_qtd_next(&chain[2u * EHCI_QTD_DWORDS]);
        last = &chain[2u * EHCI_QTD_DWORDS];
        count = 3u;
    } else {
        /* No data stage: the status stage is always an IN (EHCI 3.5.3). */
        ehci_qtd_fill(&chain[EHCI_QTD_DWORDS], EHCI_LIST_END, 0u, 0u,
                      EHCI_QTD_PID_IN, true, true);
        chain[0] = ehci_qtd_next(&chain[EHCI_QTD_DWORDS]);
        last = &chain[EHCI_QTD_DWORDS];
        count = 2u;
    }

    /* Arm the queue head.  The overlay token is cleared first: a halted
     * transfer leaves its halt bit in the overlay, and a queue head that
     * starts out halted executes nothing at all. */
    qh[3] = 0u;
    qh[4] = (uint32_t)(uintptr_t)chain;
    qh[5] = EHCI_LIST_END;
    qh[6] = 0u;

    /* 12 ticks (~200 ms) rather than 40: a control transfer a healthy device
     * answers completes in a few milliseconds, so this bound is only ever hit
     * by a device that will not answer - and it runs inside the frame loop,
     * where every tick of it is a frame the whole desktop does not draw. */
    if (!ehci_wait_chain(chain, count, last, 12u) ||
        (last[2] & (EHCI_QTD_ACTIVE | EHCI_QTD_HALT)) != 0u) {
        ehci_last_token = last[2];
        serial_trace_uint_value("WARNING", "USB EHCI transfer failed, request",
                                request);
        serial_trace_hex_value("WARNING", "  status qTD token", last[2]);
        serial_trace_hex_value("WARNING", "  setup qTD token", chain[2]);
        serial_trace_hex_value("WARNING", "  data qTD token",
                               chain[EHCI_QTD_DWORDS + 2u]);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Ports and enumeration                                                  */
/* ---------------------------------------------------------------------- */

/*
 * Every write to PORTSC is a read-modify-write of the whole register with the
 * change bits cleared in the written value.
 *
 * That is not a style choice.  The status bits are not all writable and the
 * change bits are "write one to clear", so a write that carries only the bits
 * the driver means to change silently clears everything it left out - and the
 * first thing that goes is the port enable bit the controller just raised,
 * after which the port reads as connected but disabled, every transfer is
 * dropped with "no device attached", and the driver sees timeouts rather than
 * a port it can use.  Reading the register back in and changing only the
 * intended bits is how the controller expects to be driven.
 */
static void ehci_port_write(volatile uint32_t *portsc, uint32_t value,
                            uint32_t set, uint32_t clear, uint32_t change) {
    usb_write32(portsc, (((value & ~(EHCI_PORT_CSC | EHCI_PORT_PEC | EHCI_PORT_OCC)) |
                          set | change) & ~clear));
}

/*
 * Hand one port's USB2 pins to the xHCI, on Intel chipsets that mux them.
 *
 * EHCI hardware speaks high speed on a root port and nothing else: a full or
 * low-speed device there is normally released to a companion controller, and
 * a companion-less EHCI (a board whose firmware disabled its UHCI blocks,
 * which is what a "xHCI mode" BIOS setting does) has no way to serve the
 * device at all.  Force-enabling the port lets the reset succeed and then
 * every transfer to it times out, which is how a machine ends up with a
 * keyboard that enumerates to the descriptor stage and never answers.
 *
 * The 7/8-series PCH muxes the USB2 pins between each EHCI and the xHCI:
 * PCI 0xD0 (XUSB2PR) selects which pins the xHCI owns, and PCI 0xD4
 * (XUSB2PRM) is the mask of bits software is allowed to change.  Routing the
 * failed pin to the xHCI - which speaks every speed natively - is the one
 * mechanism this chipset offers, so it is used exactly for this case: Intel
 * vendor, no companion, a started xHCI, a routable bit, and a port that just
 * proved to carry something that is not high speed.  The pin switches, the
 * device sees the upstream port go away and come back, and the xHCI's own
 * rescan - it runs every half second - picks it up.
 */
static bool ehci_port_route_to_xhci(uint32_t port) {
    uint32_t vendor = pci_config_read32(ehci.bus, ehci.slot, ehci.function, 0x00)
                      & 0xFFFFu;
    uint32_t mask;
    uint32_t routing;
    uint32_t bit;

    if (vendor != 0x8086u || usb_bus_started[USB_BUS_XHCI] == 0u) {
        return false;
    }
    if (ehci.companions != 0u) {
        return false;               /* the companion path is the normal one */
    }
    if (port == 0u || port > 31u) {
        return false;
    }
    mask = pci_config_read32(ehci.bus, ehci.slot, ehci.function, 0xD4u);
    bit = 1u << (port - 1u);
    if ((mask & bit) == 0u) {
        return false;               /* this pin is not software-routable */
    }
    routing = pci_config_read32(ehci.bus, ehci.slot, ehci.function, 0xD0u);
    pci_config_write32(ehci.bus, ehci.slot, ehci.function, 0xD0u,
                       (routing & ~mask) | ((routing | bit) & mask));
    ++usb_ehci_routed_ports;
    serial_trace_uint_value("INFO", "USB EHCI port pins routed to xHCI, port",
                            port);
    serial_trace_hex_value("INFO", "USB EHCI routing register now",
                           pci_config_read32(ehci.bus, ehci.slot,
                                             ehci.function, 0xD0u));
    return true;
}

static bool ehci_port_reset(uint32_t port, uint32_t *speed_out) {
    volatile uint32_t *portsc = ehci_portsc(port);
    uint32_t value = usb_read32(portsc);

    if ((value & EHCI_PORT_CONNECT) == 0u) {
        ehci_note_port(port, value, "no connect");
        return false;
    }
    if ((value & EHCI_PORT_OWNER) != 0u) {
        /* The companion controller owns this port, so the device on it is a
         * full or low-speed one and belongs to the UHCI driver. */
        ehci_note_port(port, value, "companion port");
        return false;
    }
    if (ehci.ppc && (value & EHCI_PORT_POWER) == 0u) {
        ehci_port_write(portsc, value, EHCI_PORT_POWER, 0u, 0u);
        usb_wait_ticks(2);
        value = usb_read32(portsc);
    }
    /*
     * The port reset belongs to software: the reset bit is set, held long
     * enough for the bus reset the USB specification asks for, and then
     * cleared by the same driver.  Waiting for the controller to clear it
     * waits forever, which is exactly what a first version of this did -
     * the port stayed in reset and every scan timed out on it.
     */
    ehci_port_write(portsc, value, EHCI_PORT_POWER | EHCI_PORT_RESET, 0u, 0u);
    usb_wait_ticks(5);                             /* about 80 ms of bus reset */
    value = usb_read32(portsc);
    /* Releasing the reset clears it explicitly: it is an ordinary state bit,
     * not a change bit, so leaving it out of the written value would keep the
     * port in reset for good. */
    ehci_port_write(portsc, value, EHCI_PORT_POWER, EHCI_PORT_RESET, 0u);
    usb_wait_ticks(2);
    value = usb_read32(portsc);

    /*
     * A high-speed device has its enable bit raised by the controller when
     * the reset completes.  A full or low-speed one does not: the controller
     * leaves the bit down and expects the port router to hand the device to a
     * companion controller.  On a controller with no companion the bit is
     * still raised below - the port then answers its status - but a transfer
     * to a non-high-speed root port is one the EHCI hardware cannot
     * schedule, so the xHCI routing below is what actually serves the
     * device when the chipset offers those pins as the way out.
     */
    if ((value & EHCI_PORT_PE) == 0u && (value & EHCI_PORT_OWNER) == 0u &&
        (value & EHCI_PORT_CONNECT) != 0u) {
        ehci_port_write(portsc, value, EHCI_PORT_POWER | EHCI_PORT_PE, 0u, 0u);
        usb_wait_ticks(2);
        value = usb_read32(portsc);
        if ((value & EHCI_PORT_PE) == 0u) {
            serial_trace_hex_value("WARNING", "USB EHCI port will not enable, PORTSC",
                                   value);
            ehci_note_port(port, value, "will not enable");
            return false;
        }
    }
    /* Clear the change bits this reset raised, so the next scan sees only
     * what happens after it. */
    ehci_port_write(portsc, value, 0u, 0u,
                    EHCI_PORT_CSC | EHCI_PORT_PEC | EHCI_PORT_OCC);
    value = usb_read32(portsc);
    if ((value & (EHCI_PORT_OWNER | EHCI_PORT_PE)) != EHCI_PORT_PE) {
        ehci_note_port(port, value, "not enabled");  /* companion handoff */
        return false;
    }
    *speed_out = (value & EHCI_PORT_SPEED_MASK) >> EHCI_PORT_SPEED_POS;
    ehci_note_port(port, value, "reset ok");
    if (*speed_out != 2u && ehci_port_route_to_xhci(port)) {
        /* Not high speed, and this chipset's pins are now the xHCI's: the
         * device will re-attach there and this port needs no more retries
         * while the xHCI owns the pin. */
        ehci_port_backoff(port, "moved to xhci");
        return false;
    }
    return true;
}

static void ehci_interrupt_qh_configure(EhciDevice *device) {
    volatile uint32_t *qh = device->qh_interrupt;
    uint32_t info1 = (uint32_t)device->address |
                     ((uint32_t)device->ep_number << 8) |
                     ((uint32_t)device->ep_mps << 16);

    ehci_qh_clear(qh);
    if (device->high_speed) {
        info1 |= EHCI_QH_SPEED_HIGH;
    } else if (device->low_speed) {
        info1 |= EHCI_QH_SPEED_LOW;
    }
    qh[1] = info1;
    /*
     * Endpoint capabilities.  The split-transaction fields (hub address, port
     * number, and the start/complete masks) describe a device reached through
     * a hub, and a device on a root port is not one: they stay zero.  The
     * multiplier is the one field that always applies, and one packet per
     * micro-frame is right for a keyboard.  Leaving the masks at their reset
     * value, or writing a start mask with no hub behind it, schedules
     * transactions to a hub that does not exist.
     */
    qh[2] = EHCI_QH_MULT_1;
}

static void ehci_post_interrupt(EhciDevice *device) {
    volatile uint32_t *qtd = device->qtd_interrupt;
    volatile uint32_t *qh = device->qh_interrupt;

    ehci_qtd_fill(qtd, EHCI_LIST_END, (uint32_t)(uintptr_t)device->report,
                  device->transfer_bytes, EHCI_QTD_PID_IN, false, true);
    qh[3] = 0u;
    qh[4] = (uint32_t)(uintptr_t)qtd;
    qh[5] = EHCI_LIST_END;
    qh[6] = 0u;
}

/* Adapter so the shared descriptor walk can drive this driver's transfers. */
static bool ehci_transfer_adapter(void *context, uint8_t request_type,
                                  uint8_t request, uint16_t value, uint16_t index,
                                  uint16_t length, uint8_t *buffer) {
    return ehci_control_transfer((EhciDevice *)context, request_type, request, value,
                                 index, length, buffer);
}

static bool ehci_enumerate(uint32_t port, uint32_t speed_field) {
    EhciDevice *device;
    uint8_t device_mps = 0u;
    uint8_t ep_number = 0u;
    uint16_t ep_mps = 0u;
    uint8_t interval = 0u;
    uint8_t kind = 0u;
    uint8_t interface_number = 0u;
    uint32_t index;
    uint32_t i;

    for (index = 0u; index < EHCI_DEVICES; ++index) {
        if (!ehci_device[ehci_cur][index].in_use) {
            break;
        }
    }
    if (index >= EHCI_DEVICES) {
        return false;
    }
    device = &ehci_device[ehci_cur][index];
    for (i = 0u; i < (uint32_t)sizeof(*device); ++i) {
        ((uint8_t *)device)[i] = 0u;
    }
    device->in_use = true;
    device->controller = (uint8_t)ehci_cur;
    device->port = (uint8_t)port;
    device->high_speed = speed_field == 2u;
    device->low_speed = speed_field == 1u;
    device->ep0_mps = device->high_speed ? 64u : 8u;
    device->qh_control = ehci_control_qh[ehci_cur][index];
    device->qh_interrupt = ehci_interrupt_qh[ehci_cur][index];
    device->qtd = ehci_transfer_qtd[ehci_cur][index][0];
    device->qtd_interrupt = ehci_interrupt_qtd[ehci_cur][index];
    device->report = ehci_report[ehci_cur][index];

    ehci_qh_clear(device->qh_control);
    ehci_qh_clear(device->qh_interrupt);
    ehci_control_qh_configure(device, 0u);
    /* Control transfers only happen through the asynchronous schedule, so the
     * queue head has to be in it before the first packet is posted. */
    ehci_relink_async();
    usb_write32(ehci_reg(EHCI_OP_USBCMD),
                 usb_read32(ehci_reg(EHCI_OP_USBCMD)) | EHCI_CMD_ASE);

    usb_last_stage = USB_STAGE_DESCRIPTOR;
    serial_trace_uint_value("INFO", "USB EHCI device on port", port);
    serial_trace_uint_value("INFO", "  port speed", speed_field);

    /* The device descriptor is read once at address zero, which is enough to
     * learn endpoint 0's packet size; the rest of the descriptors wait until
     * the device has an address of its own. */
    if (!ehci_control_transfer(device, 0x80u, USB_REQ_GET_DESCRIPTOR,
                               (uint16_t)(USB_DESC_DEVICE << 8), 0u, 18u,
                               ehci_descriptor_probe)) {
        serial_trace_uint_value("WARNING", "USB EHCI device descriptor failed, port",
                                port);
        device->in_use = false;
        ehci_port_backoff(port, "descriptors failed");
        return false;
    }
    if (!device->high_speed && ehci_descriptor_probe[7] >= 8u) {
        /* A full or low-speed device names its own endpoint 0 packet size. */
        device->ep0_mps = ehci_descriptor_probe[7];
        ehci_control_qh_configure(device, 0u);
    }

    /* Address it, then talk to it by that address from here on. */
    usb_last_stage = USB_STAGE_ADDRESS;
    if (++ehci.next_address == 0u || ehci.next_address > 127u) {
        ehci.next_address = 1u;
    }
    if (!ehci_control_transfer(device, 0x00u, 5u, (uint16_t)ehci.next_address, 0u,
                               0u, NULL)) {
        serial_trace("WARNING", "USB EHCI set address failed");
        device->in_use = false;
        ehci_port_backoff(port, "set address failed");
        return false;
    }
    ehci_control_qh_configure(device, ehci.next_address);
    usb_wait_ticks((TIMER_HZ / 100u) == 0u ? 1u : (TIMER_HZ / 100u));   /* 10 ms */

    if (!usb_hid_identify(device, ehci_transfer_adapter, &device_mps, &ep_number,
                          &ep_mps, &interval, &kind, &interface_number)) {
        serial_trace_uint_value("WARNING", "USB EHCI device is not a boot HID, port",
                                port);
        device->in_use = false;
        ehci_port_backoff(port, "not a boot HID");
        return false;
    }
    (void)device_mps;
    usb_last_stage = USB_STAGE_CONFIG;
    if (!ehci_control_transfer(device, 0x00u, USB_REQ_SET_CONFIG, 1u, 0u, 0u, NULL)) {
        device->in_use = false;
        ehci_port_backoff(port, "set config failed");
        return false;
    }
    /* Boot protocol fixes the report layout at eight bytes for a keyboard and
     * three for a mouse, whatever the report descriptor would have said. */
    (void)ehci_control_transfer(device, 0x21u, USB_REQ_SET_PROTOCOL, 0u,
                                interface_number, 0u, NULL);

    device->ep_number = ep_number;
    device->ep_mps = ep_mps;
    device->interval = interval;
    device->kind = kind;
    device->transfer_bytes = kind == USB_HID_KIND_KEYBOARD ? 8u : 3u;
    ehci_interrupt_qh_configure(device);
    ehci_post_interrupt(device);
    usb_last_stage = USB_STAGE_NONE;
    ehci_frame_list_rebuild();
    usb_write32(ehci_reg(EHCI_OP_USBCMD),
                 usb_read32(ehci_reg(EHCI_OP_USBCMD)) | EHCI_CMD_PSE);

    ++ehci_devices_total;
    if (port <= EHCI_PORT_MAX) {
        ehci_port_fails[ehci_cur][port - 1u] = 0u;   /* it works: forget the past */
    }
    if (kind == USB_HID_KIND_KEYBOARD) {
        ++usb_keyboards_total;
        serial_trace("INFO", "USB EHCI keyboard ready");
    } else {
        ++usb_mice_total;
        serial_trace("INFO", "USB EHCI mouse ready");
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Schedule maintenance                                                   */
/* ---------------------------------------------------------------------- */

/*
 * Rebuild the asynchronous list: the head queue head, then one control queue
 * head per device, and the last of them pointing back at the head.
 *
 * The list is a ring, not a chain.  The controller marks the end of a pass by
 * arriving back at the queue head it started from - the one with the head-of-
 * reclamation bit, which is what the ASYNCLISTADDR register points at - so a
 * list that ends in a null pointer instead sends it off to whatever happens
 * to live at address zero, where it reads not-a-queue-head and acts on it.
 * That is a real fault: transfers issued through such a list complete without
 * moving a byte, and the controller writes over unrelated memory while
 * following the garbage it found.
 */
static void ehci_relink_async(void) {
    volatile uint32_t *head = ehci_async_head[ehci_cur];
    volatile uint32_t *previous = head;
    uint32_t i;

    head[1] = EHCI_QH_HEAD;
    for (i = 0u; i < EHCI_DEVICES; ++i) {
        volatile uint32_t *qh;

        if (!ehci_device[ehci_cur][i].in_use) {
            continue;
        }
        qh = ehci_device[ehci_cur][i].qh_control;
        previous[0] = EHCI_QH_LINK(qh);
        previous = qh;
    }
    previous[0] = EHCI_QH_LINK(head);
    usb_write32(ehci_reg(EHCI_OP_ASYNCLISTADDR), (uint32_t)(uintptr_t)head);
}

/*
 * An interrupt queue head is reached from the frame list rather than by a
 * link from another queue head, so the frame list is where the chain is
 * built: every frame points at the first interrupt queue head, and those
 * queue heads link to each other, so one frame polls all of them.
 */
static void ehci_frame_list_rebuild(void) {
    volatile uint32_t *first = NULL;
    volatile uint32_t *previous = NULL;
    uint32_t i;

    for (i = 0u; i < EHCI_DEVICES; ++i) {
        volatile uint32_t *qh = ehci_device[ehci_cur][i].qh_interrupt;

        if (!ehci_device[ehci_cur][i].in_use) {
            continue;
        }
        if (first == NULL) {
            first = qh;
        }
        if (previous != NULL) {
            previous[0] = EHCI_QH_LINK(qh);
        }
        previous = qh;
    }
    if (previous != NULL) {
        previous[0] = EHCI_LIST_END;
    }
    for (i = 0u; i < EHCI_FRAME_LIST_SIZE; ++i) {
        ehci_frame_list[ehci_cur][i] = first != NULL ? EHCI_QH_LINK(first)
                                                     : EHCI_LIST_END;
    }
}

/* ---------------------------------------------------------------------- */
/* Controller bring-up                                                    */
/* ---------------------------------------------------------------------- */

static bool ehci_attach(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t bar0 = pci_config_read32(bus, slot, function, 0x10);
    uint32_t command;

    /* The EHCI register window is a 32-bit memory BAR, so unlike the xHCI
     * one it is always inside the first 4 GB and needs no relocation. */
    if ((bar0 & 0x1u) != 0u) {
        return false;
    }
    ehci.cap = (volatile uint32_t *)(uintptr_t)(bar0 & ~0x0Fu);
    ehci.bus = bus;
    ehci.slot = slot;
    ehci.function = function;
    command = pci_config_read32(bus, slot, function, 0x04);
    pci_config_write32(bus, slot, function, 0x04, command | 0x00000006u);
    return true;
}

static bool ehci_start(void) {
    uint32_t caplength = usb_read32(ehci.cap + EHCI_CAP_CAPLENGTH / 4u) & 0xFFu;
    uint32_t hcs_params = usb_read32(ehci.cap + EHCI_CAP_HCSPARAMS / 4u);
    uint32_t hcc_params = usb_read32(ehci.cap + EHCI_CAP_HCCPARAMS / 4u);
    uint32_t i;

    if (caplength < 0x10u || caplength > 0x40u) {
        return false;
    }
    ehci.op = (volatile uint32_t *)((uint8_t *)ehci.cap + caplength);
    ehci.ports = hcs_params & EHCI_HCS_N_PORTS;
    ehci.hcs_params = hcs_params;
    ehci.ppc = (hcs_params & EHCI_HCS_PPC) != 0u;
    ehci.companions = (hcs_params & EHCI_HCS_N_CC) >> EHCI_HCS_N_CC_POS;
    if (ehci.ports == 0u || ehci.ports > EHCI_MAX_PORTS) {
        return false;
    }
    ehci_take_legacy(hcc_params);

    /* Reset: the controller stops and reports itself halted when it is done. */
    usb_write32(ehci_reg(EHCI_OP_USBCMD), 0u);
    usb_write32(ehci_reg(EHCI_OP_USBCMD), EHCI_CMD_RESET);
    if (!usb_wait_bits(ehci_reg(EHCI_OP_USBCMD), EHCI_CMD_RESET, 0u, 40000000u, 16u)) {
        serial_trace("WARNING", "USB EHCI reset never cleared");
        return false;
    }
    if (!usb_wait_bits(ehci_reg(EHCI_OP_USBSTS), EHCI_STS_HALT, EHCI_STS_HALT,
                       40000000u, 16u)) {
        serial_trace("WARNING", "USB EHCI never halted");
        return false;
    }
    usb_wait_ticks(2);

    /*
     * The configure flag routes the ports to this controller.  On a machine
     * whose ports are shared with companion controllers - what every USB 2.0
     * chipset does, and the only place a full-speed keyboard is ever found -
     * claiming them here would take the keyboard away from the companion that
     * can actually talk to it, so the flag is left exactly as the firmware
     * set it and the companion driver (usb_uhci.c) owns the device instead.
     *
     * On a controller with no companion at all there is nobody else to serve
     * the ports, so this driver claims them and handles full and low-speed
     * devices itself (see ehci_port_reset).
     */
    if (ehci.companions == 0u) {
        usb_write32(ehci_reg(EHCI_OP_CONFIGFLAG), 1u);
        serial_trace("INFO", "USB EHCI: no companion, claiming the ports");
    } else {
        serial_trace_uint_value("INFO", "USB EHCI companion controllers",
                                ehci.companions);
    }
    usb_write32(ehci_reg(EHCI_OP_CTRLDSSEG), 0u);
    usb_write32(ehci_reg(EHCI_OP_USBINTR), 0u);
    usb_write32(ehci_reg(EHCI_OP_FRINDEX), 0u);

    ehci_qh_clear(ehci_async_head[ehci_cur]);
    for (i = 0u; i < EHCI_FRAME_LIST_SIZE; ++i) {
        ehci_frame_list[ehci_cur][i] = EHCI_LIST_END;
    }
    usb_write32(ehci_reg(EHCI_OP_PERIODICBASE),
                 (uint32_t)(uintptr_t)ehci_frame_list[ehci_cur]);
    ehci_relink_async();          /* also a ring when it holds no devices */

    usb_write32(ehci_reg(EHCI_OP_USBCMD),
                 EHCI_CMD_RUN | EHCI_CMD_ITC_8 | EHCI_CMD_ASE | EHCI_CMD_PSE);
    if (!usb_wait_bits(ehci_reg(EHCI_OP_USBSTS), EHCI_STS_HALT, 0u, 40000000u, 16u)) {
        serial_trace("WARNING", "USB EHCI did not start");
        return false;
    }
    ehci.next_address = 0u;
    ehci.started = true;
    serial_trace_uint_value("INFO", "USB EHCI ports", ehci.ports);
    serial_trace_uint_value("INFO", "  companion controllers", ehci.companions);
    usb_wait_ticks(10);
    return true;
}

static void ehci_scan_ports(void) {
    uint32_t port;
    uint32_t connected = 0u;

    for (port = 1u; port <= ehci.ports; ++port) {
        uint32_t status = usb_read32(ehci_portsc(port));
        uint32_t speed = 0u;
        uint32_t i;
        bool known = false;

        if ((status & EHCI_PORT_CONNECT) == 0u) {
            continue;
        }
        ++connected;
        for (i = 0u; i < EHCI_DEVICES; ++i) {
            if (ehci_device[ehci_cur][i].in_use &&
                ehci_device[ehci_cur][i].port == (uint8_t)port) {
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        if (port <= EHCI_PORT_MAX &&
            (int32_t)(timer_ticks - ehci_port_next_try[ehci_cur][port - 1u]) < 0) {
            continue;                /* failed recently: leave it alone a while */
        }
        serial_trace_uint_value("INFO", "USB EHCI scan port", port);
        serial_trace_hex_value("INFO", "  PORTSC", status);
        if (!ehci_port_reset(port, &speed)) {
            ehci_port_backoff(port, ehci_last_reason);
            continue;                /* companion port, or nothing that enabled */
        }
        ehci_enumerate(port, speed);
    }
    if (usb_ports_connected < connected) {
        usb_ports_connected = connected;
    }
}

static void usb_ehci_init_all(void) {
    UsbPciCursor cursor;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;

    usb_pci_cursor_reset(&cursor);
    while (ehci_count < USB_EHCI_MAX_CONTROLLERS &&
           usb_pci_find(&cursor, USB_PCI_PROGIF_EHCI, &bus, &slot, &function)) {
        ++usb_bus_found[USB_BUS_EHCI];
        ehci_cur = ehci_count;
        ehci_all[ehci_cur].started = false;
        if (!ehci_attach(bus, slot, function)) {
            continue;
        }
        serial_trace_hex_value("INFO", "USB EHCI controller at",
                               ((uint32_t)bus << 24) | ((uint32_t)slot << 16) |
                               ((uint32_t)function << 8));
        if (!ehci_start()) {
            continue;
        }
        ++ehci_count;
        ++usb_bus_started[USB_BUS_EHCI];
        ehci_scan_ports();
    }
    ehci_cur = 0u;
    serial_trace_uint_value("INFO", "USB EHCI devices", ehci_devices_total);
}

/* ---------------------------------------------------------------------- */
/* Frame loop                                                             */
/* ---------------------------------------------------------------------- */

static void ehci_device_poll(EhciDevice *device) {
    volatile uint32_t *qtd = device->qtd_interrupt;
    uint32_t token;

    if (!device->in_use) {
        return;
    }
    token = qtd[2];
    if ((token & EHCI_QTD_ACTIVE) != 0u) {
        return;                              /* still waiting for a report */
    }
    if ((token & EHCI_QTD_HALT) != 0u) {
        if (++device->errors <= 2u) {
            serial_trace_uint_value("WARNING", "USB EHCI report failed, token",
                                    token);
        }
        if (device->errors >= 8u) {
            return;                          /* give up on this endpoint */
        }
    } else {
        uint32_t residual = EHCI_QTD_LENGTH(token);
        uint32_t delivered = device->transfer_bytes;

        if (residual < delivered) {
            delivered -= residual;
        }
        if (device->kind == USB_HID_KIND_KEYBOARD) {
            usb_route_keyboard(&device->route, device->report, delivered);
        } else if (device->kind == USB_HID_KIND_MOUSE) {
            usb_mouse_report(device->report, delivered);
        }
    }
    ehci_post_interrupt(device);
}

static void usb_ehci_poll_all(void) {
    uint32_t controller;

    for (controller = 0u; controller < ehci_count; ++controller) {
        uint32_t i;

        ehci_cur = controller;
        if (!ehci.started) {
            continue;
        }
        for (i = 0u; i < EHCI_DEVICES; ++i) {
            ehci_device_poll(&ehci_device[ehci_cur][i]);
        }
    }
    ehci_cur = 0u;
}

static void usb_ehci_rescan(void) {
    uint32_t controller;

    for (controller = 0u; controller < ehci_count; ++controller) {
        ehci_cur = controller;
        if (ehci.started) {
            ehci_scan_ports();
        }
    }
    ehci_cur = 0u;
}

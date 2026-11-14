// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: xhci_hid.c, native xHCI boot-protocol HID input driver.

// This repository is licensed under the GNU General Public License.

/*
 * Native xHCI HID keyboard and boot-mouse input.
 *
 * Some UEFI-CSM machines expose no usable 8042 controller and no INT 16h
 * keyboard service, yet the firmware itself booted from a USB device through
 * xHCI.  The firmware therefore has a working xHCI stack, but it does not
 * hand a legacy keyboard path to the OS.  This driver takes the smallest
 * possible slice of that controller:
 *
 *   - Normal initialisation never resets a running controller.  The boot
 *     medium is often on the same xHCI controller, and a reset would
 *     invalidate the firmware's USB mass-storage slot, breaking INT 13h and
 *     the on-medium bios-serial.log.  It adopts the firmware's endpoint.
 *   - On a running controller it issues no commands at all.  The firmware's
 *     command ring is left exactly as it was: inserting a command at the
 *     firmware's producer index and letting the xHC consume it desynchronises
 *     the firmware's own producer/consumer state, so its next command is
 *     never fetched.
 *   - It adopts the firmware's already-configured keyboard slot and endpoint
 *     rings instead of creating a new slot.  The first free TRB is located by
 *     walking each ring from the endpoint's dequeue pointer and following any
 *     Link TRBs; the producer then re-arms one TRB at a time.
 *   - Transfer events are routed to a private event ring on secondary
 *     interrupter 1 (TRB_INTR_TARGET(1)).  Interrupter 0 and the firmware's
 *     event ring are never touched, so the firmware's INT 13h stack keeps
 *     working.  If the controller has only one interrupter, the endpoint
 *     dequeue pointer is polled instead (best effort; some controllers defer
 *     the dequeue write-back).
 *   - If the controller is stopped there is no live firmware USB stack, so a
 *     full reset and a normal slot/endpoint enumeration are safe.
 *
 * A hard reset can leave the firmware with no HID slots at all: the previous
 * OS owned the controller, so the firmware skips USB input setup.  Normal
 * init cannot adopt what does not exist.  The boot check therefore retries
 * ten times, then xhci_boot_recover() flushes bios-serial.log, detaches the
 * medium, resets the controller, and enumerates the root ports itself.  That
 * reset necessarily invalidates the firmware's boot-stick slot; with no
 * keyboard the machine cannot continue, and the kernel, WAD and framebuffer
 * are already in RAM.  Input recovery takes priority at that point.
 *
 * The driver supports boot-protocol keyboards and mice on root ports.  A
 * boot keyboard report is eight bytes; a boot mouse report is three or four
 * bytes ([buttons][dx][dy][wheel?]).  Some gaming mice stay in report
 * protocol, where the known 30FA:1040 sends a seven-byte report ID 1 packet
 * with 16-bit X/Y.  The Transfer Event residual length identifies the true
 * report length, so both layouts are decoded without issuing commands to the
 * firmware's controller.  Hubs and unknown HID report formats are not
 * decoded.
 *
 * A second entry point, xhci_mouse_init(), runs when the PS/2/SMM keyboard is
 * healthy but the mouse is not.  It adopts a boot-mouse endpoint without
 * touching the keyboard endpoint: a lone endpoint must advertise an MPS of
 * four bytes or less (a boot keyboard needs eight), while a composite HID
 * device with two or more interrupt-IN endpoints contributes only its first
 * endpoint (interface zero, the mouse on the known 30FA:1040 hardware).  The
 * PS/2 and INT 16h keyboard paths remain active.
 */

#ifndef HALOXOS_FORCE_XHCI_HID
#define HALOXOS_FORCE_XHCI_HID  0
#endif

#define XHCI_MAX_SLOTS          64u
#define XHCI_MAX_PORTS          64u
#define XHCI_MAX_INTERRUPTERS   16u
#define XHCI_TRB_DWORDS         4u
#define XHCI_EVENT_TRBS         64u
#define XHCI_CMD_TRBS           64u
#define XHCI_EP0_TRBS           4u
#define XHCI_INT_TRBS           16u
#define XHCI_RING_SCAN_LIMIT    1024u
#define XHCI_MAX_CANDIDATES     8u
/* The real CSM board reports 16 scratchpad buffers after a platform
 * reset.  The xHCI spec allows up to 1023; 32 covers every machine seen
 * so far while keeping the static BSS footprint reasonable. */
#define XHCI_MAX_SCRATCHPADS    32u

#define XHCI_MMIO_CAPLENGTH     0x00u
#define XHCI_MMIO_HCSPARAMS1    0x04u
#define XHCI_MMIO_HCSPARAMS2    0x08u
#define XHCI_MMIO_HCCPARAMS1    0x10u
#define XHCI_MMIO_DBOFF         0x14u
#define XHCI_MMIO_RTSOFF        0x18u

/* Intel PCH USB port mux registers, in the xHCI function's PCI config space.
 * USB3PSSEN enables the SuperSpeed terminations; XUSB2PR selects whether the
 * USB2 pins belong to the xHC or to the companion EHCI.  The matching masks
 * say which bits software may change.  These registers do not exist on every
 * Intel xHCI, which is why the probe also requires an Intel EHCI companion. */
#define XHCI_PCI_VENDOR_INTEL   0x8086u
#define XHCI_PCI_XUSB2PR        0xD0u
#define XHCI_PCI_USB2PRM        0xD4u
#define XHCI_PCI_USB3_PSSEN     0xD8u
#define XHCI_PCI_USB3PRM        0xDCu
#define XHCI_PCI_USB_ROUTE_BITS 0xFFFFu

#define XHCI_OP_USBCMD          0x00u
#define XHCI_OP_USBSTS          0x04u
#define XHCI_OP_PAGESIZE        0x08u
#define XHCI_OP_CRCR            0x18u
#define XHCI_OP_DCBAAP          0x30u
#define XHCI_OP_CONFIG          0x38u
#define XHCI_OP_PORTSC          0x400u

/* Interrupter register set: interrupter 0 starts at RTSOFF + 0x20 and each
 * following interrupter is 0x20 bytes further on. */
#define XHCI_RT_INTR0           0x20u
#define XHCI_RT_INTR1           0x40u
#define XHCI_RT_STRIDE          0x20u
#define XHCI_RT_IMAN            0x00u
#define XHCI_RT_ERSTSZ          0x08u
#define XHCI_RT_ERSTBA          0x10u
#define XHCI_RT_ERDP            0x18u

#define XHCI_USBCMD_RUN         (1u << 0)
#define XHCI_USBCMD_HCRST       (1u << 1)
#define XHCI_USBCMD_INTE        (1u << 2)
#define XHCI_USBSTS_HCH         (1u << 0)
#define XHCI_USBSTS_EINT        (1u << 3)
#define XHCI_USBSTS_HSE         (1u << 12)
#define XHCI_USBSTS_CNR         (1u << 11)

#define XHCI_PORTSC_CCS         (1u << 0)
#define XHCI_PORTSC_PED         (1u << 1)
#define XHCI_PORTSC_PR          (1u << 4)
#define XHCI_PORTSC_PP          (1u << 9)
#define XHCI_PORTSC_PIC_MASK    (3u << 14)
#define XHCI_PORTSC_LWS         (1u << 16)
#define XHCI_PORTSC_CSC         (1u << 17)
#define XHCI_PORTSC_PEC         (1u << 18)
#define XHCI_PORTSC_WRC         (1u << 19)
#define XHCI_PORTSC_OCC         (1u << 20)
#define XHCI_PORTSC_PRC         (1u << 21)
#define XHCI_PORTSC_PLC         (1u << 22)
#define XHCI_PORTSC_CEC         (1u << 23)
#define XHCI_PORTSC_WCE         (1u << 25)
#define XHCI_PORTSC_WDE         (1u << 26)
#define XHCI_PORTSC_WOE         (1u << 27)
#define XHCI_PORTSC_WPR         (1u << 31)

#define XHCI_PORTSC_CHANGE_BITS (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | \
                                 XHCI_PORTSC_WRC | XHCI_PORTSC_OCC | \
                                 XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | \
                                 XHCI_PORTSC_CEC)

#define XHCI_PORTSC_NEUTRAL     (XHCI_PORTSC_PP | XHCI_PORTSC_PIC_MASK | \
                                 XHCI_PORTSC_LWS | XHCI_PORTSC_WCE | \
                                 XHCI_PORTSC_WDE | XHCI_PORTSC_WOE)

#define XHCI_TRB_TYPE(n)        ((uint32_t)(n) << 10)
#define XHCI_TRB_TYPE_MASK      (0x3Fu << 10)
#define XHCI_TRB_CYCLE          (1u << 0)
#define XHCI_TRB_TC             (1u << 1)
#define XHCI_TRB_ISP            (1u << 2)
#define XHCI_TRB_CHAIN          (1u << 4)
#define XHCI_TRB_IOC            (1u << 5)
#define XHCI_TRB_IDT            (1u << 6)
#define XHCI_TRB_DIR_IN         (1u << 16)
/* Setup TRB Transfer Type (TRT), bits 16-17: 0 = no data, 2 = OUT,
 * 3 = IN.  TRT=1 is reserved; using it for IN leaves the control transfer
 * in an undefined state even when the first packet happens to complete. */
#define XHCI_TRB_TRT_OUT        (2u << 16)
#define XHCI_TRB_TRT_IN         (3u << 16)
#define XHCI_TRB_INTR_TARGET(n) ((uint32_t)(n) << 22)
#define XHCI_TRB_SLOT_SHIFT     24u
#define XHCI_TRB_EP_SHIFT       16u

#define XHCI_TRB_NORMAL         1u
#define XHCI_TRB_NO_OP          8u
#define XHCI_TRB_SETUP          2u
#define XHCI_TRB_DATA           3u
#define XHCI_TRB_STATUS         4u
#define XHCI_TRB_LINK           6u
#define XHCI_TRB_ENABLE_SLOT    9u
#define XHCI_TRB_DISABLE_SLOT   10u
#define XHCI_TRB_ADDR_DEV       11u
#define XHCI_TRB_CONFIG_EP      12u
#define XHCI_TRB_EVAL_CONTEXT   13u
#define XHCI_TRB_RESET_EP       14u
#define XHCI_TRB_TRANSFER_EVENT 32u
#define XHCI_TRB_CMD_COMPLETE   33u
#define XHCI_TRB_PORT_STATUS    34u

#define XHCI_COMP_SUCCESS       1u
#define XHCI_COMP_SHORT_PACKET  13u

#define XHCI_SPEED_LOW          1u
#define XHCI_SPEED_FULL         2u
#define XHCI_SPEED_HIGH         3u
#define XHCI_SPEED_SUPER        4u

#define XHCI_EP_TYPE_CONTROL    4u
#define XHCI_EP_TYPE_INT_IN     7u
#define XHCI_EP_TYPE(n)         ((uint32_t)(n) << 3)

typedef struct {
    volatile uint32_t dword[XHCI_TRB_DWORDS];
} XhciTrb;

/* DMA-visible memory.  There is no paging on this target, so the address of
 * a static object is the physical address the controller must use. */
static uint64_t xhci_dcbaa[XHCI_MAX_SLOTS + 1u] __attribute__((aligned(64)));
static uint64_t xhci_scratchpad_array[XHCI_MAX_SCRATCHPADS]
    __attribute__((aligned(64)));
static uint8_t xhci_scratchpad_pages[XHCI_MAX_SCRATCHPADS][4096]
    __attribute__((aligned(4096)));
static uint32_t xhci_input_ctx[33u * 16u] __attribute__((aligned(64)));
/* One device context per slot we keep.  Full initialisation can own the
 * keyboard slot and the mouse slot at the same time, and a rejected
 * non-HID device still needs its own context until its slot is disabled. */
static uint32_t xhci_device_ctx[XHCI_MAX_CANDIDATES][32u * 16u]
    __attribute__((aligned(64)));
static uint32_t *xhci_slot_ctx[XHCI_MAX_SLOTS + 1u];
static uint32_t xhci_slot_ctx_used;
static XhciTrb xhci_event_ring[XHCI_EVENT_TRBS] __attribute__((aligned(64)));
static uint64_t xhci_erst[2] __attribute__((aligned(64)));
static XhciTrb xhci_cmd_ring[XHCI_CMD_TRBS + 1u] __attribute__((aligned(64)));
static XhciTrb xhci_ep0_ring[XHCI_EP0_TRBS] __attribute__((aligned(64)));
/* Each configured interrupt-IN endpoint needs a ring of its own. */
static XhciTrb xhci_int_rings[XHCI_MAX_CANDIDATES][XHCI_INT_TRBS]
    __attribute__((aligned(64)));
static uint32_t xhci_int_ring_used;
static uint8_t xhci_report_bufs[XHCI_MAX_CANDIDATES][64]
    __attribute__((aligned(64)));
static uint8_t xhci_config_buf[256] __attribute__((aligned(64)));
static uint8_t xhci_desc_buf[32] __attribute__((aligned(64)));

static volatile uint32_t *xhci_mmio;
static uint32_t xhci_op_base;
static uint32_t xhci_rt_base;
static uint32_t xhci_db_base;
static uint32_t xhci_max_slots;
static uint32_t xhci_max_ports;
static uint32_t xhci_max_intrs;
static uint32_t xhci_scratchpad_count;
static uint32_t xhci_page_size_mask;
static uint32_t xhci_ctx_dwords;
static uint32_t xhci_rt_intr;
/* Interrupter index that owns the event ring we programmed.  Adoption uses
 * interrupter 1 (0x20) so the firmware's interrupter 0 stays untouched;
 * full init owns interrupter 0.  Interrupt TRBs must carry the same index,
 * and the one-interrupter fallback must not target a ring we do not own. */
static uint32_t xhci_intr_target;

static bool xhci_present;
static bool xhci_running;
static bool xhci_ready;
static bool xhci_attempted;
static bool xhci_hid_ready;
/* Mouse-only mode runs beside a working PS/2/SMM keyboard.  It adopts a
 * mouse endpoint (a lone small endpoint or the first interrupt endpoint of a
 * composite HID device) and deliberately leaves xhci_hid_ready false so the
 * 8042/INT 16h keyboard paths stay active.  A larger lone endpoint, or an
 * extra composite endpoint, is kept as an unclassified keyboard fallback;
 * its first report decides the class, and real 8042 key bytes take
 * precedence to prevent duplicate input. */
static bool xhci_mouse_only;
static bool xhci_mouse_ready;
static bool xhci_use_events;
static bool xhci_full_init;

static uint8_t xhci_bus;
static uint8_t xhci_pci_slot;
static uint8_t xhci_pci_fn;

/* The Intel routing registers are saved before the first write so the
 * shutdown hand-off can put the PCH mux back exactly as firmware left it. */
static bool xhci_intel_routing_saved;
static uint32_t xhci_intel_orig_xusb2pr;
static uint32_t xhci_intel_orig_usb3_pssen;
static uint32_t xhci_intel_usb2_mask;
static uint32_t xhci_intel_usb3_mask;

static uint32_t xhci_cmd_base;
static uint32_t xhci_cmd_size;
static uint32_t xhci_cmd_index;
static uint32_t xhci_cmd_cycle;

static uint32_t xhci_event_base;
static uint32_t xhci_event_size;
static uint32_t xhci_event_index;
static uint32_t xhci_event_cycle;

static uint8_t xhci_slot_id;
static uint8_t xhci_ep_dci;
static uint16_t xhci_ep_max_packet;
static uint8_t xhci_ep_interval;
static uint8_t xhci_interface_number;
static uint8_t xhci_config_value;

static uint32_t xhci_ep0_cycle;

static uint32_t xhci_report_count;
static uint32_t xhci_key_count;
static uint32_t xhci_mouse_count;
static uint32_t xhci_error_count;
static uint32_t xhci_candidate_count;

static char xhci_status_line[96];

/* Scancodes emitted by the xHCI decoder are also kept in a tiny ring so
 * the crash handler can consume them without going through the normal
 * IRQ-driven input path.  The ring is cleared when the trace viewer
 * starts, so stale keys from normal operation can never leak into it. */
#define XHCI_CRASH_QUEUE 32u
static uint8_t xhci_crash_queue[XHCI_CRASH_QUEUE];
static uint32_t xhci_crash_head;
static uint32_t xhci_crash_tail;

static void xhci_crash_push(uint8_t code) {
    uint32_t next = (xhci_crash_head + 1u) % XHCI_CRASH_QUEUE;

    if (next == xhci_crash_tail) {
        xhci_crash_tail = (xhci_crash_tail + 1u) % XHCI_CRASH_QUEUE;
    }
    xhci_crash_queue[xhci_crash_head] = code;
    xhci_crash_head = next;
}

static uint32_t xhci_read32(uint32_t offset) {
    return xhci_mmio[offset >> 2];
}

static void xhci_write32(uint32_t offset, uint32_t value) {
    xhci_mmio[offset >> 2] = value;
}

static uint64_t xhci_read64(uint32_t offset) {
    uint32_t low = xhci_read32(offset);
    uint32_t high = xhci_read32(offset + 4u);
    return (uint64_t)low | ((uint64_t)high << 32);
}

static void xhci_write64(uint32_t offset, uint64_t value) {
    xhci_write32(offset, (uint32_t)(value & 0xFFFFFFFFu));
    xhci_write32(offset + 4u, (uint32_t)(value >> 32));
}

static void xhci_delay(void) {
    io_wait();
}

static bool xhci_wait_mask(uint32_t offset, uint32_t mask, uint32_t value,
                           uint32_t spins) {
    for (uint32_t i = 0; i < spins; ++i) {
        if ((xhci_read32(offset) & mask) == value) {
            return true;
        }
        xhci_delay();
    }
    return false;
}

static void xhci_doorbell(uint8_t slot, uint8_t target) {
    if (xhci_mmio == NULL) {
        return;
    }
    xhci_write32(xhci_db_base + ((uint32_t)slot * 4u), target);
}

static uint32_t xhci_trb_type(const XhciTrb *trb) {
    return (trb->dword[3] & XHCI_TRB_TYPE_MASK) >> 10;
}

/* ------------------------------------------------------------------ */
/* PCI discovery and register mapping                                  */
/* ------------------------------------------------------------------ */

static bool xhci_map_controller(void) {
    uint32_t id;
    uint32_t class_reg;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t command;
    uint32_t base;

    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t slot = 0; slot < 32u; ++slot) {
            for (uint8_t fn = 0; fn < 8u; ++fn) {
                id = pci_config_read32((uint8_t)bus, slot, fn, 0x00);
                if (id == 0xFFFFFFFFu) {
                    if (fn == 0) {
                        break;
                    }
                    continue;
                }
                class_reg = pci_config_read32((uint8_t)bus, slot, fn, 0x08);
                if (((class_reg >> 24) & 0xFFu) != 0x0Cu ||
                    ((class_reg >> 16) & 0xFFu) != 0x03u ||
                    ((class_reg >> 8) & 0xFFu) != 0x30u) {
                    continue;
                }

                bar0 = pci_config_read32((uint8_t)bus, slot, fn, 0x10);
                if ((bar0 & 0x1u) != 0) {
                    continue;
                }
                bar1 = 0;
                if ((bar0 & 0x6u) == 0x4u) {
                    bar1 = pci_config_read32((uint8_t)bus, slot, fn, 0x14);
                }
                if (bar1 != 0) {
                    /* The kernel is a 32-bit physical environment.  A
                     * controller above 4 GiB cannot be reached here. */
                    continue;
                }
                base = bar0 & ~0x3Fu;
                if (base == 0) {
                    continue;
                }

                command = pci_config_read32((uint8_t)bus, slot, fn, 0x04);
                command |= 0x0006u; /* memory space + bus master */
                pci_config_write32((uint8_t)bus, slot, fn, 0x04, command);

                xhci_bus = (uint8_t)bus;
                xhci_pci_slot = slot;
                xhci_pci_fn = fn;
                xhci_mmio = (volatile uint32_t *)(uintptr_t)base;
                return true;
            }
        }
    }
    return false;
}

/*
 * Is there an Intel EHCI companion on this machine?  Linux gates the same
 * routing registers on that check: an Intel xHCI without an EHCI sibling
 * has no mux to program, and its 0xD0/0xD4 dwords can be unrelated
 * capabilities rather than routing registers.  If there is no companion,
 * leave the PCI configuration space alone.
 */
static bool xhci_has_intel_ehci(void) {
    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t slot = 0; slot < 32u; ++slot) {
            for (uint8_t fn = 0; fn < 8u; ++fn) {
                uint32_t id = pci_config_read32((uint8_t)bus, slot, fn, 0x00);
                uint32_t class_reg;

                if (id == 0xFFFFFFFFu) {
                    if (fn == 0) {
                        break;
                    }
                    continue;
                }
                if ((id & 0xFFFFu) != XHCI_PCI_VENDOR_INTEL) {
                    continue;
                }
                class_reg = pci_config_read32((uint8_t)bus, slot, fn, 0x08);
                if (((class_reg >> 8) & 0xFFFFFFu) == 0x0C0320u) {
                    return true;
                }
            }
        }
    }
    return false;
}

/*
 * Give the USB2 pins to the xHCI on Intel PCHs that mux them with EHCI.
 * Firmware commonly leaves every pin on EHCI after a warm reset.  The xHC
 * then resets cleanly and reports all of its ports disconnected, which is
 * exactly the second-boot failure this driver sees.  Linux writes all
 * software-routable bits at once: XUSB2PR/USB3PSSEN = mask (see
 * drivers/usb/host/pci-quirks.c, usb_enable_intel_xhci_ports).  The masks
 * are read from the xHCI function, not from the EHCI function.
 *
 * This runs only in the full-reset path.  A running controller is adopted
 * untouched, because switching its pins would disconnect the boot medium
 * out from under the firmware's INT 13h stack.
 */
static void xhci_route_intel_ports(void) {
    uint32_t id;
    uint32_t usb2prm;
    uint32_t usb3prm;
    uint32_t mask2;
    uint32_t mask3;
    uint32_t value;

    if (!xhci_present || xhci_mmio == NULL) {
        return;
    }
    id = pci_config_read32(xhci_bus, xhci_pci_slot, xhci_pci_fn, 0x00);
    serial_trace_hex_value("INFO", "input: xHCI PCI id", id);
    if ((id & 0xFFFFu) != XHCI_PCI_VENDOR_INTEL) {
        serial_trace("INFO", "input: xHCI is not Intel; PCH port routing skipped");
        return;
    }

    usb2prm = pci_config_read32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                                XHCI_PCI_USB2PRM);
    usb3prm = pci_config_read32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                                XHCI_PCI_USB3PRM);
    serial_trace_hex_value("INFO", "input: xHCI Intel USB2 mask raw", usb2prm);
    serial_trace_hex_value("INFO", "input: xHCI Intel USB3 mask raw", usb3prm);
    if (!xhci_has_intel_ehci()) {
        serial_trace("WARNING", "input: xHCI Intel controller has no EHCI mux companion");
        return;
    }
    serial_trace("INFO", "input: xHCI Intel EHCI mux companion present");
    /* A missing register floats high.  Keep the reserved upper half out of
     * the write, and do not treat 0xFFFFFFFF as "all pins routable". */
    mask2 = (usb2prm == 0xFFFFFFFFu) ? 0u : (usb2prm & XHCI_PCI_USB_ROUTE_BITS);
    mask3 = (usb3prm == 0xFFFFFFFFu) ? 0u : (usb3prm & XHCI_PCI_USB_ROUTE_BITS);

    if (!xhci_intel_routing_saved) {
        xhci_intel_orig_xusb2pr = pci_config_read32(xhci_bus, xhci_pci_slot,
                                                     xhci_pci_fn,
                                                     XHCI_PCI_XUSB2PR);
        xhci_intel_orig_usb3_pssen = pci_config_read32(xhci_bus, xhci_pci_slot,
                                                       xhci_pci_fn,
                                                       XHCI_PCI_USB3_PSSEN);
        xhci_intel_routing_saved = true;
    }
    xhci_intel_usb2_mask = mask2;
    xhci_intel_usb3_mask = mask3;

    serial_trace_hex_value("INFO", "input: xHCI Intel USB2 routable pins", mask2);
    serial_trace_hex_value("INFO", "input: xHCI Intel USB3 routable pins", mask3);
    if (mask2 == 0u && mask3 == 0u) {
        serial_trace("INFO", "input: xHCI Intel PCH reports no routable USB pins");
        return;
    }

    if (mask3 != 0u) {
        pci_config_write32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                           XHCI_PCI_USB3_PSSEN, mask3);
        value = pci_config_read32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                                  XHCI_PCI_USB3_PSSEN);
        serial_trace_hex_value("INFO", "input: xHCI Intel USB3 enable now", value);
    }
    if (mask2 != 0u) {
        pci_config_write32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                           XHCI_PCI_XUSB2PR, mask2);
        value = pci_config_read32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                                  XHCI_PCI_XUSB2PR);
        serial_trace_hex_value("INFO", "input: xHCI Intel USB2 routing now", value);
    }
}

static void xhci_restore_intel_ports(void) {
    uint32_t value;

    if (!xhci_intel_routing_saved) {
        return;
    }
    if (xhci_intel_usb3_mask != 0u) {
        value = pci_config_read32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                                  XHCI_PCI_USB3_PSSEN);
        value = (value & ~xhci_intel_usb3_mask) |
                (xhci_intel_orig_usb3_pssen & xhci_intel_usb3_mask);
        pci_config_write32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                           XHCI_PCI_USB3_PSSEN, value);
    }
    if (xhci_intel_usb2_mask != 0u) {
        value = pci_config_read32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                                  XHCI_PCI_XUSB2PR);
        value = (value & ~xhci_intel_usb2_mask) |
                (xhci_intel_orig_xusb2pr & xhci_intel_usb2_mask);
        pci_config_write32(xhci_bus, xhci_pci_slot, xhci_pci_fn,
                           XHCI_PCI_XUSB2PR, value);
    }
    serial_trace("INFO", "input: xHCI Intel port routing restored for firmware");
    xhci_intel_routing_saved = false;
}

static bool xhci_read_capabilities(void) {
    uint32_t capbase;
    uint32_t hcsparams1;
    uint32_t hcsparams2;
    uint32_t hccparams1;
    uint32_t dboff;
    uint32_t rtsoff;

    if (xhci_mmio == NULL) {
        return false;
    }
    capbase = xhci_read32(XHCI_MMIO_CAPLENGTH);
    xhci_op_base = capbase & 0xFFu;
    hcsparams1 = xhci_read32(XHCI_MMIO_HCSPARAMS1);
    hcsparams2 = xhci_read32(XHCI_MMIO_HCSPARAMS2);
    hccparams1 = xhci_read32(XHCI_MMIO_HCCPARAMS1);
    dboff = xhci_read32(XHCI_MMIO_DBOFF) & ~0x3u;
    rtsoff = xhci_read32(XHCI_MMIO_RTSOFF) & ~0x1Fu;
    xhci_db_base = dboff;
    xhci_rt_base = rtsoff;
    xhci_max_slots = hcsparams1 & 0xFFu;
    xhci_max_intrs = (hcsparams1 >> 8) & 0xFFu;
    xhci_max_ports = (hcsparams1 >> 24) & 0xFFu;
    xhci_ctx_dwords = (hccparams1 & (1u << 2)) != 0 ? 16u : 8u;
    xhci_scratchpad_count = (((hcsparams2 >> 21) & 0x1Fu) << 5) |
                            ((hcsparams2 >> 27) & 0x1Fu);
    if (xhci_max_slots == 0) {
        xhci_max_slots = XHCI_MAX_SLOTS;
    }
    if (xhci_max_slots > XHCI_MAX_SLOTS) {
        xhci_max_slots = XHCI_MAX_SLOTS;
    }
    if (xhci_max_ports == 0 || xhci_max_ports > XHCI_MAX_PORTS) {
        xhci_max_ports = XHCI_MAX_PORTS;
    }
    if (xhci_max_intrs == 0) {
        xhci_max_intrs = 1;
    }
    xhci_page_size_mask = xhci_read32(xhci_op_base + XHCI_OP_PAGESIZE);
    return xhci_op_base != 0 && xhci_rt_base != 0 && xhci_db_base != 0;
}

/* ------------------------------------------------------------------ */
/* BIOS/OS handoff                                                     */
/* ------------------------------------------------------------------ */

/*
 * Locate the USB Legacy Support extended capability, or zero when the
 * controller does not implement it.  Shared by the OS-takeover handoff at
 * init and the firmware hand-back before a warm reset.
 */
static uint32_t xhci_legacy_cap_offset(void) {
    uint32_t hccparams1;
    uint32_t offset;

    if (xhci_mmio == NULL) {
        return 0;
    }
    hccparams1 = xhci_read32(XHCI_MMIO_HCCPARAMS1);
    offset = ((hccparams1 >> 16) & 0xFFFFu) << 2;
    for (uint32_t guard = 0; guard < 64u && offset != 0; ++guard) {
        uint32_t cap = xhci_read32(offset);
        uint8_t id = (uint8_t)(cap & 0xFFu);
        uint8_t next = (uint8_t)((cap >> 8) & 0xFFu);

        if (id == 1u) { /* USB Legacy Support */
            return offset;
        }
        if (next == 0) {
            break;
        }
        offset += (uint32_t)next << 2;
    }
    return 0;
}

static void xhci_legacy_handoff(void) {
    uint32_t offset = xhci_legacy_cap_offset();

    if (offset != 0) {
        uint32_t legsup = xhci_read32(offset);
        if ((legsup & (1u << 16)) != 0) {
            xhci_write32(offset, legsup | (1u << 24));
            for (uint32_t i = 0; i < 100000u; ++i) {
                legsup = xhci_read32(offset);
                if ((legsup & (1u << 16)) == 0) {
                    break;
                }
                xhci_delay();
            }
        }
        legsup = xhci_read32(offset);
        legsup |= (1u << 24);  /* OS owns the controller */
        legsup &= ~(1u << 16); /* BIOS SMI no longer owns it */
        xhci_write32(offset, legsup);

        /* Disable the legacy SMI sources.  The status bits are RW1C;
         * write one to clear them, then clear the enable bits. */
        {
            uint32_t ctlsts = xhci_read32(offset + 4u);
            ctlsts |= 0x00000011u; /* clear SMI status bits */
            ctlsts &= ~0x60000011u; /* disable SMI enables */
            xhci_write32(offset + 4u, ctlsts);
        }
    }
}

/*
 * Hand the controller back the way firmware expects to find it after a
 * warm reset: BIOS-owned set, OS-owned clear.  A platform reset that
 * does not power-cycle the chipset keeps these MMIO bits, and a BIOS
 * that finds OSOWNED still set gives up on USB initialization - which
 * leaves the next boot with no firmware-configured HID endpoints at
 * all.  The controller itself is reset separately; this only releases
 * the ownership semaphore.
 */
static void xhci_legacy_release(void) {
    uint32_t offset = xhci_legacy_cap_offset();
    uint32_t legsup;

    if (offset == 0) {
        return;
    }
    legsup = xhci_read32(offset);
    legsup &= ~(1u << 24);
    legsup |= (1u << 16);
    xhci_write32(offset, legsup);
    serial_trace_hex_value("INFO", "xHCI: legacy semaphore released", legsup);
}

/* ------------------------------------------------------------------ */
/* Port handling (full initialisation only)                            */
/* ------------------------------------------------------------------ */

static uint32_t xhci_portsc(uint32_t port) {
    return xhci_read32(xhci_op_base + XHCI_OP_PORTSC +
                       (port - 1u) * 0x10u);
}

static void xhci_port_clear_changes(uint32_t port, uint32_t extra) {
    uint32_t portsc = xhci_portsc(port);
    uint32_t neutral = portsc & XHCI_PORTSC_NEUTRAL;
    xhci_write32(xhci_op_base + XHCI_OP_PORTSC + (port - 1u) * 0x10u,
                 neutral | XHCI_PORTSC_CHANGE_BITS | extra);
}

static bool xhci_reset_port(uint32_t port, uint32_t *speed_out) {
    uint32_t portsc;
    uint32_t speed;

    portsc = xhci_portsc(port);
    /* Power must be applied before CCS can be meaningful.  HCRST may leave
     * PP cleared on controllers that implement port power switching, so the
     * old order (check CCS, then power) could never bring such a port up. */
    if ((portsc & XHCI_PORTSC_PP) == 0) {
        uint32_t value = XHCI_PORTSC_PP |
                         (portsc & (XHCI_PORTSC_PIC_MASK |
                                    XHCI_PORTSC_WCE | XHCI_PORTSC_WDE |
                                    XHCI_PORTSC_WOE));
        xhci_write32(xhci_op_base + XHCI_OP_PORTSC +
                     (port - 1u) * 0x10u, value);
        for (uint32_t i = 0; i < 100000u; ++i) {
            if ((xhci_portsc(port) & XHCI_PORTSC_PP) != 0) {
                break;
            }
            xhci_delay();
        }
        portsc = xhci_portsc(port);
    }
    if ((portsc & XHCI_PORTSC_CCS) == 0) {
        return false;
    }

    speed = (portsc >> 10) & 0xFu;
    if (speed >= XHCI_SPEED_SUPER) {
        uint32_t neutral = portsc & XHCI_PORTSC_NEUTRAL;
        xhci_write32(xhci_op_base + XHCI_OP_PORTSC +
                     (port - 1u) * 0x10u, neutral | XHCI_PORTSC_WPR);
        if (!xhci_wait_mask(xhci_op_base + XHCI_OP_PORTSC +
                            (port - 1u) * 0x10u,
                            XHCI_PORTSC_WRC, XHCI_PORTSC_WRC, 200000u)) {
            return false;
        }
        xhci_port_clear_changes(port, XHCI_PORTSC_WRC);
    } else {
        uint32_t neutral = portsc & XHCI_PORTSC_NEUTRAL;
        xhci_write32(xhci_op_base + XHCI_OP_PORTSC +
                     (port - 1u) * 0x10u, neutral | XHCI_PORTSC_PR);
        if (!xhci_wait_mask(xhci_op_base + XHCI_OP_PORTSC +
                            (port - 1u) * 0x10u,
                            XHCI_PORTSC_PRC, XHCI_PORTSC_PRC, 200000u)) {
            return false;
        }
        xhci_port_clear_changes(port, XHCI_PORTSC_PRC);
    }

    for (uint32_t i = 0; i < 100000u; ++i) {
        portsc = xhci_portsc(port);
        if ((portsc & XHCI_PORTSC_PED) != 0) {
            speed = (portsc >> 10) & 0xFu;
            if (speed_out != NULL) {
                *speed_out = speed;
            }
            xhci_port_clear_changes(port, 0);
            return true;
        }
        if ((portsc & XHCI_PORTSC_CCS) == 0) {
            return false;
        }
        xhci_delay();
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Event ring                                                          */
/* ------------------------------------------------------------------ */

static void xhci_program_event_ring(uint32_t intr_offset) {
    uint32_t intr = xhci_rt_base + intr_offset;

    memset_local(xhci_event_ring, 0, sizeof(xhci_event_ring));
    memset_local(xhci_erst, 0, sizeof(xhci_erst));
    xhci_erst[0] = (uint64_t)(uintptr_t)xhci_event_ring;
    xhci_erst[1] = XHCI_EVENT_TRBS;

    /* Quiesce the interrupter before replacing its segment table.  On a
     * running controller this is interrupter 1, which the firmware does
     * not use; interrupter 0 and its event ring are left untouched. */
    xhci_write32(intr + XHCI_RT_ERSTSZ, 0);
    xhci_write64(intr + XHCI_RT_ERSTBA, 0);
    xhci_write64(intr + XHCI_RT_ERDP, 0);
    xhci_write32(intr + XHCI_RT_IMAN, 1u); /* clear a stale IP; IE stays 0 */

    xhci_write32(intr + XHCI_RT_ERSTSZ, 1);
    xhci_write64(intr + XHCI_RT_ERSTBA, (uint64_t)(uintptr_t)xhci_erst);
    xhci_write64(intr + XHCI_RT_ERDP,
                 (uint64_t)(uintptr_t)xhci_event_ring | (1u << 3));

    xhci_rt_intr = intr_offset;
    xhci_intr_target = (intr_offset - XHCI_RT_INTR0) / XHCI_RT_STRIDE;
    xhci_event_base = (uint32_t)(uintptr_t)xhci_event_ring;
    xhci_event_size = XHCI_EVENT_TRBS;
    xhci_event_index = 0;
    xhci_event_cycle = 1;
}

static void xhci_event_advance(void) {
    ++xhci_event_index;
    if (xhci_event_index >= xhci_event_size) {
        xhci_event_index = 0;
        xhci_event_cycle ^= 1u;
    }
    xhci_write64(xhci_rt_base + xhci_rt_intr + XHCI_RT_ERDP,
                 (uint64_t)(xhci_event_base + xhci_event_index * 16u) |
                 (1u << 3));
}

static bool xhci_event_ready(void) {
    const XhciTrb *event = &xhci_event_ring[xhci_event_index];
    return (event->dword[3] & XHCI_TRB_CYCLE) == xhci_event_cycle;
}

static uint32_t xhci_event_type(const XhciTrb *event) {
    return (event->dword[3] & XHCI_TRB_TYPE_MASK) >> 10;
}

static uint32_t xhci_event_completion(const XhciTrb *event) {
    return (event->dword[2] >> 24) & 0xFFu;
}

static uint32_t xhci_event_length(const XhciTrb *event) {
    return event->dword[2] & 0xFFFFFFu;
}

static bool xhci_wait_command(uint8_t *slot_out, uint32_t *code_out) {
    for (uint32_t spins = 0; spins < 500000u; ++spins) {
        if (xhci_event_ready()) {
            XhciTrb *event = &xhci_event_ring[xhci_event_index];
            uint32_t type = xhci_event_type(event);
            if (type == XHCI_TRB_CMD_COMPLETE) {
                uint32_t code = xhci_event_completion(event);
                uint8_t slot = (uint8_t)((event->dword[3] >>
                                          XHCI_TRB_SLOT_SHIFT) & 0xFFu);
                xhci_event_advance();
                if (code_out != NULL) {
                    *code_out = code;
                }
                if (slot_out != NULL) {
                    *slot_out = slot;
                }
                return code == XHCI_COMP_SUCCESS;
            }
            /* Port status changes and other asynchronous events are
             * discarded; they are not needed for a polling keyboard. */
            xhci_event_advance();
        } else {
            xhci_delay();
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Adoption of a firmware-configured keyboard                          */
/* ------------------------------------------------------------------ */

typedef enum {
    XHCI_HID_KIND_UNKNOWN = 0,
    XHCI_HID_KIND_KEYBOARD,
    XHCI_HID_KIND_MOUSE
} XhciHidKind;

typedef struct {
    bool valid;
    uint8_t slot;
    uint8_t dci;
    uint16_t max_packet;
    uint8_t interval;
    uint32_t deq;
    uint32_t pending_buf;
    uint32_t ring_base;
    uint32_t ring_size;
    uint32_t prod_index;
    uint32_t prod_cycle;
    uint32_t armed_trb;
    uint32_t armed_len;
    XhciHidKind kind;
    /* Some gaming mice ignore boot protocol and keep sending their native
     * report-protocol report (the known 30FA:1040 uses report ID 1 with
     * 16-bit X/Y).  Once a Transfer Event shows the true report length,
     * remember which format the endpoint uses so the no-event fallback can
     * decode later reports correctly. */
    bool mouse_report_protocol;
    uint8_t prev[8];
    uint8_t prev_mods;
    bool prev_has_key;
} XhciAdoptedEp;

static XhciAdoptedEp xhci_adopted[XHCI_MAX_CANDIDATES];

/* Defined with the report decoder and full-initialisation poller below. */
static void xhci_poll_full(void);
static bool xhci_report_looks_like_keyboard(const uint8_t *report,
                                            bool prev_has_key);
static bool xhci_apply_report(XhciAdoptedEp *ep, const uint8_t *report);
static void xhci_handle_report(XhciAdoptedEp *ep, uint32_t index,
                               uint32_t residual, bool have_residual);

static uint64_t *xhci_dcbaa_table(void) {
    uint64_t dcbaap = xhci_read64(xhci_op_base + XHCI_OP_DCBAAP);
    if (dcbaap == 0 || dcbaap > 0xFFFFFFFFu) {
        return NULL;
    }
    return (uint64_t *)(uintptr_t)(uint32_t)dcbaap;
}

static uint32_t *xhci_device_ep(uint32_t *ctx, uint32_t dci) {
    return ctx + dci * xhci_ctx_dwords;
}

/* Slot-context and interrupt-ring pools.  Full initialisation owns one
 * slot per HID device (and temporarily one for every non-HID device it
 * probes), so a single shared device context is not enough. */
static uint32_t *xhci_alloc_device_ctx(uint8_t slot) {
    for (uint32_t i = 0; i < XHCI_MAX_CANDIDATES; ++i) {
        uint32_t bit = 1u << i;

        if ((xhci_slot_ctx_used & bit) != 0) {
            continue;
        }
        xhci_slot_ctx_used |= bit;
        memset_local(xhci_device_ctx[i], 0, sizeof(xhci_device_ctx[i]));
        xhci_slot_ctx[slot] = xhci_device_ctx[i];
        return xhci_device_ctx[i];
    }
    return NULL;
}

static void xhci_free_device_ctx(uint8_t slot) {
    uint32_t *ctx = xhci_slot_ctx[slot];

    if (ctx == NULL) {
        return;
    }
    for (uint32_t i = 0; i < XHCI_MAX_CANDIDATES; ++i) {
        if (ctx == xhci_device_ctx[i]) {
            xhci_slot_ctx_used &= ~(1u << i);
            break;
        }
    }
    xhci_slot_ctx[slot] = NULL;
}

static uint32_t xhci_alloc_int_ring(void) {
    for (uint32_t i = 0; i < XHCI_MAX_CANDIDATES; ++i) {
        uint32_t bit = 1u << i;

        if ((xhci_int_ring_used & bit) != 0) {
            continue;
        }
        xhci_int_ring_used |= bit;
        memset_local(xhci_int_rings[i], 0, sizeof(xhci_int_rings[i]));
        return (uint32_t)(uintptr_t)xhci_int_rings[i];
    }
    return 0;
}

static void xhci_free_int_ring(uint32_t base) {
    for (uint32_t i = 0; i < XHCI_MAX_CANDIDATES; ++i) {
        if (base == (uint32_t)(uintptr_t)xhci_int_rings[i]) {
            xhci_int_ring_used &= ~(1u << i);
            return;
        }
    }
}

static uint64_t xhci_ep_dequeue(const uint32_t *ep) {
    return (uint64_t)ep[2] | ((uint64_t)ep[3] << 32);
}

/*
 * Locate the first free TRB in a transfer ring without knowing the
 * firmware's producer state.  Starting at the endpoint dequeue pointer,
 * walk forward while TRBs carry the consumer cycle; the first TRB with a
 * different cycle has never been written in this lap and is therefore the
 * producer's next slot.  Link TRBs end the segment and toggle the cycle.
 */
static bool xhci_ring_first_free(uint32_t base, uint32_t size,
                                 uint32_t start_index, uint32_t dcs,
                                 uint32_t *index_out, uint32_t *cycle_out) {
    uint32_t index = start_index;
    uint32_t expected = dcs & 1u;
    uint32_t scanned = 0;
    bool wrapped = false;

    if (size < 2u || start_index >= size) {
        return false;
    }
    /* The dequeue is already at the Link: the next free slot is the first
     * entry of the next lap, and the Link must be rewritten before the xHC
     * can follow it.  Returning the Link slot makes the arm path wrap. */
    if (start_index == size - 1u) {
        *index_out = size - 1u;
        *cycle_out = dcs & 1u;
        return true;
    }

    /* Scan at most one full lap of data slots.  If every slot carries the
     * consumer cycle, the ring is full and there is no safe slot to arm;
     * returning a wrapped index there would overwrite an outstanding TRB. */
    while (scanned < size - 1u) {
        const XhciTrb *trb;
        uint32_t type;

        if (index == size - 1u) {
            index = 0;
            expected ^= 1u;
            wrapped = true;
            continue;
        }
        trb = (const XhciTrb *)(uintptr_t)(base + index * 16u);
        type = xhci_trb_type(trb);
        if (type == XHCI_TRB_LINK) {
            index = 0;
            expected ^= 1u;
            wrapped = true;
            continue;
        }
        if ((trb->dword[3] & XHCI_TRB_CYCLE) != expected) {
            if (wrapped && index == 0u) {
                /* The free slot is the first entry after the Link.  Force
                 * the arm path through the Link rewrite even if the Link
                 * has not been written yet. */
                *index_out = size - 1u;
                *cycle_out = dcs & 1u;
            } else {
                *index_out = index;
                *cycle_out = expected;
            }
            return true;
        }
        ++scanned;
        ++index;
        if (index >= size) {
            index = 0;
            expected ^= 1u;
            wrapped = true;
        }
    }
    return false;
}

static bool xhci_ring_geometry(uint32_t deq, uint32_t *base_out,
                               uint32_t *size_out, uint32_t *index_out,
                               uint32_t *cycle_out) {
    uint32_t dcs = deq & 1u;
    uint32_t addr = deq & ~0xFu;
    uint32_t scan;

    if (addr == 0) {
        return false;
    }

    /* EDK2 and SeaBIOS both write a Link TRB at the end of the segment
     * once the ring wraps.  The Link points back at the segment base. */
    for (scan = 0; scan < XHCI_RING_SCAN_LIMIT; ++scan) {
        uint32_t trb_addr = addr + scan * 16u;
        const XhciTrb *trb;
        uint32_t target;
        uint32_t distance;
        uint32_t size;
        uint32_t index;

        if (trb_addr < addr) {
            break;
        }
        trb = (const XhciTrb *)(uintptr_t)trb_addr;
        if (xhci_trb_type(trb) != XHCI_TRB_LINK) {
            continue;
        }
        target = trb->dword[0] & ~0xFu;
        if (target == 0 || target > trb_addr) {
            continue;
        }
        if ((target & 0x3Fu) != 0) {
            continue;
        }
        if ((trb->dword[3] & XHCI_TRB_TC) == 0) {
            continue;
        }
        distance = trb_addr - target;
        if (distance < 16u || distance > 16384u || (distance & 0xFu) != 0) {
            continue;
        }
        size = distance / 16u + 1u;
        index = (addr - target) / 16u;
        if (index < size) {
            *base_out = target;
            *size_out = size;
            *index_out = index;
            *cycle_out = dcs;
            return true;
        }
    }

    /* SeaBIOS allocates a 256-byte-aligned 16-TRB ring and leaves the Link
     * TRB at index 15 zero until the ring first wraps. */
    {
        uint32_t base = addr & ~0xFFu;
        uint32_t index = (addr - base) / 16u;
        if (index < 16u) {
            *base_out = base;
            *size_out = 16u;
            *index_out = index;
            *cycle_out = dcs;
            return true;
        }
    }
    return false;
}

static void xhci_snapshot_pending(XhciAdoptedEp *ep) {
    uint32_t addr = ep->deq & ~0xFu;
    uint32_t dcs = ep->deq & 1u;
    const XhciTrb *trb = (const XhciTrb *)(uintptr_t)addr;

    if ((trb->dword[3] & XHCI_TRB_CYCLE) != dcs ||
        xhci_trb_type(trb) != XHCI_TRB_NORMAL) {
        return;
    }
    if ((trb->dword[2] & 0x1FFFFu) < 8u) {
        return;
    }
    ep->pending_buf = trb->dword[0];
    if (ep->pending_buf == 0) {
        return;
    }
    memcpy_local(ep->prev, (const void *)(uintptr_t)ep->pending_buf, 8);
    ep->prev_mods = ep->prev[0];
    ep->prev_has_key = false;
    for (uint32_t i = 2; i < 8u; ++i) {
        if (ep->prev[i] != 0) {
            ep->prev_has_key = true;
        }
    }
}

static uint32_t xhci_scan_candidates(void) {
    uint64_t *dcbaa = xhci_dcbaa_table();
    uint32_t count = 0;

    memset_local(xhci_adopted, 0, sizeof(xhci_adopted));
    xhci_candidate_count = 0;
    if (dcbaa == NULL) {
        serial_trace("WARNING", "input: xHCI DCBAA is not usable");
        return 0;
    }
    serial_trace_hex_value("INFO", "input: xHCI DCBAA",
                           (uint32_t)(uintptr_t)dcbaa);

    for (uint32_t slot = 1; slot <= xhci_max_slots; ++slot) {
        uint64_t ctx_ptr = dcbaa[slot];
        uint32_t *ctx;
        uint32_t slot_state;
        uint32_t entries;
        uint32_t port;
        bool has_bulk = false;

        if (ctx_ptr == 0 || ctx_ptr > 0xFFFFFFFFu) {
            continue;
        }
        ctx = (uint32_t *)(uintptr_t)(uint32_t)ctx_ptr;
        slot_state = (ctx[3] >> 27) & 0x1Fu;
        if (slot_state != 2u && slot_state != 3u) {
            continue; /* not addressed/configured */
        }
        entries = (ctx[0] >> 27) & 0x1Fu;
        port = (ctx[1] >> 16) & 0xFFu;
        serial_trace_hex_value("INFO", "input: xHCI slot", slot);
        serial_trace_uint_value("INFO", "input: xHCI slot state", slot_state);
        serial_trace_uint_value("INFO", "input: xHCI slot entries", entries);
        serial_trace_uint_value("INFO", "input: xHCI slot root port", port);
        if (entries == 0 || entries > 31u) {
            serial_trace("WARNING", "input: xHCI slot has invalid endpoint count");
            continue;
        }
        if (port == 0 || port > xhci_max_ports) {
            serial_trace("WARNING", "input: xHCI slot has invalid root port");
            continue;
        }
        if ((xhci_portsc(port) & XHCI_PORTSC_CCS) == 0) {
            serial_trace("WARNING", "input: xHCI slot root port not connected");
            continue;
        }

        /* A slot with a bulk endpoint is the boot stick.  Leave it alone:
         * its endpoints and ring state belong to the firmware's INT 13h
         * stack and must not be touched. */
        for (uint32_t dci = 2; dci <= entries; ++dci) {
            uint32_t *ep = xhci_device_ep(ctx, dci);
            uint32_t type = (ep[1] >> 3) & 0x7u;
            if (type == 2u || type == 6u) {
                has_bulk = true;
                break;
            }
        }
        if (has_bulk) {
            boot_medium_on_xhci = true;
            if (!boot_medium_type_known) {
                boot_medium_is_usb = true;
            }
            serial_trace("INFO", "input: xHCI slot is the boot medium");
            continue;
        }

        /* Mouse-only mode normally adopts the mouse endpoint, but a valid
         * 8042 configuration byte is not proof that the firmware still
         * delivers keyboard bytes.  Keep a lone keyboard-sized endpoint as
         * an unclassified fallback so a USB keyboard can rescue a machine
         * whose virtual 8042 reports ready but has no physical keyboard.
         * A composite HID device (the known 30FA:1040 mouse exposes
         * interface 0 = mouse and interface 1 = keyboard, both with
         * eight-byte interrupt endpoints) keeps its first endpoint as the
         * mouse; the remaining endpoint is classified from its first
         * report. */
        uint32_t int_in_count = 0;
        uint32_t first_int_in_dci = 0;
        if (xhci_mouse_only) {
            for (uint32_t dci = 2; dci <= entries; ++dci) {
                uint32_t *ep = xhci_device_ep(ctx, dci);
                uint32_t type = (ep[1] >> 3) & 0x7u;
                if (type == XHCI_EP_TYPE_INT_IN && (dci & 1u) != 0) {
                    ++int_in_count;
                    if (first_int_in_dci == 0) {
                        first_int_in_dci = dci;
                    }
                }
            }
            if (int_in_count >= 2) {
                serial_trace_uint_value("INFO",
                                        "input: xHCI mouse-only composite endpoints",
                                        int_in_count);
                serial_trace_hex_value("INFO",
                                       "input: xHCI mouse-only composite first DCI",
                                       first_int_in_dci);
            }
        }

        for (uint32_t dci = 2; dci <= entries; ++dci) {
            uint32_t *ep = xhci_device_ep(ctx, dci);
            uint32_t type = (ep[1] >> 3) & 0x7u;
            uint16_t max_packet = (uint16_t)((ep[1] >> 16) & 0xFFFFu);
            uint8_t interval = (uint8_t)((ep[0] >> 16) & 0xFFu);
            uint64_t deq;

            if (type != XHCI_EP_TYPE_INT_IN || (dci & 1u) == 0) {
                continue;
            }
            /* Only a running endpoint has a dequeue pointer the xHC is
             * currently using.  A stopped or halted endpoint would need a
             * Reset Endpoint command, which is unsafe while the firmware
             * still owns the command ring. */
            if ((ep[0] & 7u) != 1u) {
                serial_trace_hex_value("WARNING",
                                       "input: xHCI interrupt endpoint not running (DCI)",
                                       dci);
                serial_trace_uint_value("WARNING",
                                        "input: xHCI interrupt endpoint state",
                                        ep[0] & 7u);
                continue;
            }
            if (xhci_mouse_only) {
                if (int_in_count >= 2 && dci == first_int_in_dci) {
                    serial_trace("INFO",
                                 "input: xHCI mouse-only adopting composite first endpoint");
                } else if (int_in_count >= 2) {
                    serial_trace_hex_value("INFO",
                                           "input: xHCI mouse-only probing extra endpoint",
                                           dci);
                } else if (max_packet > 4u) {
                    serial_trace_hex_value("INFO",
                                           "input: xHCI mouse-only probing keyboard-capable DCI",
                                           dci);
                }
            }
            /* Boot keyboards report eight bytes and boot mice report three
             * or four.  Accept both classes; the Transfer Event residual
             * length tells them apart once a report arrives.  Anything
             * smaller than a three-byte boot-mouse report is not HID. */
            if (max_packet < 3u || max_packet > 64u) {
                serial_trace_hex_value("WARNING",
                                       "input: xHCI interrupt endpoint MPS (DCI)",
                                       dci);
                serial_trace_uint_value("WARNING",
                                        "input: xHCI interrupt endpoint MPS",
                                        max_packet);
                continue;
            }
            if (count >= XHCI_MAX_CANDIDATES) {
                break;
            }
            deq = xhci_ep_dequeue(ep);
            if (deq == 0 || deq > 0xFFFFFFFFu) {
                serial_trace_hex_value("WARNING",
                                       "input: xHCI interrupt endpoint bad dequeue (DCI)",
                                       dci);
                continue;
            }
            {
                XhciAdoptedEp *candidate = &xhci_adopted[count];
                candidate->valid = true;
                candidate->slot = (uint8_t)slot;
                candidate->dci = (uint8_t)dci;
                candidate->max_packet = max_packet;
                candidate->interval = interval;
                candidate->deq = (uint32_t)deq;
                /* A four-byte (or smaller) endpoint cannot be a boot
                 * keyboard.  Larger endpoints stay unclassified until the
                 * first report length arrives, because some mice advertise
                 * an eight-byte interrupt packet while sending four.  In
                 * mouse-only mode the first endpoint of a composite HID
                 * device remains the known 30FA:1040 mouse; every other
                 * larger endpoint is a keyboard fallback and is classified
                 * by its first report. */
                candidate->kind = (max_packet <= 4u ||
                                   (xhci_mouse_only && int_in_count >= 2u &&
                                    dci == first_int_in_dci))
                                      ? XHCI_HID_KIND_MOUSE
                                      : XHCI_HID_KIND_UNKNOWN;
                if (candidate->kind != XHCI_HID_KIND_UNKNOWN) {
                    serial_trace_uint_value("INFO", "input: xHCI HID class",
                                            (uint32_t)candidate->kind);
                }
                xhci_snapshot_pending(candidate);
            }
            serial_trace_hex_value("INFO", "input: xHCI HID candidate slot",
                                   slot);
            serial_trace_hex_value("INFO", "input: xHCI HID candidate DCI",
                                   dci);
            serial_trace_uint_value("INFO", "input: xHCI HID candidate MPS",
                                    max_packet);
            serial_trace_uint_value("INFO", "input: xHCI HID candidate interval",
                                    interval);
            ++count;
        }
    }
    xhci_candidate_count = count;
    return count;
}

static bool xhci_arm_candidate(XhciAdoptedEp *ep, uint32_t index) {
    uint32_t idx = ep->prod_index;
    uint32_t cycle = ep->prod_cycle & 1u;
    uint32_t link_cycle = cycle;
    uint32_t next;
    uint32_t length;
    bool wrapped = false;
    XhciTrb *normal;
    uint8_t *buf = xhci_report_bufs[index];

    if (ep->ring_size < 2u) {
        return false;
    }

    /* The Link TRB occupies the last entry.  When the free slot is the
     * Link, the Normal TRB being armed belongs at index zero of the next
     * lap, and the Link carries the cycle of the lap that is ending. */
    if (idx >= ep->ring_size - 1u) {
        wrapped = true;
        link_cycle = cycle;
        idx = 0;
        cycle ^= 1u;
    }

    /* Keep exactly one TRB valid for the xHC.  The slot after the one we
     * are about to arm gets the opposite cycle so the xHC stops there
     * instead of running into stale entries from an earlier lap.  Without
     * this, a ring that wrapped an odd number of times can contain old
     * Normal TRBs whose cycle matches the current lap, and the xHC would
     * execute them (writing into their old buffers) before we re-arm. */
    next = idx + 1u;
    if (next >= ep->ring_size - 1u) {
        XhciTrb *link = (XhciTrb *)(uintptr_t)
            (ep->ring_base + (ep->ring_size - 1u) * 16u);
        if ((cycle & 1u) != 0) {
            link->dword[3] &= ~XHCI_TRB_CYCLE;
        } else {
            link->dword[3] |= XHCI_TRB_CYCLE;
        }
    } else {
        XhciTrb *following = (XhciTrb *)(uintptr_t)
            (ep->ring_base + next * 16u);
        if ((cycle & 1u) != 0) {
            following->dword[3] &= ~XHCI_TRB_CYCLE;
        } else {
            following->dword[3] |= XHCI_TRB_CYCLE;
        }
    }

    /* Ask for at most eight bytes.  A boot keyboard report is eight bytes;
     * a boot mouse report is three or four, and the known report-protocol
     * mouse is seven.  Asking for the endpoint's full MPS on a 16/64-byte
     * keyboard endpoint would make a correct report look short, and asking
     * for more than eight on a mouse endpoint only adds padding. */
    length = ep->max_packet > 8u ? 8u : ep->max_packet;
    ep->armed_len = length;

    memset_local(buf, 0, 64);
    normal = (XhciTrb *)(uintptr_t)(ep->ring_base + idx * 16u);
    normal->dword[0] = (uint32_t)(uintptr_t)buf;
    normal->dword[1] = 0;
    normal->dword[2] = length | XHCI_TRB_INTR_TARGET(xhci_intr_target);
    /* A completion event is only useful when it lands in a ring we own.
     * The one-interrupter fallback polls the endpoint dequeue instead, so
     * it must not ask the xHC to write into the firmware's interrupter 0. */
    normal->dword[3] = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | cycle;
    if (xhci_use_events || xhci_full_init) {
        normal->dword[3] |= XHCI_TRB_IOC;
    }

    /* Publish the Link last.  The xHC may follow it as soon as its cycle
     * is valid, so the first Normal TRB of the new lap must already be in
     * place; otherwise a stale entry whose cycle happens to match could
     * execute and write into an old buffer. */
    if (wrapped) {
        XhciTrb *link = (XhciTrb *)(uintptr_t)
            (ep->ring_base + (ep->ring_size - 1u) * 16u);
        link->dword[0] = ep->ring_base;
        link->dword[1] = 0;
        link->dword[2] = 0;
        link->dword[3] = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC |
                         link_cycle;
    }

    ep->armed_trb = (uint32_t)(uintptr_t)normal;
    ep->prod_index = idx + 1u;
    ep->prod_cycle = cycle;
    xhci_doorbell(ep->slot, ep->dci);
    return true;
}

static bool xhci_adopt_running(void) {
    uint32_t count;
    uint32_t armed = 0;

    count = xhci_scan_candidates();
    if (count == 0) {
        serial_trace("WARNING", "input: xHCI running but no configured HID endpoint found");
        return false;
    }

    if (xhci_max_intrs >= 2u) {
        xhci_program_event_ring(XHCI_RT_INTR1);
        xhci_use_events = true;
        serial_trace("INFO", "input: xHCI using private event ring on interrupter 1");
    } else {
        xhci_use_events = false;
        serial_trace("WARNING", "input: xHCI only one interrupter; polling endpoint dequeue");
    }

    for (uint32_t i = 0; i < count; ++i) {
        XhciAdoptedEp *ep = &xhci_adopted[i];
        uint32_t index = 0;
        uint32_t cycle = 0;

        if (!ep->valid) {
            continue;
        }
        if (!xhci_ring_geometry(ep->deq, &ep->ring_base, &ep->ring_size,
                                &index, &cycle)) {
            serial_trace_hex_value("WARNING", "input: xHCI ring geometry failed for slot",
                                   ep->slot);
            ep->valid = false;
            continue;
        }
        if (!xhci_ring_first_free(ep->ring_base, ep->ring_size, index, cycle,
                                  &ep->prod_index, &ep->prod_cycle)) {
            serial_trace_hex_value("WARNING", "input: xHCI ring full for slot",
                                   ep->slot);
            ep->valid = false;
            continue;
        }
        if (!xhci_arm_candidate(ep, i)) {
            ep->valid = false;
            continue;
        }
        serial_trace_hex_value("INFO", "input: xHCI adopted HID slot", ep->slot);
        serial_trace_hex_value("INFO", "input: xHCI adopted HID DCI", ep->dci);
        serial_trace_hex_value("INFO", "input: xHCI ring base", ep->ring_base);
        serial_trace_uint_value("INFO", "input: xHCI ring size", ep->ring_size);
        ++armed;
    }

    if (armed == 0) {
        serial_trace("WARNING", "input: xHCI no adoptable HID ring");
        return false;
    }

    xhci_slot_id = xhci_adopted[0].slot;
    xhci_ep_dci = xhci_adopted[0].dci;
    xhci_ep_max_packet = xhci_adopted[0].max_packet;
    serial_bootlog_set_usb_hid_ready(true);
    if (xhci_mouse_only) {
        xhci_mouse_ready = true;
        serial_trace("INFO", "input: xHCI mouse-only path ready");
    } else {
        xhci_hid_ready = true;
        boot_menu_dirty = true;
    }
    return true;
}

static XhciAdoptedEp *xhci_find_candidate(uint32_t slot, uint32_t dci) {
    for (uint32_t i = 0; i < xhci_candidate_count; ++i) {
        XhciAdoptedEp *ep = &xhci_adopted[i];
        if (ep->valid && ep->slot == slot && ep->dci == dci) {
            return ep;
        }
    }
    return NULL;
}

/*
 * Decode one completed interrupt transfer.
 *
 * Transfer Events carry the number of bytes the xHC did not write
 * (residual), so actual = armed_len - residual distinguishes an eight-byte
 * boot keyboard report from a three/four-byte boot mouse report and from
 * the seven-byte report-protocol mouse report even when all endpoints
 * advertise the same MPS.  The single-interrupter fallback has no Transfer
 * Event; for a mouse it reuses the format learned from the last completed
 * report.
 */
static void xhci_handle_report(XhciAdoptedEp *ep, uint32_t index,
                               uint32_t residual, bool have_residual) {
    const uint8_t *report = xhci_report_bufs[index];
    uint32_t actual;
    uint32_t mouse_len;

    if (ep == NULL || !ep->valid || ep->armed_len == 0) {
        return;
    }

    if (have_residual) {
        if (residual > ep->armed_len) {
            ++xhci_error_count;
            return;
        }
        actual = ep->armed_len - residual;
    } else {
        actual = ep->armed_len;
    }
    if (actual == 0) {
        return;
    }

    if (ep->kind == XHCI_HID_KIND_UNKNOWN) {
        if (have_residual) {
            if (actual >= 8u) {
                ep->kind = XHCI_HID_KIND_KEYBOARD;
            } else if (actual >= 3u) {
                ep->kind = XHCI_HID_KIND_MOUSE;
            }
        } else if (report[1] != 0) {
            /* A boot keyboard's reserved byte is always zero. */
            ep->kind = XHCI_HID_KIND_MOUSE;
        } else {
            ep->kind = XHCI_HID_KIND_KEYBOARD;
        }
        if (ep->kind != XHCI_HID_KIND_UNKNOWN) {
            serial_trace_uint_value("INFO", "input: xHCI HID class",
                                    (uint32_t)ep->kind);
            serial_trace_uint_value("INFO", "input: xHCI HID report bytes",
                                    actual);
        }
    }

    if (ep->kind == XHCI_HID_KIND_MOUSE) {
        /* A boot mouse sends three or four bytes.  The known gaming mouse
         * keeps report protocol on and sends seven bytes with report ID 1;
         * a Transfer Event residual is the only reliable way to tell the
         * two apart, so learn the format from it.  The no-event fallback
         * then uses the learned format for later reports. */
        if (have_residual) {
            if (actual >= 7u && report[0] == 1u) {
                if (!ep->mouse_report_protocol) {
                    ep->mouse_report_protocol = true;
                    serial_trace("INFO",
                                 "input: xHCI mouse report protocol (report ID 1)");
                    serial_trace_uint_value("INFO",
                                            "input: xHCI mouse report bytes",
                                            actual);
                }
            } else if (actual >= 3u && actual <= 4u) {
                if (ep->mouse_report_protocol) {
                    ep->mouse_report_protocol = false;
                    serial_trace("INFO",
                                 "input: xHCI mouse boot report protocol");
                }
            }
        }
        mouse_len = ep->mouse_report_protocol
                        ? (actual > 8u ? 8u : actual)
                        : (actual > 4u ? 4u : actual);
        if (mouse_len >= 3u) {
            mouse_apply_hid_report(report, mouse_len);
            ++xhci_mouse_count;
        }
        return;
    }

    if (ps2_hardware_key_seen) {
        /* A real 8042 keyboard byte is authoritative.  The USB endpoint is
         * only a rescue path for firmware that reports a valid command byte
         * but never delivers a key; do not enqueue the same key twice. */
        return;
    }

    if (actual >= 8u && xhci_report_looks_like_keyboard(report,
                                                       ep->prev_has_key)) {
        (void)xhci_apply_report(ep, report);
    }
}

static void xhci_poll_dequeue(XhciAdoptedEp *ep, uint32_t index) {
    uint64_t *dcbaa = xhci_dcbaa_table();
    uint64_t ctx_ptr;
    uint32_t *ctx;
    uint32_t *ep_ctx;
    uint32_t deq;
    uint32_t link_addr;

    if (dcbaa == NULL) {
        return;
    }
    ctx_ptr = dcbaa[ep->slot];
    if (ctx_ptr == 0 || ctx_ptr > 0xFFFFFFFFu) {
        return;
    }
    ctx = (uint32_t *)(uintptr_t)(uint32_t)ctx_ptr;
    ep_ctx = xhci_device_ep(ctx, ep->dci);
    deq = (uint32_t)xhci_ep_dequeue(ep_ctx) & ~0xFu;
    link_addr = ep->ring_base + (ep->ring_size - 1u) * 16u;

    /* A dequeue equal to the armed TRB means the xHC has not consumed it
     * yet.  The Link address is also "pending" for a TRB at index zero,
     * because the xHC must follow the Link before it can fetch that TRB. */
    if (deq == ep->armed_trb ||
        (ep->armed_trb == ep->ring_base && deq == link_addr)) {
        return;
    }

    ++xhci_report_count;
    xhci_handle_report(ep, index, 0, false);
    (void)xhci_arm_candidate(ep, index);
}

static void xhci_poll_interrupt(void) {
    uint32_t guard;

    if (!xhci_hid_ready && !xhci_mouse_ready) {
        return;
    }

    if (xhci_full_init) {
        xhci_poll_full();
        return;
    }

    if (!xhci_use_events) {
        for (uint32_t i = 0; i < xhci_candidate_count; ++i) {
            if (xhci_adopted[i].valid) {
                xhci_poll_dequeue(&xhci_adopted[i], i);
            }
        }
        return;
    }

    for (guard = 0; guard < 16u; ++guard) {
        XhciTrb *event;
        uint32_t type;
        uint32_t code;
        uint32_t slot;
        uint32_t ep_id;
        uint32_t length;

        if (!xhci_event_ready()) {
            break;
        }
        event = &xhci_event_ring[xhci_event_index];
        type = xhci_event_type(event);
        code = xhci_event_completion(event);
        slot = (event->dword[3] >> XHCI_TRB_SLOT_SHIFT) & 0xFFu;
        ep_id = (event->dword[3] >> XHCI_TRB_EP_SHIFT) & 0x1Fu;
        length = xhci_event_length(event);

        if (type == XHCI_TRB_TRANSFER_EVENT) {
            XhciAdoptedEp *ep = xhci_find_candidate(slot, ep_id);
            if (ep != NULL) {
                uint32_t index = (uint32_t)(ep - xhci_adopted);
                xhci_event_advance();
                ++xhci_report_count;
                /* Both Success and Short Packet are normal completions.  A
                 * full-length transfer is occasionally reported as Short
                 * Packet (code 13) with zero residual, so the residual is
                 * the authority for the report length, not the code. */
                if (code == XHCI_COMP_SUCCESS ||
                    code == XHCI_COMP_SHORT_PACKET) {
                    xhci_handle_report(ep, index, length, true);
                } else {
                    ++xhci_error_count;
                }
                if (code == XHCI_COMP_SUCCESS ||
                    code == XHCI_COMP_SHORT_PACKET) {
                    (void)xhci_arm_candidate(ep, index);
                } else {
                    /* A stalled endpoint cannot be recovered without a
                     * Reset Endpoint command, which is not safe while the
                     * firmware owns the command ring. */
                    ep->valid = false;
                    serial_trace_hex_value("WARNING",
                                           "input: xHCI HID endpoint halted, code",
                                           code);
                }
                continue;
            }
        }
        xhci_event_advance();
    }
}

/* ------------------------------------------------------------------ */
/* Controller reset and full initialisation                            */
/* ------------------------------------------------------------------ */

static bool xhci_reset_controller(void) {
    uint32_t cmd;

    cmd = xhci_read32(xhci_op_base + XHCI_OP_USBCMD);
    cmd &= ~(XHCI_USBCMD_RUN | XHCI_USBCMD_INTE);
    xhci_write32(xhci_op_base + XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_mask(xhci_op_base + XHCI_OP_USBSTS, XHCI_USBSTS_HCH,
                        XHCI_USBSTS_HCH, 100000u)) {
        return false;
    }

    cmd = xhci_read32(xhci_op_base + XHCI_OP_USBCMD);
    cmd &= ~XHCI_USBCMD_RUN;
    cmd |= XHCI_USBCMD_HCRST;
    xhci_write32(xhci_op_base + XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_mask(xhci_op_base + XHCI_OP_USBCMD, XHCI_USBCMD_HCRST, 0,
                        200000u)) {
        return false;
    }
    if (!xhci_wait_mask(xhci_op_base + XHCI_OP_USBSTS, XHCI_USBSTS_CNR, 0,
                        200000u)) {
        return false;
    }
    return true;
}

static bool xhci_setup_scratchpads(void) {
    if (xhci_scratchpad_count == 0) {
        xhci_dcbaa[0] = 0;
        return true;
    }
    if (xhci_scratchpad_count > XHCI_MAX_SCRATCHPADS) {
        serial_trace_uint_value("WARNING",
                                "input: xHCI too many scratchpad buffers",
                                xhci_scratchpad_count);
        return false;
    }
    if ((xhci_page_size_mask & 1u) == 0) {
        serial_trace_hex_value("WARNING",
                               "input: xHCI controller lacks 4K page size",
                               xhci_page_size_mask);
        return false;
    }
    memset_local(xhci_scratchpad_array, 0, sizeof(xhci_scratchpad_array));
    for (uint32_t i = 0; i < xhci_scratchpad_count; ++i) {
        xhci_scratchpad_array[i] =
            (uint64_t)(uintptr_t)xhci_scratchpad_pages[i];
    }
    xhci_dcbaa[0] = (uint64_t)(uintptr_t)xhci_scratchpad_array;
    return true;
}

static bool xhci_full_ring_init(void) {
    memset_local(xhci_cmd_ring, 0, sizeof(xhci_cmd_ring));
    xhci_cmd_ring[XHCI_CMD_TRBS].dword[0] =
        (uint32_t)(uintptr_t)xhci_cmd_ring;
    xhci_cmd_ring[XHCI_CMD_TRBS].dword[1] = 0;
    xhci_cmd_ring[XHCI_CMD_TRBS].dword[2] = 0;
    xhci_cmd_ring[XHCI_CMD_TRBS].dword[3] =
        XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC | 1u;
    xhci_cmd_base = (uint32_t)(uintptr_t)xhci_cmd_ring;
    xhci_cmd_size = XHCI_CMD_TRBS;
    xhci_cmd_index = 0;
    xhci_cmd_cycle = 1;
    return true;
}

static bool xhci_command(uint32_t param_lo, uint32_t param_hi,
                         uint32_t status, uint32_t type, uint8_t slot,
                         uint8_t *slot_out, uint32_t *code_out) {
    XhciTrb *trb;

    if (!xhci_ready || xhci_cmd_base == 0 || xhci_cmd_size == 0) {
        return false;
    }

    if (xhci_cmd_index >= xhci_cmd_size) {
        XhciTrb *link = &xhci_cmd_ring[xhci_cmd_size];
        link->dword[0] = xhci_cmd_base;
        link->dword[1] = 0;
        link->dword[2] = 0;
        link->dword[3] = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC |
                         xhci_cmd_cycle;
        xhci_cmd_index = 0;
        xhci_cmd_cycle ^= 1u;
    }

    trb = &xhci_cmd_ring[xhci_cmd_index];
    trb->dword[0] = param_lo;
    trb->dword[1] = param_hi;
    trb->dword[2] = status;
    trb->dword[3] = XHCI_TRB_TYPE(type) |
                     ((uint32_t)slot << XHCI_TRB_SLOT_SHIFT) |
                     xhci_cmd_cycle;
    ++xhci_cmd_index;

    xhci_doorbell(0, 0);
    return xhci_wait_command(slot_out, code_out);
}

/* ------------------------------------------------------------------ */
/* Input/device context helpers                                        */
/* ------------------------------------------------------------------ */

static void xhci_zero_input_context(void) {
    memset_local(xhci_input_ctx, 0, sizeof(xhci_input_ctx));
}

/* The Input Context starts with the 8-DWORD Input Control Context (ICC),
 * not the Slot Context.  The Slot Context follows it, then one Endpoint
 * Context per DCI.  Putting the Slot Context at offset 0 overwrites the
 * ICC and makes Address Device fail with a TRB Error. */
#define XHCI_INPUT_ICC_DWORDS 8u

static uint32_t *xhci_input_slot(void) {
    return xhci_input_ctx + XHCI_INPUT_ICC_DWORDS;
}

static uint32_t *xhci_input_ep(uint32_t dci) {
    return xhci_input_ctx + XHCI_INPUT_ICC_DWORDS +
           dci * xhci_ctx_dwords;
}

static void xhci_fill_slot_context(uint32_t speed, uint32_t port,
                                   uint32_t context_entries) {
    uint32_t *slot = xhci_input_slot();
    slot[0] = (speed << 20) | (context_entries << 27);
    slot[1] = port << 16;
    slot[2] = 0;
    slot[3] = 0;
}

static void xhci_fill_ep0_context(uint32_t max_packet, uint32_t ring,
                                  uint32_t cycle) {
    uint32_t *ep = xhci_input_ep(1);
    ep[0] = 0; /* the xHC assigns the running state */
    ep[1] = XHCI_EP_TYPE(XHCI_EP_TYPE_CONTROL) | (3u << 1) |
            (max_packet << 16);
    ep[2] = ring | (cycle & 1u);
    ep[3] = 0;
    ep[4] = 8; /* average TRB length for a control endpoint */
}

static void xhci_fill_int_context(uint32_t dci, uint32_t max_packet,
                                  uint32_t interval, uint32_t ring,
                                  uint32_t cycle) {
    uint32_t *ep = xhci_input_ep(dci);
    ep[0] = (interval & 0xFFu) << 16;
    ep[1] = XHCI_EP_TYPE(XHCI_EP_TYPE_INT_IN) | (3u << 1) |
            (max_packet << 16);
    ep[2] = ring | (cycle & 1u);
    ep[3] = 0;
    ep[4] = max_packet; /* average TRB length */
}

/* ------------------------------------------------------------------ */
/* Control transfers (full initialisation only)                        */
/* ------------------------------------------------------------------ */

static bool xhci_control_transfer(uint8_t bm_request_type, uint8_t request,
                                  uint16_t value, uint16_t index,
                                  uint16_t length, void *buffer) {
    uint32_t cycle = xhci_ep0_cycle;
    uint32_t tail_cycle = cycle ^ 1u;
    bool dir_in = (bm_request_type & 0x80u) != 0;
    bool have_data = length != 0;
    XhciTrb *setup = &xhci_ep0_ring[0];
    XhciTrb *data = &xhci_ep0_ring[1];
    XhciTrb *status = &xhci_ep0_ring[have_data ? 2u : 1u];
    XhciTrb *noop = &xhci_ep0_ring[2];
    XhciTrb *link = &xhci_ep0_ring[3];
    bool success = false;

    if (!xhci_ready || xhci_slot_id == 0) {
        return false;
    }

    /* Fixed one-TD ring: setup, optional data, status, No Op (no-data
     * only), then the Link.  The controller parks on the tail with the
     * finished TD's cycle.  Writing the tail with the opposite cycle lets
     * it follow the old Link, wrap, and execute the new TD on the next
     * doorbell; writing the tail with the new cycle instead makes it see
     * a cycle mismatch and never reach the new TD. */
    memset_local(xhci_ep0_ring, 0, sizeof(xhci_ep0_ring));

    setup->dword[0] = (uint32_t)bm_request_type |
                      ((uint32_t)request << 8) |
                      ((uint32_t)value << 16);
    setup->dword[1] = (uint32_t)index | ((uint32_t)length << 16);
    setup->dword[2] = 8; /* setup packet length */
    setup->dword[3] = XHCI_TRB_TYPE(XHCI_TRB_SETUP) | XHCI_TRB_IDT |
                      XHCI_TRB_CHAIN |
                      (dir_in ? XHCI_TRB_TRT_IN :
                       (have_data ? XHCI_TRB_TRT_OUT : 0)) | cycle;

    if (have_data) {
        data->dword[0] = (uint32_t)(uintptr_t)buffer;
        data->dword[1] = 0;
        data->dword[2] = length;
        data->dword[3] = XHCI_TRB_TYPE(XHCI_TRB_DATA) |
                         XHCI_TRB_CHAIN | XHCI_TRB_ISP |
                         (dir_in ? XHCI_TRB_DIR_IN : 0) | cycle;
    } else {
        /* Keep the ring geometry fixed so the Link is always at TRB 3. */
        noop->dword[3] = XHCI_TRB_TYPE(XHCI_TRB_NO_OP) | tail_cycle;
    }

    status->dword[0] = 0;
    status->dword[1] = 0;
    status->dword[2] = 0;
    status->dword[3] = XHCI_TRB_TYPE(XHCI_TRB_STATUS) | XHCI_TRB_IOC |
                       (have_data && dir_in ? 0 : XHCI_TRB_DIR_IN) | cycle;

    link->dword[0] = (uint32_t)(uintptr_t)xhci_ep0_ring;
    link->dword[1] = 0;
    link->dword[2] = 0;
    link->dword[3] = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC |
                     tail_cycle;

    xhci_doorbell(xhci_slot_id, 1);

    /* The status stage carries IOC.  A short data stage may produce an
     * earlier event (completion code 13); consume it and wait for the
     * status-stage success event. */
    for (uint32_t spins = 0; spins < 500000u; ++spins) {
        if (!xhci_event_ready()) {
            xhci_delay();
            continue;
        }
        {
            XhciTrb *event = &xhci_event_ring[xhci_event_index];
            uint32_t type = xhci_event_type(event);
            uint32_t code = xhci_event_completion(event);
            uint32_t ep = (event->dword[3] >> XHCI_TRB_EP_SHIFT) & 0x1Fu;
            uint32_t slot = (event->dword[3] >> XHCI_TRB_SLOT_SHIFT) & 0xFFu;

            if (type == XHCI_TRB_TRANSFER_EVENT &&
                slot == xhci_slot_id && ep == 1u) {
                xhci_event_advance();
                if (code == XHCI_COMP_SUCCESS) {
                    success = true;
                    break;
                }
                if (code == XHCI_COMP_SHORT_PACKET) {
                    continue;
                }
                ++xhci_error_count;
                break;
            }
            xhci_event_advance();
        }
    }

    if (success) {
        xhci_ep0_cycle ^= 1u;
    } else {
        serial_trace_hex_value("WARNING",
                               "input: xHCI EP0 transfer request failed",
                               request);
        ++xhci_error_count;
    }
    return success;
}

/* ------------------------------------------------------------------ */
/* Device enumeration (full initialisation only)                       */
/* ------------------------------------------------------------------ */

static uint16_t xhci_get_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool xhci_read_device_descriptor(uint8_t *out, uint16_t length) {
    return xhci_control_transfer(0x80u, 6u, 1u << 8, 0, length, out);
}

static bool xhci_evaluate_ep0(uint32_t speed, uint32_t port,
                              uint16_t max_packet) {
    xhci_zero_input_context();
    xhci_input_ctx[1] = (1u << 0) | (1u << 1); /* slot + EP0 */
    xhci_fill_slot_context(speed, port, 1);
    xhci_fill_ep0_context(max_packet,
                          (uint32_t)(uintptr_t)xhci_ep0_ring,
                          xhci_ep0_cycle);
    return xhci_command((uint32_t)(uintptr_t)xhci_input_ctx, 0, 0,
                        XHCI_TRB_EVAL_CONTEXT, xhci_slot_id, NULL, NULL);
}

typedef struct {
    uint8_t interface_number;
    uint8_t protocol;
    uint8_t ep_address;
    uint8_t dci;
    uint16_t max_packet;
    uint8_t interval;
    uint32_t ring_base;
    XhciHidKind kind;
} XhciHidEndpoint;

typedef struct {
    uint8_t config_value;
    uint8_t count;
    XhciHidEndpoint ep[XHCI_MAX_CANDIDATES];
} XhciConfigInfo;

/*
 * Collect every HID interrupt-IN endpoint in the configuration.  The
 * real CSM hardware exposes a boot mouse and a boot keyboard, either as
 * two separate USB devices (two slots) or as two interfaces of one
 * composite device (two DCIs).  The old parser returned after the first
 * keyboard endpoint, so a full reset could only ever restore half of the
 * input path.
 *
 * Subclass 0 is accepted as well as subclass 1: some CSM firmware hands
 * the keyboard interface over with bInterfaceSubClass = 0 while still
 * setting bInterfaceProtocol = 1 (keyboard).
 */
static bool xhci_parse_config(const uint8_t *config, uint16_t total,
                              XhciConfigInfo *info) {
    uint16_t offset = 0;
    uint8_t current_iface = 0;
    uint8_t current_proto = 0;
    bool in_hid = false;
    bool iface_has_ep = false;

    memset_local(info, 0, sizeof(*info));
    if (total < 9u) {
        return false;
    }
    info->config_value = config[5];

    while (offset + 2u <= total) {
        uint8_t length = config[offset];
        uint8_t type = config[offset + 1u];

        if (length < 2u || (uint16_t)(offset + length) > total) {
            break;
        }
        if (type == 4u && length >= 9u) { /* interface */
            uint8_t cls = config[offset + 5u];
            uint8_t sub = config[offset + 6u];
            uint8_t proto = config[offset + 7u];

            current_iface = config[offset + 2u];
            current_proto = proto;
            in_hid = cls == 3u && (sub == 0u || sub == 1u);
            iface_has_ep = false;
        } else if (type == 5u && length >= 7u && in_hid && !iface_has_ep) {
            uint8_t address = config[offset + 2u];
            uint8_t attributes = config[offset + 3u];
            uint16_t max_packet = xhci_get_le16(&config[offset + 4u]) & 0x7FFu;
            uint8_t interval = config[offset + 6u];

            if ((address & 0x80u) != 0 && (attributes & 3u) == 3u) {
                if (info->count < XHCI_MAX_CANDIDATES) {
                    XhciHidEndpoint *ep = &info->ep[info->count++];

                    ep->interface_number = current_iface;
                    ep->protocol = current_proto;
                    ep->ep_address = address;
                    ep->max_packet = max_packet;
                    ep->interval = interval;
                    ep->ring_base = 0;
                    if (current_proto == 1u) {
                        ep->kind = XHCI_HID_KIND_KEYBOARD;
                    } else if (current_proto == 2u) {
                        ep->kind = XHCI_HID_KIND_MOUSE;
                    } else {
                        ep->kind = XHCI_HID_KIND_UNKNOWN;
                    }
                    /* One interrupt-IN endpoint per interface is enough
                     * for a boot keyboard/mouse; ignore consumer-control
                     * or vendor endpoints on the same interface. */
                    iface_has_ep = true;
                }
            }
        }
        offset = (uint16_t)(offset + length);
    }
    return info->count != 0;
}

static uint32_t xhci_interval_from_bInterval(uint32_t speed, uint8_t interval) {
    uint32_t value;

    if (interval == 0) {
        return 0;
    }
    if (speed == XHCI_SPEED_HIGH || speed == XHCI_SPEED_SUPER) {
        value = (uint32_t)interval - 1u;
        return value > 15u ? 15u : value;
    }
    /* Full/low-speed bInterval is 2^(bInterval-1) frames, and one frame
     * is eight microframes, so the xHCI exponent is bInterval + 2.  The
     * known 30FA:1040 advertises bInterval 10, which would become an
     * exponent of 12 (512 ms); the firmware programs 6 (8 ms) for the
     * same endpoint.  Clamp to 6 so a report-protocol value cannot turn
     * the keyboard into a half-second-per-key device. */
    value = (uint32_t)interval + 2u;
    if (value > 6u) {
        value = 6u;
    }
    return value;
}

static void xhci_release_rings(XhciConfigInfo *info) {
    for (uint32_t i = 0; i < info->count; ++i) {
        if (info->ep[i].ring_base != 0) {
            xhci_free_int_ring(info->ep[i].ring_base);
            info->ep[i].ring_base = 0;
        }
    }
}

static void xhci_disable_slot(uint8_t slot) {
    uint32_t code = 0;

    if (!xhci_command(0, 0, 0, XHCI_TRB_DISABLE_SLOT, slot, NULL, &code)) {
        serial_trace_hex_value("WARNING", "input: xHCI disable slot failed",
                               code);
        return;
    }
    xhci_dcbaa[slot] = 0;
    xhci_free_device_ctx(slot);
}

/*
 * Enumerate one connected root port and configure every HID interrupt-IN
 * endpoint the device exposes.  `have_kinds` tells the caller which HID
 * kinds are already live, so a composite keyboard+mouse device does not
 * add a second keyboard endpoint after the keyboard was already found on
 * another port.
 */
static bool xhci_enable_device(uint32_t port, uint32_t speed,
                               uint32_t have_kinds, XhciConfigInfo *info,
                               uint8_t *slot_out) {
    uint8_t slot = 0;
    uint32_t code = 0;
    uint8_t first_desc[8];
    uint16_t device_mps;
    uint16_t total;
    uint16_t config_len;
    uint32_t ep0_mps;
    uint32_t kept;
    uint32_t max_dci;
    uint32_t add_flags;

    memset_local(info, 0, sizeof(*info));
    if (speed == XHCI_SPEED_SUPER) {
        ep0_mps = 512;
    } else if (speed == XHCI_SPEED_HIGH) {
        ep0_mps = 64;
    } else {
        ep0_mps = 8;
    }

    if (!xhci_command(0, 0, 0, XHCI_TRB_ENABLE_SLOT, 0, &slot, &code)) {
        serial_trace_hex_value("WARNING", "input: xHCI enable slot failed", code);
        return false;
    }
    if (slot == 0 || slot > xhci_max_slots) {
        serial_trace_hex_value("WARNING", "input: xHCI returned invalid slot",
                               slot);
        return false;
    }
    xhci_slot_id = slot;
    if (xhci_alloc_device_ctx(slot) == NULL) {
        serial_trace("WARNING", "input: xHCI device-context pool exhausted");
        xhci_disable_slot(slot);
        return false;
    }

    /* Address Device.  The slot is fresh, so only slot + EP0 are added. */
    xhci_zero_input_context();
    xhci_input_ctx[1] = (1u << 0) | (1u << 1);
    xhci_fill_slot_context(speed, port, 1);
    xhci_fill_ep0_context(ep0_mps,
                          (uint32_t)(uintptr_t)xhci_ep0_ring, 1);
    xhci_ep0_cycle = 1;
    xhci_dcbaa[slot] = (uint64_t)(uintptr_t)xhci_slot_ctx[slot];
    if (!xhci_command((uint32_t)(uintptr_t)xhci_input_ctx, 0, 0,
                      XHCI_TRB_ADDR_DEV, slot, NULL, &code)) {
        serial_trace_hex_value("WARNING", "input: xHCI address device failed",
                               code);
        xhci_disable_slot(slot);
        return false;
    }

    /* Read the first eight bytes of the device descriptor to learn the
     * real EP0 max packet size.  Full-speed devices may use 8, 16, 32 or
     * 64 bytes; the initial guess is the minimum safe size. */
    if (!xhci_read_device_descriptor(first_desc, 8)) {
        serial_trace("WARNING", "input: xHCI device descriptor read failed");
        xhci_disable_slot(slot);
        return false;
    }
    device_mps = first_desc[7];
    if (device_mps != 8 && device_mps != 16 && device_mps != 32 &&
        device_mps != 64 && device_mps != 512) {
        device_mps = (uint16_t)ep0_mps;
    }
    if (device_mps != ep0_mps) {
        if (!xhci_evaluate_ep0(speed, port, device_mps)) {
            serial_trace("WARNING", "input: xHCI EP0 max-packet update failed");
            xhci_disable_slot(slot);
            return false;
        }
        ep0_mps = device_mps;
    }

    if (!xhci_read_device_descriptor(xhci_desc_buf, 18)) {
        serial_trace("WARNING", "input: xHCI full device descriptor read failed");
        xhci_disable_slot(slot);
        return false;
    }
    if (xhci_desc_buf[4] == 9u) { /* hub */
        serial_trace("INFO", "input: xHCI device is a hub; not HID input");
        xhci_disable_slot(slot);
        return false;
    }

    if (!xhci_control_transfer(0x80u, 6u, 2u << 8, 0, 9,
                               xhci_config_buf)) {
        serial_trace("WARNING", "input: xHCI config header read failed");
        xhci_disable_slot(slot);
        return false;
    }
    total = xhci_get_le16(&xhci_config_buf[2]);
    if (total < 9u || total > sizeof(xhci_config_buf)) {
        serial_trace_hex_value("WARNING", "input: xHCI bad config length", total);
        xhci_disable_slot(slot);
        return false;
    }
    config_len = total;
    if (!xhci_control_transfer(0x80u, 6u, 2u << 8, 0, config_len,
                               xhci_config_buf)) {
        serial_trace("WARNING", "input: xHCI config descriptor read failed");
        xhci_disable_slot(slot);
        return false;
    }
    if (!xhci_parse_config(xhci_config_buf, config_len, info)) {
        serial_trace("INFO", "input: xHCI device has no HID interrupt endpoint");
        xhci_disable_slot(slot);
        return false;
    }

    /* Drop endpoints whose HID kind is already provided by an earlier
     * device, clamp the report size and convert the interval once. */
    kept = 0;
    for (uint32_t i = 0; i < info->count; ++i) {
        XhciHidEndpoint *ep = &info->ep[i];
        uint32_t kind_bit = 0;

        if (ep->kind == XHCI_HID_KIND_KEYBOARD) {
            kind_bit = 1u;
        } else if (ep->kind == XHCI_HID_KIND_MOUSE) {
            kind_bit = 2u;
        }
        if (kind_bit != 0 && (have_kinds & kind_bit) != 0) {
            serial_trace_hex_value("INFO",
                                   "input: xHCI skip duplicate HID kind",
                                   kind_bit);
            continue;
        }
        if (ep->max_packet < 3u) {
            ep->max_packet = 3u;
        }
        if (ep->max_packet > sizeof(xhci_report_bufs[0])) {
            ep->max_packet = sizeof(xhci_report_bufs[0]);
        }
        ep->dci = (uint8_t)(2u * (ep->ep_address & 0x0Fu) + 1u);
        if (ep->dci <= 1u || ep->dci > 31u) {
            serial_trace_hex_value("WARNING",
                                   "input: xHCI invalid HID endpoint address",
                                   ep->ep_address);
            continue;
        }
        ep->interval = (uint8_t)xhci_interval_from_bInterval(speed,
                                                             ep->interval);
        info->ep[kept++] = *ep;
    }
    info->count = (uint8_t)kept;
    if (info->count == 0) {
        serial_trace("INFO", "input: xHCI no new HID endpoint on this device");
        xhci_disable_slot(slot);
        return false;
    }

    for (uint32_t i = 0; i < info->count; ++i) {
        info->ep[i].ring_base = xhci_alloc_int_ring();
        if (info->ep[i].ring_base == 0) {
            serial_trace("WARNING", "input: xHCI interrupt-ring pool exhausted");
            xhci_release_rings(info);
            xhci_disable_slot(slot);
            return false;
        }
    }

    /* SET_CONFIGURATION, then put every HID interface in boot protocol and
     * disable idle repeats.  A failure to set idle is not fatal: the
     * report-diff logic filters repeated reports anyway. */
    if (!xhci_control_transfer(0x00u, 9u, info->config_value, 0, 0, NULL)) {
        serial_trace("WARNING", "input: xHCI SET_CONFIGURATION failed");
        xhci_release_rings(info);
        xhci_disable_slot(slot);
        return false;
    }
    for (uint32_t i = 0; i < info->count; ++i) {
        (void)xhci_control_transfer(0x21u, 0x0Bu, 0,
                                    info->ep[i].interface_number, 0, NULL);
        (void)xhci_control_transfer(0x21u, 0x0Au, 0,
                                    info->ep[i].interface_number, 0, NULL);
    }

    /* Configure every interrupt IN endpoint in one command.  A1 carries
     * the slot plus each DCI; EP0 is deliberately not in A1. */
    xhci_zero_input_context();
    add_flags = 1u << 0;
    max_dci = 1;
    for (uint32_t i = 0; i < info->count; ++i) {
        add_flags |= 1u << info->ep[i].dci;
        if (info->ep[i].dci > max_dci) {
            max_dci = info->ep[i].dci;
        }
        xhci_fill_int_context(info->ep[i].dci, info->ep[i].max_packet,
                              info->ep[i].interval,
                              info->ep[i].ring_base, 1);
    }
    xhci_input_ctx[1] = add_flags;
    xhci_fill_slot_context(speed, port, max_dci);
    xhci_fill_ep0_context(ep0_mps,
                          (uint32_t)(uintptr_t)xhci_ep0_ring,
                          xhci_ep0_cycle);
    if (!xhci_command((uint32_t)(uintptr_t)xhci_input_ctx, 0, 0,
                      XHCI_TRB_CONFIG_EP, slot, NULL, &code)) {
        serial_trace_hex_value("WARNING", "input: xHCI configure endpoint failed",
                               code);
        xhci_release_rings(info);
        xhci_disable_slot(slot);
        return false;
    }

    xhci_config_value = info->config_value;
    xhci_interface_number = info->ep[0].interface_number;
    xhci_ep_dci = info->ep[0].dci;
    xhci_ep_max_packet = info->ep[0].max_packet;
    xhci_ep_interval = info->ep[0].interval;
    *slot_out = slot;
    return true;
}

/* ------------------------------------------------------------------ */
/* HID boot report decoding                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t usage;
    uint8_t code;
    bool extended;
} XhciKeyMap;

static const XhciKeyMap xhci_key_map[] = {
    {0x04, 0x1E, false}, {0x05, 0x30, false}, {0x06, 0x2E, false},
    {0x07, 0x20, false}, {0x08, 0x12, false}, {0x09, 0x21, false},
    {0x0A, 0x22, false}, {0x0B, 0x23, false}, {0x0C, 0x17, false},
    {0x0D, 0x24, false}, {0x0E, 0x25, false}, {0x0F, 0x26, false},
    {0x10, 0x32, false}, {0x11, 0x31, false}, {0x12, 0x18, false},
    {0x13, 0x19, false}, {0x14, 0x10, false}, {0x15, 0x13, false},
    {0x16, 0x1F, false}, {0x17, 0x14, false}, {0x18, 0x16, false},
    {0x19, 0x2F, false}, {0x1A, 0x11, false}, {0x1B, 0x2D, false},
    {0x1C, 0x15, false}, {0x1D, 0x2C, false},
    {0x1E, 0x02, false}, {0x1F, 0x03, false}, {0x20, 0x04, false},
    {0x21, 0x05, false}, {0x22, 0x06, false}, {0x23, 0x07, false},
    {0x24, 0x08, false}, {0x25, 0x09, false}, {0x26, 0x0A, false},
    {0x27, 0x0B, false},
    {0x28, 0x1C, false}, {0x29, 0x01, false}, {0x2A, 0x0E, false},
    {0x2B, 0x0F, false}, {0x2C, 0x39, false}, {0x2D, 0x0C, false},
    {0x2E, 0x0D, false}, {0x2F, 0x1A, false}, {0x30, 0x1B, false},
    {0x31, 0x2B, false}, {0x32, 0x2B, false}, {0x33, 0x27, false},
    {0x34, 0x28, false}, {0x35, 0x29, false}, {0x36, 0x33, false},
    {0x37, 0x34, false}, {0x38, 0x35, false}, {0x39, 0x3A, false},
    {0x3A, 0x3B, false}, {0x3B, 0x3C, false}, {0x3C, 0x3D, false},
    {0x3D, 0x3E, false}, {0x3E, 0x3F, false}, {0x3F, 0x40, false},
    {0x40, 0x41, false}, {0x41, 0x42, false}, {0x42, 0x43, false},
    {0x43, 0x44, false}, {0x44, 0x57, false}, {0x45, 0x58, false},
    {0x46, 0x37, true},  {0x47, 0x46, false}, {0x48, 0x00, false},
    {0x49, 0x52, true},  {0x4A, 0x47, true},  {0x4B, 0x49, true},
    {0x4C, 0x53, true},  {0x4D, 0x4F, true},  {0x4E, 0x51, true},
    {0x4F, 0x4D, true},  {0x50, 0x4B, true},  {0x51, 0x50, true},
    {0x52, 0x48, true},  {0x53, 0x45, false},
    {0x54, 0x35, true},  {0x55, 0x37, false}, {0x56, 0x4A, false},
    {0x57, 0x4E, false}, {0x58, 0x1C, true},  {0x59, 0x4F, false},
    {0x5A, 0x50, false}, {0x5B, 0x51, false}, {0x5C, 0x4B, false},
    {0x5D, 0x4C, false}, {0x5E, 0x4D, false}, {0x5F, 0x47, false},
    {0x60, 0x48, false}, {0x61, 0x49, false}, {0x62, 0x52, false},
    {0x63, 0x53, false}, {0x64, 0x56, true},  {0x65, 0x5D, true}
};

static bool xhci_lookup_usage(uint8_t usage, XhciKeyMap *out) {
    for (size_t i = 0; i < sizeof(xhci_key_map) / sizeof(xhci_key_map[0]);
         ++i) {
        if (xhci_key_map[i].usage == usage) {
            *out = xhci_key_map[i];
            return true;
        }
    }
    return false;
}

static void xhci_emit_ps2(uint8_t code, bool extended, bool released) {
    uint8_t out = released ? (uint8_t)(code | 0x80u) : code;

    if (extended) {
        xhci_crash_push(0xE0);
        handle_scancode(0xE0);
    }
    xhci_crash_push(out);
    handle_scancode(out);
}

static void xhci_emit_modifier(uint8_t bit, bool released) {
    switch (bit) {
        case 0: xhci_emit_ps2(0x1D, false, released); break; /* L Ctrl */
        case 1: xhci_emit_ps2(0x2A, false, released); break; /* L Shift */
        case 2: xhci_emit_ps2(0x38, false, released); break; /* L Alt */
        case 3: xhci_emit_ps2(0x5B, true, released); break;  /* L GUI */
        case 4: xhci_emit_ps2(0x1D, true, released); break;  /* R Ctrl */
        case 5: xhci_emit_ps2(0x36, false, released); break; /* R Shift */
        case 6: xhci_emit_ps2(0x38, true, released); break;  /* R Alt */
        case 7: xhci_emit_ps2(0x5C, true, released); break;  /* R GUI */
        default: break;
    }
}

static bool xhci_report_contains(const uint8_t *report, uint8_t usage) {
    for (uint32_t i = 2; i < 8u; ++i) {
        if (report[i] == usage) {
            return true;
        }
    }
    return false;
}

/*
 * A boot keyboard report is eight bytes: modifiers, a reserved byte that
 * is always zero, and six usage codes.  Mice and hubs send shorter reports;
 * their Transfer Event residual length is non-zero and the caller drops
 * them before this check.  An all-zero report is a release only if this
 * endpoint was holding a key, which keeps mouse button reports (whose
 * button bits look like modifiers) from generating phantom Ctrl/Shift/Alt.
 */
static bool xhci_report_looks_like_keyboard(const uint8_t *report,
                                            bool prev_has_key) {
    bool any_usage = false;

    if (report[1] != 0) {
        return false;
    }
    for (uint32_t i = 2; i < 8u; ++i) {
        uint8_t usage = report[i];
        if (usage == 0) {
            continue;
        }
        any_usage = true;
        if (!((usage >= 0x04u && usage <= 0x65u) ||
              (usage >= 0xE0u && usage <= 0xE7u))) {
            return false;
        }
    }
    if (any_usage) {
        return true;
    }
    return prev_has_key;
}

static bool xhci_apply_report(XhciAdoptedEp *ep, const uint8_t *report) {
    uint8_t mods = report[0];
    uint8_t changed_mods = (uint8_t)(mods ^ ep->prev_mods);
    bool activity = false;
    uint32_t i;

    if (changed_mods != 0) {
        for (i = 0; i < 8u; ++i) {
            uint8_t mask = (uint8_t)(1u << i);
            if ((changed_mods & mask) != 0) {
                xhci_emit_modifier((uint8_t)i, (mods & mask) == 0);
                activity = true;
            }
        }
        ep->prev_mods = mods;
    }

    /* Releases first: a key that moved from key slot N to slot M in the
     * same report is still the same held key. */
    for (i = 2; i < 8u; ++i) {
        uint8_t usage = ep->prev[i];
        if (usage == 0 || xhci_report_contains(report, usage)) {
            continue;
        }
        {
            XhciKeyMap map;
            if (xhci_lookup_usage(usage, &map)) {
                /* Pause is press-only: enqueueing on release would deliver
                 * a second KEY_PAUSE for one physical keypress. */
                if (!(map.code == 0 && usage == 0x48u)) {
                    xhci_emit_ps2(map.code, map.extended, true);
                }
                activity = true;
            }
        }
    }

    for (i = 2; i < 8u; ++i) {
        uint8_t usage = report[i];
        if (usage == 0 || xhci_report_contains(ep->prev, usage)) {
            continue;
        }
        {
            XhciKeyMap map;
            if (xhci_lookup_usage(usage, &map)) {
                if (map.code == 0 && usage == 0x48u) {
                    enqueue_key(KEY_PAUSE, 0);
                    ps2_keyboard_event_seen = true;
                } else {
                    xhci_emit_ps2(map.code, map.extended, false);
                }
                activity = true;
            }
        }
    }

    memcpy_local(ep->prev, report, 8);
    ep->prev_has_key = false;
    for (i = 2; i < 8u; ++i) {
        if (report[i] != 0) {
            ep->prev_has_key = true;
        }
    }
    if (activity) {
        ++xhci_key_count;
        if (system_state == STATE_BOOT_MENU) {
            boot_menu_dirty = true;
        }
    }
    return activity;
}

/* ------------------------------------------------------------------ */
/* Full-initialisation polling                                         */
/* ------------------------------------------------------------------ */

static void xhci_poll_full(void) {
    for (uint32_t guard = 0; guard < 16u; ++guard) {
        XhciTrb *event;
        uint32_t type;
        uint32_t code;
        uint32_t slot;
        uint32_t ep_id;
        uint32_t length;

        if (!xhci_event_ready()) {
            break;
        }
        event = &xhci_event_ring[xhci_event_index];
        type = xhci_event_type(event);
        code = xhci_event_completion(event);
        slot = (event->dword[3] >> XHCI_TRB_SLOT_SHIFT) & 0xFFu;
        ep_id = (event->dword[3] >> XHCI_TRB_EP_SHIFT) & 0x1Fu;
        length = xhci_event_length(event);

        if (type == XHCI_TRB_TRANSFER_EVENT) {
            XhciAdoptedEp *ep = xhci_find_candidate(slot, ep_id);
            if (ep != NULL) {
                uint32_t index = (uint32_t)(ep - xhci_adopted);

                xhci_event_advance();
                ++xhci_report_count;
                if (code == XHCI_COMP_SUCCESS ||
                    code == XHCI_COMP_SHORT_PACKET) {
                    xhci_handle_report(ep, index, length, true);
                    (void)xhci_arm_candidate(ep, index);
                } else {
                    ++xhci_error_count;
                    ep->valid = false;
                    serial_trace_hex_value("WARNING",
                                           "input: xHCI HID endpoint halted, code",
                                           code);
                }
                continue;
            }
        }
        xhci_event_advance();
    }
}

/* ------------------------------------------------------------------ */
/* Full-initialisation entry                                           */
/* ------------------------------------------------------------------ */

/*
 * Power every root port that the controller says has power switching.  The
 * xHCI specification allows PP to come out of HCRST cleared, and a port
 * whose PP is off reports CCS clear no matter what is plugged into it.  This
 * has to run before the CCS scan, because the old code only powered a port
 * after it had already seen CCS set.
 */
static void xhci_power_ports(void) {
    uint32_t powered = 0;
    uint32_t newly_powered = 0;
    uint32_t connected = 0;

    if (!xhci_present || xhci_mmio == NULL || xhci_op_base == 0) {
        return;
    }
    for (uint32_t port = 1; port <= xhci_max_ports; ++port) {
        uint32_t portsc = xhci_portsc(port);

        if ((portsc & XHCI_PORTSC_PP) == 0) {
            uint32_t value = XHCI_PORTSC_PP |
                             (portsc & (XHCI_PORTSC_PIC_MASK |
                                        XHCI_PORTSC_WCE |
                                        XHCI_PORTSC_WDE |
                                        XHCI_PORTSC_WOE));
            xhci_write32(xhci_op_base + XHCI_OP_PORTSC +
                         (port - 1u) * 0x10u, value);
            ++newly_powered;
        }
    }
    if (newly_powered != 0u) {
        for (uint32_t i = 0; i < 20000u; ++i) {
            xhci_delay();
        }
    }

    /*
     * A just-powered device takes a moment to assert CCS; USB 2.0 allows up
     * to 100 ms of connect debounce.  The next reconnect attempt starts with
     * HCRST, which clears PP again, so a window shorter than that debounce
     * resets the port before the keyboard can ever appear.  Give the first
     * connection half a second, and leave early as soon as one is seen.
     */
    for (uint32_t spins = 0; spins < 500000u; ++spins) {
        connected = 0;
        for (uint32_t port = 1; port <= xhci_max_ports; ++port) {
            if ((xhci_portsc(port) & XHCI_PORTSC_CCS) != 0) {
                ++connected;
            }
        }
        if (connected != 0u) {
            break;
        }
        xhci_delay();
    }
    for (uint32_t port = 1; port <= xhci_max_ports; ++port) {
        if ((xhci_portsc(port) & XHCI_PORTSC_PP) != 0) {
            ++powered;
        }
    }
    serial_trace_uint_value("INFO", "input: xHCI powered ports", powered);
    serial_trace_uint_value("INFO", "input: xHCI ports needing power", newly_powered);
    serial_trace_uint_value("INFO", "input: xHCI connected ports", connected);
    if (connected == 0u) {
        for (uint32_t port = 1; port <= xhci_max_ports; ++port) {
            serial_trace_hex_value("INFO", "input: xHCI port", port);
            serial_trace_hex_value("INFO", "input: xHCI port status",
                                   xhci_portsc(port));
        }
    }
}

static uint32_t xhci_enumerate_hid_ports(void) {
    uint32_t count = 0;
    uint32_t have_kinds = 0;

    xhci_power_ports();
    memset_local(xhci_adopted, 0, sizeof(xhci_adopted));
    xhci_candidate_count = 0;

    for (uint32_t port = 1; port <= xhci_max_ports; ++port) {
        uint32_t speed = 0;
        uint8_t slot = 0;
        XhciConfigInfo info;
        uint32_t adopted_from_device = 0;

        if ((have_kinds & 3u) == 3u) {
            break; /* keyboard and mouse both found */
        }
        if ((xhci_portsc(port) & XHCI_PORTSC_CCS) == 0) {
            continue;
        }
        serial_trace_hex_value("INFO", "input: xHCI HID port", port);
        if (!xhci_reset_port(port, &speed)) {
            serial_trace_hex_value("WARNING",
                                   "input: xHCI port reset failed", port);
            continue;
        }
        serial_trace_hex_value("INFO", "input: xHCI HID port speed", speed);
        if (!xhci_enable_device(port, speed, have_kinds, &info, &slot)) {
            serial_trace_hex_value("WARNING",
                                   "input: xHCI enumeration failed on port",
                                   port);
            continue;
        }

        for (uint32_t i = 0; i < info.count; ++i) {
            XhciHidEndpoint *ep = &info.ep[i];
            XhciAdoptedEp *ad;
            uint32_t index;

            if (count >= XHCI_MAX_CANDIDATES) {
                break;
            }
            index = count;
            ad = &xhci_adopted[index];
            memset_local(ad, 0, sizeof(*ad));
            ad->valid = true;
            ad->slot = slot;
            ad->dci = ep->dci;
            ad->max_packet = ep->max_packet;
            ad->interval = ep->interval;
            ad->ring_base = ep->ring_base;
            ad->ring_size = XHCI_INT_TRBS;
            ad->prod_index = 0;
            ad->prod_cycle = 1;
            ad->kind = ep->kind;
            if (!xhci_arm_candidate(ad, index)) {
                serial_trace_hex_value("WARNING",
                                       "input: xHCI arm failed for slot", slot);
                ad->valid = false;
                continue;
            }
            ++count;
            ++adopted_from_device;
            if (ep->kind == XHCI_HID_KIND_KEYBOARD) {
                have_kinds |= 1u;
            } else if (ep->kind == XHCI_HID_KIND_MOUSE) {
                have_kinds |= 2u;
            }
            serial_trace_hex_value("INFO", "input: xHCI HID endpoint slot",
                                   slot);
            serial_trace_hex_value("INFO", "input: xHCI HID endpoint DCI",
                                   ep->dci);
            serial_trace_uint_value("INFO", "input: xHCI HID endpoint MPS",
                                    ep->max_packet);
            serial_trace_uint_value("INFO", "input: xHCI HID endpoint interval",
                                    ep->interval);
        }

        if (adopted_from_device == 0) {
            /* The slot was configured but nothing could be armed; release
             * it so its context can be reused by the next port. */
            xhci_release_rings(&info);
            xhci_disable_slot(slot);
        }
    }
    xhci_candidate_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        if (xhci_adopted[i].valid &&
            xhci_adopted[i].kind == XHCI_HID_KIND_KEYBOARD) {
            xhci_slot_id = xhci_adopted[i].slot;
            xhci_ep_dci = xhci_adopted[i].dci;
            xhci_ep_max_packet = xhci_adopted[i].max_packet;
            xhci_ep_interval = xhci_adopted[i].interval;
            break;
        }
    }
    return count;
}

/*
 * A platform reset (reboot, watchdog, triple fault) can leave the xHC
 * halted with its firmware-owned DCBAAP/CRCR still intact.  Starting the
 * controller again is much safer than HCRST: the boot stick's slot and
 * the firmware's INT 13h stack survive, so the on-medium boot log keeps
 * working.  Only resume when the state looks structurally valid; if the
 * controller reports a host-system error or is still resetting, fall
 * back to the full reset path.
 */
static bool xhci_try_resume_stopped(void) {
    uint32_t usbcmd;
    uint32_t usbsts;
    uint64_t dcbaap;
    uint64_t crcr;
    uint64_t crcr_ptr;

    usbcmd = xhci_read32(xhci_op_base + XHCI_OP_USBCMD);
    usbsts = xhci_read32(xhci_op_base + XHCI_OP_USBSTS);
    dcbaap = xhci_read64(xhci_op_base + XHCI_OP_DCBAAP);
    crcr = xhci_read64(xhci_op_base + XHCI_OP_CRCR);
    /* CRCR's low six bits are the ring cycle state plus the command
     * stop/abort/running flags; a live firmware ring normally has RCS=1
     * and may still report CRR.  Only the 64-byte-aligned pointer above
     * those bits can be validated. */
    crcr_ptr = crcr & ~0x3FULL;

    serial_trace_hex_value("INFO", "input: xHCI stopped USBSTS", usbsts);
    serial_trace_hex_value("INFO", "input: xHCI stopped USBCMD", usbcmd);
    serial_trace_hex_value("INFO", "input: xHCI stopped DCBAAP",
                           (uint32_t)dcbaap);
    serial_trace_hex_value("INFO", "input: xHCI stopped CRCR",
                           (uint32_t)crcr);

    if ((usbsts & XHCI_USBSTS_HSE) != 0) {
        serial_trace("WARNING", "input: xHCI host-system error; reset needed");
        return false;
    }
    if ((usbsts & XHCI_USBSTS_CNR) != 0) {
        serial_trace("WARNING", "input: xHCI still not ready; reset needed");
        return false;
    }
    if ((usbcmd & XHCI_USBCMD_HCRST) != 0) {
        serial_trace("WARNING", "input: xHCI reset still in progress");
        return false;
    }
    if (dcbaap == 0 || dcbaap > 0xFFFFFFFFu || (dcbaap & 0x3Fu) != 0) {
        serial_trace("WARNING", "input: xHCI stopped DCBAAP invalid");
        return false;
    }
    if (crcr_ptr == 0 || crcr_ptr > 0xFFFFFFFFu) {
        serial_trace("WARNING", "input: xHCI stopped CRCR invalid");
        return false;
    }

    usbcmd |= XHCI_USBCMD_RUN;
    usbcmd &= ~(XHCI_USBCMD_HCRST | XHCI_USBCMD_INTE);
    xhci_write32(xhci_op_base + XHCI_OP_USBCMD, usbcmd);
    if (!xhci_wait_mask(xhci_op_base + XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0,
                        200000u)) {
        serial_trace("WARNING", "input: xHCI resume timed out");
        return false;
    }
    xhci_running = true;
    xhci_ready = true;
    serial_trace("INFO", "input: xHCI controller resumed without reset");
    return true;
}

static bool xhci_start_from_scratch(void) {
    uint32_t cmd;
    uint32_t cmd_ring;

    serial_trace_hex_value("INFO", "input: xHCI USBSTS before reset",
                           xhci_read32(xhci_op_base + XHCI_OP_USBSTS));
    /* Route the PCH USB2/USB3 pins to this controller before resetting it.
     * HCRST does not change PCI configuration space, but the devices have to
     * be on the xHC when the reset completes and the port scan begins. */
    xhci_route_intel_ports();
    if (!xhci_reset_controller()) {
        serial_trace_hex_value("WARNING", "input: xHCI USBSTS after reset",
                               xhci_read32(xhci_op_base + XHCI_OP_USBSTS));
        return false;
    }
    xhci_running = false;
    xhci_ready = false;
    memset_local(xhci_dcbaa, 0, sizeof(xhci_dcbaa));
    memset_local(xhci_device_ctx, 0, sizeof(xhci_device_ctx));
    memset_local(xhci_input_ctx, 0, sizeof(xhci_input_ctx));
    memset_local(xhci_slot_ctx, 0, sizeof(xhci_slot_ctx));
    memset_local(xhci_adopted, 0, sizeof(xhci_adopted));
    xhci_slot_ctx_used = 0;
    xhci_int_ring_used = 0;
    xhci_candidate_count = 0;
    serial_trace_uint_value("INFO", "input: xHCI scratchpad buffers",
                            xhci_scratchpad_count);
    if (!xhci_setup_scratchpads()) {
        return false;
    }
    /* HCRST may reset the legacy-support capability; take ownership again
     * before the firmware can reclaim the controller through an SMI. */
    xhci_legacy_handoff();
    xhci_program_event_ring(XHCI_RT_INTR0);
    xhci_full_ring_init();
    cmd_ring = xhci_cmd_base | 1u;

    xhci_write32(xhci_op_base + XHCI_OP_CONFIG, xhci_max_slots);
    xhci_write64(xhci_op_base + XHCI_OP_DCBAAP,
                 (uint64_t)(uintptr_t)xhci_dcbaa);
    xhci_write64(xhci_op_base + XHCI_OP_CRCR, (uint64_t)cmd_ring);
    serial_trace_hex_value("INFO", "input: xHCI command ring base", xhci_cmd_base);
    serial_trace_hex_value("INFO", "input: xHCI CRCR programmed", cmd_ring);

    cmd = xhci_read32(xhci_op_base + XHCI_OP_USBCMD);
    cmd &= ~XHCI_USBCMD_INTE;
    cmd |= XHCI_USBCMD_RUN;
    xhci_write32(xhci_op_base + XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_mask(xhci_op_base + XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0,
                        200000u)) {
        return false;
    }
    /*
     * A few controllers clear CRCR when RUN is set even though the ring was
     * programmed while halted.  That leaves a running controller that cannot
     * answer Enable Slot, which looks exactly like an empty port scan once a
     * keyboard finally appears.  Re-program only when the pointer really
     * reads back empty, and keep the readback in the trace either way.
     */
    if ((uint32_t)(xhci_read64(xhci_op_base + XHCI_OP_CRCR) & ~0x3FULL) == 0u) {
        serial_trace("WARNING", "input: xHCI CRCR empty after run; reprogramming");
        xhci_write64(xhci_op_base + XHCI_OP_CRCR, (uint64_t)cmd_ring);
        for (uint32_t i = 0; i < 10000u; ++i) {
            if ((uint32_t)(xhci_read64(xhci_op_base + XHCI_OP_CRCR) &
                           ~0x3FULL) != 0u) {
                break;
            }
            xhci_delay();
        }
        serial_trace_hex_value("INFO", "input: xHCI CRCR readback",
                               (uint32_t)xhci_read64(xhci_op_base +
                                                      XHCI_OP_CRCR));
    }
    xhci_running = true;
    xhci_ready = true;
    xhci_full_init = true;
    return true;
}

/* ------------------------------------------------------------------ */
/* Status and public entry points                                      */
/* ------------------------------------------------------------------ */

static void xhci_build_status_line(void) {
    size_t len = 0;

    xhci_status_line[0] = '\0';
    append_string(xhci_status_line, &len, sizeof(xhci_status_line), "XHCI ");
    if (!xhci_present) {
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      "none");
        return;
    }
    append_hex8(xhci_status_line, &len, sizeof(xhci_status_line), xhci_bus);
    append_string(xhci_status_line, &len, sizeof(xhci_status_line), ":");
    append_hex8(xhci_status_line, &len, sizeof(xhci_status_line),
                xhci_pci_slot);
    append_string(xhci_status_line, &len, sizeof(xhci_status_line), ".");
    append_hex8(xhci_status_line, &len, sizeof(xhci_status_line),
                xhci_pci_fn);
    append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                  xhci_running ? " run" : " stop");
    if (xhci_hid_ready) {
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      xhci_full_init ? " HID init" : " HID");
        if (!xhci_full_init) {
            append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                          xhci_use_events ? " evt1" : " deq");
        }
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      " slot ");
        append_uint(xhci_status_line, &len, sizeof(xhci_status_line),
                    xhci_slot_id);
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      " ep ");
        append_uint(xhci_status_line, &len, sizeof(xhci_status_line),
                    xhci_ep_dci);
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      " keys ");
        append_uint(xhci_status_line, &len, sizeof(xhci_status_line),
                    xhci_key_count);
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      " mouse ");
        append_uint(xhci_status_line, &len, sizeof(xhci_status_line),
                    xhci_mouse_count);
    } else if (xhci_mouse_ready) {
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      " HID mouse slot ");
        append_uint(xhci_status_line, &len, sizeof(xhci_status_line),
                    xhci_slot_id);
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      " ep ");
        append_uint(xhci_status_line, &len, sizeof(xhci_status_line),
                    xhci_ep_dci);
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      " reports ");
        append_uint(xhci_status_line, &len, sizeof(xhci_status_line),
                    xhci_mouse_count);
    } else if (xhci_attempted) {
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      " no HID");
    } else {
        append_string(xhci_status_line, &len, sizeof(xhci_status_line),
                      " idle");
    }
}

/*
 * One compact dump of everything the next boot's failure report needs:
 * controller state, the ports that report a device, and every firmware
 * slot/endpoint context with its type and state.  A boot that ends on the
 * xHCI input error screen is otherwise indistinguishable between "the
 * firmware configured no HID", "the endpoint stopped" and "the controller
 * itself is not running"; this says which.
 */
static void xhci_log_controller_state(const char *tag) {
    uint64_t dcbaap;
    uint64_t *dcbaa;

    if (!xhci_present || xhci_mmio == NULL || xhci_op_base == 0) {
        serial_trace("INFO", "input: xHCI state unavailable (no controller)");
        return;
    }
    serial_trace_concat("INFO", "input: xHCI state ",
                        tag != NULL ? tag : "");
    serial_trace_hex_value("INFO", "input: xHCI USBCMD",
                           xhci_read32(xhci_op_base + XHCI_OP_USBCMD));
    serial_trace_hex_value("INFO", "input: xHCI USBSTS",
                           xhci_read32(xhci_op_base + XHCI_OP_USBSTS));
    serial_trace_hex_value("INFO", "input: xHCI CONFIG",
                           xhci_read32(xhci_op_base + XHCI_OP_CONFIG));
    dcbaap = xhci_read64(xhci_op_base + XHCI_OP_DCBAAP);
    serial_trace_hex_value("INFO", "input: xHCI DCBAAP low", (uint32_t)dcbaap);
    serial_trace_hex_value("INFO", "input: xHCI DCBAAP high",
                           (uint32_t)(dcbaap >> 32));
    serial_trace_hex_value("INFO", "input: xHCI CRCR low",
                           (uint32_t)xhci_read64(xhci_op_base + XHCI_OP_CRCR));
    serial_trace_hex_value("INFO", "input: xHCI CRCR high",
                           (uint32_t)(xhci_read64(xhci_op_base + XHCI_OP_CRCR) >> 32));

    for (uint32_t port = 1; port <= xhci_max_ports; ++port) {
        uint32_t portsc = xhci_portsc(port);

        if ((portsc & (XHCI_PORTSC_CCS | XHCI_PORTSC_PED |
                       XHCI_PORTSC_CSC | XHCI_PORTSC_PRC)) == 0) {
            continue;
        }
        serial_trace_hex_value("INFO", "input: xHCI port", port);
        serial_trace_hex_value("INFO", "input: xHCI port status", portsc);
    }

    dcbaa = xhci_dcbaa_table();
    if (dcbaa == NULL) {
        serial_trace("INFO", "input: xHCI no usable DCBAA to dump slots from");
        return;
    }
    for (uint32_t slot = 1; slot <= xhci_max_slots; ++slot) {
        uint64_t ctx_ptr = dcbaa[slot];
        uint32_t *ctx;
        uint32_t entries;
        uint32_t state;

        if (ctx_ptr == 0 || ctx_ptr > 0xFFFFFFFFu ||
            (ctx_ptr & 0x3Fu) != 0) {
            continue;
        }
        ctx = (uint32_t *)(uintptr_t)(uint32_t)ctx_ptr;
        entries = (ctx[0] >> 27) & 0x1Fu;
        state = (ctx[3] >> 27) & 0x1Fu;
        /* A DCBAA slot pointer can be stale firmware memory from a previous
         * boot.  Only a slot in one of the four defined states, with a
         * plausible context size, is a slot the controller could own. */
        if (state == 0 || state > 4u || entries == 0 || entries > 31u) {
            continue;
        }
        serial_trace_hex_value("INFO", "input: xHCI dump slot", slot);
        serial_trace_hex_value("INFO", "input: xHCI dump slot ctx",
                               (uint32_t)ctx_ptr);
        serial_trace_hex_value("INFO", "input: xHCI dump slot state", state);
        serial_trace_uint_value("INFO", "input: xHCI dump slot entries",
                                entries);
        serial_trace_hex_value("INFO", "input: xHCI dump slot port",
                               (ctx[1] >> 16) & 0xFFu);
        for (uint32_t dci = 1; dci <= entries; ++dci) {
            const uint32_t *ep = xhci_device_ep(ctx, dci);
            uint32_t ep_state = ep[0] & 7u;
            uint32_t ep_type = (ep[1] >> 3) & 0x7u;

            if (ep_state == 0 && ep_type == 0) {
                continue;
            }
            serial_trace_hex_value("INFO", "input: xHCI dump EP DCI", dci);
            serial_trace_hex_value("INFO", "input: xHCI dump EP state",
                                   ep_state);
            serial_trace_hex_value("INFO", "input: xHCI dump EP type",
                                   ep_type);
            serial_trace_hex_value("INFO", "input: xHCI dump EP dequeue",
                                   (uint32_t)xhci_ep_dequeue(ep));
        }
    }
}

static void xhci_hid_init_mode(bool mouse_only) {
    uint32_t count;

    if (xhci_attempted) {
        return;
    }
    xhci_attempted = true;
    xhci_mouse_only = mouse_only;

    if (!xhci_map_controller()) {
        xhci_present = false;
        xhci_build_status_line();
        serial_trace("INFO", "input: no xHCI controller for native input");
        return;
    }
    xhci_present = true;
    if (!xhci_read_capabilities()) {
        xhci_build_status_line();
        serial_trace("WARNING", "input: xHCI capabilities unreadable");
        return;
    }
    serial_trace_hex_value("INFO", "input: xHCI MMIO base",
                           (uint32_t)(uintptr_t)xhci_mmio);
    serial_trace_uint_value("INFO", "input: xHCI max slots", xhci_max_slots);
    serial_trace_uint_value("INFO", "input: xHCI max ports", xhci_max_ports);
    serial_trace_uint_value("INFO", "input: xHCI max interrupters",
                            xhci_max_intrs);
    xhci_legacy_handoff();

    xhci_running = (xhci_read32(xhci_op_base + XHCI_OP_USBCMD) &
                    XHCI_USBCMD_RUN) != 0;
    serial_trace("INFO", xhci_running ? "input: xHCI controller running"
                                      : "input: xHCI controller stopped");
    xhci_log_controller_state("at init");
    if (xhci_running) {
        /* Never reset a running controller: the boot stick is often on
         * this same controller and the firmware's INT 13h stack would
         * lose it.  Adopt the firmware's HID endpoint instead. */
        if (!xhci_adopt_running()) {
            xhci_log_controller_state("adoption failed");
            xhci_build_status_line();
            serial_trace("WARNING",
                         "input: xHCI running but no adoptable HID; not resetting");
            return;
        }
        xhci_ready = true;
        serial_trace("INFO", "input: xHCI adopted running controller");
        xhci_build_status_line();
        return;
    }

    /* A platform reset can leave the controller halted while the
     * firmware's DCBAAP/CRCR are still valid.  Resuming preserves the
     * boot stick slot; only fall through to HCRST when resume is not
     * structurally possible. */
    if (xhci_try_resume_stopped()) {
        if (xhci_adopt_running()) {
            serial_trace("INFO", "input: xHCI adopted resumed controller");
            xhci_build_status_line();
            return;
        }
        serial_trace("WARNING", "input: xHCI resumed but no adoptable HID");
    }

    if (mouse_only) {
        xhci_build_status_line();
        serial_trace("WARNING",
                     "input: xHCI mouse-only could not resume; not resetting");
        return;
    }

    /* HCRST detaches the firmware's boot stick from this controller.  Do
     * not do that here: the trace still has to be writable while the boot
     * check makes its non-destructive adoption retries.  If those fail,
     * xhci_boot_recover() flushes the trace, detaches the medium and only
     * then performs this reset. */
    if (boot_medium_is_usb || boot_medium_on_xhci) {
        serial_trace("WARNING",
                     "input: xHCI boot medium is USB; deferring reset to input recovery");
        xhci_build_status_line();
        return;
    }

    if (!xhci_start_from_scratch()) {
        xhci_log_controller_state("after failed reset");
        xhci_build_status_line();
        serial_trace("WARNING", "input: xHCI controller reset failed");
        return;
    }
    xhci_ready = true;
    serial_trace("INFO", "input: xHCI controller initialized from scratch");

    count = xhci_enumerate_hid_ports();
    if (count == 0) {
        xhci_log_controller_state("no HID after enumeration");
        xhci_build_status_line();
        serial_trace("WARNING", "input: xHCI no HID input found");
        return;
    }
    xhci_use_events = true;
    xhci_full_init = true;
    xhci_hid_ready = true;
    xhci_mouse_ready = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (xhci_adopted[i].valid &&
            xhci_adopted[i].kind == XHCI_HID_KIND_MOUSE) {
            xhci_mouse_ready = true;
            break;
        }
    }
    serial_bootlog_set_usb_hid_ready(true);
    serial_trace_uint_value("INFO", "input: xHCI HID endpoints", count);
    xhci_build_status_line();
    boot_menu_dirty = true;
}

/* ------------------------------------------------------------------ */
/* Boot input check support                                            */
/* ------------------------------------------------------------------ */

/*
 * The check needs to tell a live keyboard endpoint from a live mouse
 * endpoint.  Adoption leaves a class UNKNOWN until the first report (a
 * boot keyboard and an eight-byte mouse look identical in the endpoint
 * context), so keyboard mode reports every adopted endpoint as the
 * keyboard path; full initialisation knows the class from the HID
 * descriptors and counts a keyboard only when one was actually armed.
 */
static bool xhci_native_keyboard_ready(void) {
    if (!xhci_present || !xhci_hid_ready) {
        return false;
    }
    if (!xhci_full_init) {
        return true;
    }
    for (uint32_t i = 0; i < xhci_candidate_count; ++i) {
        if (xhci_adopted[i].valid &&
            xhci_adopted[i].kind == XHCI_HID_KIND_KEYBOARD) {
            return true;
        }
    }
    return false;
}

static bool xhci_native_mouse_ready(void) {
    if (!xhci_present) {
        return false;
    }
    for (uint32_t i = 0; i < xhci_candidate_count; ++i) {
        if (xhci_adopted[i].valid &&
            xhci_adopted[i].kind == XHCI_HID_KIND_MOUSE) {
            return true;
        }
    }
    return false;
}

static uint32_t xhci_connected_ports(void) {
    uint32_t count = 0;

    if (!xhci_present || xhci_mmio == NULL) {
        return 0;
    }
    for (uint32_t port = 1; port <= xhci_max_ports; ++port) {
        if ((xhci_portsc(port) & XHCI_PORTSC_CCS) != 0) {
            ++count;
        }
    }
    return count;
}

/* Full initialisation owns the controller and its command ring, so it can
 * safely re-enumerate the root ports for a device that was still
 * enumerating at boot (or was plugged in during the retry countdown).
 * Adoption mode must never reset the firmware's controller: the boot
 * medium may be on it and the INT 13h stack would lose it.  There the
 * retry only looks for endpoints the firmware has configured since. */
static bool xhci_boot_reconnect(void) {
    if (!xhci_present) {
        return false;
    }
    if (!xhci_attempted) {
        xhci_hid_init_mode(false);
        return xhci_native_keyboard_ready() || xhci_native_mouse_ready();
    }
    if (xhci_full_init) {
        uint32_t count;

        /* Full initialisation already owns the command ring, so the
         * firmware disk path is either still usable or already gone.  This
         * call only happens while the keyboard is missing; re-enumerate. */
        serial_trace("INFO", "input check: re-enumerating xHCI root ports");
        if (!xhci_start_from_scratch()) {
            serial_trace("WARNING", "input check: xHCI reconnect reset failed");
            xhci_build_status_line();
            return false;
        }
        xhci_hid_ready = false;
        xhci_mouse_ready = false;
        count = xhci_enumerate_hid_ports();
        for (uint32_t i = 0; i < count; ++i) {
            if (!xhci_adopted[i].valid) {
                continue;
            }
            if (xhci_adopted[i].kind == XHCI_HID_KIND_KEYBOARD) {
                xhci_hid_ready = true;
            } else if (xhci_adopted[i].kind == XHCI_HID_KIND_MOUSE) {
                xhci_mouse_ready = true;
            }
        }
        if (xhci_hid_ready || xhci_mouse_ready) {
            serial_bootlog_set_usb_hid_ready(true);
        }
        xhci_build_status_line();
        boot_menu_dirty = true;
        return xhci_hid_ready || xhci_mouse_ready;
    }
    if (!xhci_hid_ready && !xhci_mouse_ready && xhci_running) {
        if (xhci_adopt_running()) {
            xhci_build_status_line();
            return true;
        }
    }
    return xhci_native_keyboard_ready() || xhci_native_mouse_ready();
}

/*
 * Last resort when the firmware left no adoptable HID after every retry.
 * Taking the controller over means HCRST, which invalidates the firmware's
 * boot-stick slot: INT 13h stops answering.  The trace is therefore
 * flushed and the medium detached first, so the failure report that is
 * already in RAM reaches bios-serial.log even though the fatal screen's
 * own flush can no longer write.  The kernel is fully in RAM by this
 * point, so losing the boot medium costs the log, not the boot - and a
 * machine with no keyboard at all would not boot anyway.
 */
static bool xhci_boot_recover(void) {
    uint32_t count;

    if (!xhci_present || xhci_mmio == NULL || xhci_op_base == 0) {
        return false;
    }
    /* No keyboard means the machine cannot continue, so the boot medium no
     * longer has priority.  HCRST invalidates the firmware's USB disk slot
     * when the stick shares this controller; the trace is flushed and the
     * medium is detached below, and the kernel, WAD and framebuffer are
     * already in RAM.  This is the only path that can create the HID
     * endpoints the firmware failed to configure. */
    if (boot_medium_is_usb || boot_medium_on_xhci) {
        serial_trace("WARNING",
                     "input check: boot medium is on xHCI; taking it over for keyboard recovery");
    }
    serial_trace("WARNING",
                 "input check: no adoptable firmware HID; taking over the controller");
    xhci_log_controller_state("before last-resort reset");
    serial_bootlog_flush("xHCI last-resort reset");
    serial_bootlog_detach_medium();

    if (!xhci_start_from_scratch()) {
        serial_trace("WARNING", "input check: last-resort xHCI reset failed");
        xhci_log_controller_state("after failed last-resort reset");
        /* Leave the ownership semaphore with the firmware even though the
         * reset did not complete, so a manual reset recovers USB. */
        xhci_legacy_release();
        return false;
    }
    xhci_hid_ready = false;
    xhci_mouse_ready = false;
    count = xhci_enumerate_hid_ports();
    serial_trace_uint_value("INFO", "input check: last-resort HID endpoints",
                            count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!xhci_adopted[i].valid) {
            continue;
        }
        if (xhci_adopted[i].kind == XHCI_HID_KIND_KEYBOARD) {
            xhci_hid_ready = true;
        } else if (xhci_adopted[i].kind == XHCI_HID_KIND_MOUSE) {
            xhci_mouse_ready = true;
        }
    }
    if (count == 0) {
        xhci_log_controller_state("no HID after last-resort reset");
    }
    if (xhci_hid_ready || xhci_mouse_ready) {
        serial_bootlog_set_usb_hid_ready(true);
        xhci_build_status_line();
        boot_menu_dirty = true;
        /* A mouse without a keyboard still cannot drive the boot menu; do
         * not report success, but do keep the controller owned so the
         * following retry/reset can find the keyboard. */
        if (xhci_hid_ready) {
            return true;
        }
    }

    /* No keyboard: the boot check halts on the fatal screen, and the next
     * reset is most likely the user's physical reset button - which gives
     * this code no chance to run again.  Hand the controller back now, so
     * that reset finds a clean BIOS-owned xHC instead of one still owned
     * by us with our rings installed. */
    serial_trace("WARNING",
                 "input check: last-resort takeover found no keyboard; handing the controller back");
    if (xhci_reset_controller()) {
        serial_trace("INFO", "input check: xHCI controller reset before hand-back");
    }
    xhci_restore_intel_ports();
    xhci_hid_ready = false;
    xhci_mouse_ready = false;
    xhci_legacy_release();
    xhci_build_status_line();
    return false;
}

/*
 * Hand-off to firmware.  A platform reset - and an S5 shutdown on boards
 * whose standby rail keeps the xHC alive - can keep the xHC's ownership
 * semaphore: firmware then finds OSOWNED set, refuses to initialize USB,
 * and the next boot has no HID slots at all.  Stop the controller, reset
 * it and set BIOSOWNED again after the final log flush.  The flush comes
 * first because HCRST kills the firmware disk path.
 */
static void xhci_handoff_to_firmware(const char *reason) {
    if (!xhci_present || xhci_mmio == NULL || xhci_op_base == 0) {
        serial_bootlog_flush(reason);
        return;
    }
    serial_trace("INFO", "xHCI handoff: preparing controller for firmware");
    xhci_log_controller_state("before handoff");
    serial_bootlog_flush(reason);
    serial_bootlog_detach_medium();

    if (!xhci_reset_controller()) {
        serial_trace("WARNING", "xHCI handoff: controller reset failed");
    } else {
        serial_trace("INFO", "xHCI handoff: controller reset complete");
    }
    /* The next firmware boot may use EHCI for its boot keyboard.  Put the
     * PCH mux back to the values saved before the OS routed it. */
    xhci_restore_intel_ports();
    xhci_legacy_release();
    xhci_running = false;
    xhci_ready = false;
    xhci_full_init = false;
    serial_trace("INFO", "xHCI handoff complete");
}

static void xhci_restart_handoff(void) {
    xhci_handoff_to_firmware("restart");
}

/*
 * Shutdown is the same hand-off, with the final log tagged for the reason
 * the machine is going down.  Without this the common shutdown path left
 * OSOWNED set, and the next boot's firmware skipped USB initialization.
 */
static void xhci_poweroff_handoff(void) {
    xhci_handoff_to_firmware("power off");
}

static void xhci_hid_init(void) {
    xhci_hid_init_mode(false);
}

/* Adopt a mouse without disturbing a working PS/2/SMM keyboard. */
static void xhci_mouse_init(void) {
    xhci_hid_init_mode(true);
}

static const char *xhci_input_status_text(void) {
    if (xhci_status_line[0] == '\0') {
        xhci_build_status_line();
    }
    return xhci_status_line;
}

static bool xhci_hid_input_ready(void) {
    /* Keyboard-ready only.  Mouse-only mode runs beside a working PS/2
     * keyboard, so the caller must keep draining the 8042 and keep the
     * INT 16h fallback enabled. */
    return xhci_hid_ready;
}

static void xhci_hid_poll(void) {
    if (!xhci_attempted || (!xhci_hid_ready && !xhci_mouse_ready)) {
        return;
    }
    xhci_poll_interrupt();
}

/*
 * Crash-time keyboard source.  The crash handler runs with interrupts
 * disabled and cannot use the normal IRQ path; it calls this instead of
 * reading only the 8042.  The decoder above already emits PS/2 set-1
 * scancodes into xhci_crash_queue, so the trace viewer can reuse its
 * existing scancode state machine unchanged.
 */
static void xhci_hid_crash_begin(void) {
    xhci_crash_head = 0;
    xhci_crash_tail = 0;
}

static bool xhci_hid_crash_poll(uint8_t *scancode) {
    if (scancode == NULL) {
        return false;
    }
    if (!xhci_attempted || (!xhci_hid_ready && !xhci_mouse_ready)) {
        return false;
    }
    if (xhci_crash_tail != xhci_crash_head) {
        *scancode = xhci_crash_queue[xhci_crash_tail];
        xhci_crash_tail = (xhci_crash_tail + 1u) % XHCI_CRASH_QUEUE;
        return true;
    }
    xhci_poll_interrupt();
    if (xhci_crash_tail != xhci_crash_head) {
        *scancode = xhci_crash_queue[xhci_crash_tail];
        xhci_crash_tail = (xhci_crash_tail + 1u) % XHCI_CRASH_QUEUE;
        return true;
    }
    return false;
}

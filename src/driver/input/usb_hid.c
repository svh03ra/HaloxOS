// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: usb_hid.c, USB keyboard and mouse through an xHCI controller.

// This repository is licensed under the GNU General Public License.

/*
 * A machine booted through UEFI often has no PS/2 path at all: the firmware
 * was driving its own USB keyboard, and once ExitBootServices has run there
 * is nothing left to read on port 0x60.  The same hardware booted through
 * BIOS usually keeps working, because the firmware's USB legacy emulation
 * keeps feeding the 8042 - which is why a PS/2-only driver can look healthy
 * on one firmware and silent on the same machine under the other.
 *
 * This driver therefore talks to the xHCI controller directly, so the OS
 * owns the keyboard and mouse instead of inheriting them from whoever
 * happened to boot it.  Enumeration runs synchronously at boot with bounded
 * waits everywhere (a controller that never answers must cost boot time,
 * never the boot itself), and the frame loop polls the event ring after it.
 *
 * Reports are translated into Set-1 scancodes and PS/2-shaped mouse packets,
 * so everything downstream - modifier state, the key queue, held-key state
 * for DOOM - keeps working exactly as it does for a PS/2 device.
 *
 * Physical addresses are used directly: the kernel runs with paging off, so
 * a pointer is its own bus address.  The rings and contexts the controller
 * walks by DMA are volatile, both to keep the compiler from caching them
 * across a poll and because the controller writes them behind our back.
 */

#define USB_XHCI_MAX_SLOTS     32
#define USB_CMD_TRBS           16
#define USB_EV_TRBS            256
#define USB_XFER_TRBS          16
#define USB_HID_MAX_DEVICES    4
/* Devices the controller is told about, hubs included: a hub needs a slot and
 * a device context of its own before anything behind it can be addressed, so
 * the pool is the keyboards and mice plus the hubs they hang on. */
#define USB_DEVICE_MAX         8
#define USB_HUB_MAX            3u
/* How many hubs deep the walk goes.  A keyboard is normally on the root port
 * or one hub below it; a machine with a docking station adds one more. */
#define USB_DEPTH_MAX          3u
#define USB_HUB_PORTS_MAX      16u
#define USB_CTX_BYTES          2048          /* covers 64-byte contexts too */
#define USB_REPORT_BYTES       16
/* The xHCI specification permits up to 1023 scratchpad pages.  The old
 * implementation allocated only 16 and then deliberately started the
 * controller with DCBAA[0] == 0 when a real controller asked for more.  That
 * is not a valid xHCI state: if MaxScratchpadBuffers is non-zero, software must
 * provide the Scratchpad Buffer Array and every Scratchpad Buffer before
 * entering Run mode.  Keep one shared early-boot DMA pool instead of paying the
 * full maximum once per controller.  4 MiB is enough for 1023 4 KiB pages only
 * in the aggregate up to 1024 pages; individual controllers use exactly what
 * they advertise and larger page sizes consume the corresponding number of
 * bytes. */
#define USB_XHCI_SCRATCH_MAX          1023u
#define USB_XHCI_SCRATCH_POOL_BYTES   (4u * 1024u * 1024u)
/* A machine can carry several controllers of the same kind - two or three
 * xHCI blocks is normal on a desktop board, and a USB 2.0 chipset has up to
 * six UHCI companions - so each bus keeps its controllers apart instead of
 * stopping at the first one.  That matters because the keyboard is on
 * whichever controller its port happens to belong to, not on the one the PCI
 * scan meets first. */
#define USB_XHCI_MAX_CONTROLLERS 4u
/* A controller describes its ports in "Supported Protocol" extended
 * capabilities - one for its USB2 ports and one for its USB3 ports. */
#define USB_XHCI_PROTO_MAX     4u
#define XHCI_XECP_SUPPORTED_PROTOCOL 2u
#define USB_EHCI_MAX_CONTROLLERS 2u
#define USB_UHCI_MAX_CONTROLLERS 6u
#define USB_OHCI_MAX_CONTROLLERS 2u

#define USB_PCI_CLASS_SERIAL   0x0Cu
#define USB_PCI_SUBCLASS_USB   0x03u
#define USB_PCI_PROGIF_UHCI    0x00u
#define USB_PCI_PROGIF_OHCI    0x10u
#define USB_PCI_PROGIF_EHCI    0x20u
#define USB_PCI_PROGIF_XHCI    0x30u

/* Capability registers (BAR + offset). */
#define XHCI_CAP_CAPLENGTH     0x00u
#define XHCI_CAP_HCSPARAMS1    0x04u
#define XHCI_CAP_HCSPARAMS2    0x08u
#define XHCI_CAP_HCCPARAMS1    0x10u
#define XHCI_CAP_DBOFF         0x14u
#define XHCI_CAP_RTSOFF        0x18u

/* Operational registers (BAR + CAPLENGTH + offset). */
#define XHCI_OP_USBCMD         0x00u
#define XHCI_OP_USBSTS         0x04u
#define XHCI_OP_PAGESIZE       0x08u
#define XHCI_OP_CRCR           0x18u
/* Bit 0 of the command ring register is the ring's cycle state: the state the
 * controller starts reading the ring with, which is why the address written
 * there is not the whole story (see usb_controller_start). */
#define XHCI_CRCR_RCS          0x00000001u
#define XHCI_OP_DCBAAP         0x30u
#define XHCI_OP_CONFIG         0x38u
#define XHCI_OP_PORTSC(n)      (0x400u + 0x10u * ((n) - 1u))

/* Runtime registers (BAR + RTSOFF + offset); interrupter 0 only. */
#define XHCI_RT_IMAN           0x20u
#define XHCI_RT_ERSTSZ         0x28u
#define XHCI_RT_ERSTBA         0x30u
#define XHCI_RT_ERDP           0x38u
/* ERDP bit 3 is write-one-to-clear Event Handler Busy.  It must accompany
 * every dequeue update after an event is consumed or some real controllers
 * stop posting subsequent completion events. */
#define XHCI_ERDP_EHB          0x00000008u

#define XHCI_USBCMD_RS         0x00000001u
#define XHCI_USBCMD_HCRST      0x00000002u
#define XHCI_USBSTS_HCH        0x00000001u
#define XHCI_USBSTS_HSE        0x00000004u
#define XHCI_USBSTS_EINT       0x00000008u
#define XHCI_USBSTS_PCD        0x00000010u
#define XHCI_USBSTS_CNR        0x00000800u
#define XHCI_USBSTS_HCE        0x00001000u
/* The status bits that are cleared by writing a one back.  A controller the
 * firmware left with an error latched can be reset and still carry it, and a
 * latched host controller error is one of the two ways a controller that came
 * up stops fetching commands. */
#define XHCI_USBSTS_CLEAR      (XHCI_USBSTS_HSE | XHCI_USBSTS_EINT | \
                                XHCI_USBSTS_PCD | XHCI_USBSTS_HCE)

#define XHCI_PORTSC_CCS        0x00000001u
#define XHCI_PORTSC_PED        0x00000002u
#define XHCI_PORTSC_PR         0x00000010u
#define XHCI_PORTSC_PP         0x00000200u
#define XHCI_PORTSC_SPEED_MASK 0x00003C00u
#define XHCI_PORTSC_SPEED_POS  10u
/* Link state, bits 5..8.  On a USB3 protocol port the link has to reach U0
 * before the port can carry anything, and PED reports exactly that - but a
 * port whose link is still settling reports PED 0 while PLS is already U0 on
 * some real controllers, so both are accepted. */
#define XHCI_PORTSC_PLS_MASK   0x000001E0u
#define XHCI_PORTSC_PLS_U0     0x00000000u
#define XHCI_PORTSC_PLS_U3     0x00000060u
/* Only these PORTSC bits are ours to write: port power, port reset, and the
 * write-1-to-clear change flags.  Everything else - connect status, enable
 * status, the link state field, the negotiated speed - is the controller's
 * to report, and writing a value read from it back would be asking the port
 * to change state (a link state of "Polling" written back is not a legal
 * transition). */
#define XHCI_PORTSC_CSC        0x00020000u
#define XHCI_PORTSC_PEC        0x00040000u
#define XHCI_PORTSC_WRC        0x00080000u
#define XHCI_PORTSC_OCC        0x00100000u
#define XHCI_PORTSC_PRC        0x00200000u
#define XHCI_PORTSC_PLC        0x00400000u
#define XHCI_PORTSC_CHANGES    (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | \
                                XHCI_PORTSC_WRC | XHCI_PORTSC_OCC | \
                                XHCI_PORTSC_PRC | XHCI_PORTSC_PLC)
#define XHCI_PORTSC_WRITABLE   (XHCI_PORTSC_PP | XHCI_PORTSC_PR | \
                                XHCI_PORTSC_CHANGES)

#define TRB_NORMAL             1u
#define TRB_SETUP              2u
#define TRB_DATA               3u
#define TRB_STATUS             4u
#define TRB_LINK               6u
#define TRB_NO_OP_TRANSFER     8u
#define TRB_ENABLE_SLOT        9u
#define TRB_ADDRESS_DEVICE     11u
#define TRB_CONFIGURE_EP       12u
#define TRB_EVALUATE_CONTEXT   13u
/* Type 8 is a No-Op *Transfer* TRB.  A command ring needs this distinct
 * command type; putting type 8 on it completes with TRB Error (code 5). */
#define TRB_NO_OP_COMMAND      23u
#define TRB_TRANSFER_EVENT     32u
#define TRB_CMD_COMPLETION     33u
#define TRB_PORT_STATUS        34u

#define TRB_CYCLE              0x00000001u
#define TRB_ENT                0x00000002u
#define TRB_ISP                0x00000004u
#define TRB_CHAIN              0x00000010u
#define TRB_IOC                0x00000020u
#define TRB_IDT                0x00000040u

#define USB_EP_TYPE_CONTROL    4u
#define USB_EP_TYPE_INT_IN     7u

#define USB_XFER_SUCCESS       1u
#define USB_XFER_SHORT         13u

#define USB_SPEED_FULL         1u
#define USB_SPEED_LOW          2u
#define USB_SPEED_HIGH         3u
#define USB_SPEED_SUPER        4u

#define USB_REQ_GET_DESCRIPTOR 0x06u
#define USB_REQ_SET_CONFIG     0x09u
#define USB_REQ_SET_PROTOCOL   0x0Bu

#define USB_DESC_DEVICE        1u
#define USB_DESC_CONFIG        2u
#define USB_DESC_HUB           0x29u
#define USB_CLASS_HID          3u
#define USB_CLASS_HUB          9u
#define USB_REQ_GET_STATUS     0x00u
#define USB_REQ_CLEAR_FEATURE  0x01u
#define USB_REQ_SET_FEATURE    0x03u
/* Hub class requests are addressed to a downstream port, which is neither the
 * device nor an endpoint, so the request type says "other recipient". */
#define USB_HUB_CTRL_OUT       0x23u
#define USB_HUB_CTRL_IN        0xA3u
/* The hub class descriptor is asked for with a class request, not a standard
 * one: a hub answers the standard GET_DESCRIPTOR with a stall, which reads as
 * "this hub has no descriptor" if the request type is wrong. */
#define USB_HUB_CTRL_DESC      0xA0u
#define USB_HUB_PORT_POWER     8u
#define USB_HUB_PORT_RESET     4u
/* Change selectors are the bit positions of the same names in the port status
 * word, which is why the two lists share their numbers. */
#define USB_HUB_C_CONNECTION   16u
#define USB_HUB_C_ENABLE       17u
#define USB_HUB_C_SUSPEND      18u
#define USB_HUB_C_RESET        20u
#define USB_HUB_ST_CONNECT     0x00000001u
#define USB_HUB_ST_ENABLE      0x00000002u
#define USB_HUB_ST_SUSPEND     0x00000004u
#define USB_HUB_ST_LOW         0x00000200u
#define USB_HUB_ST_HIGH        0x00000400u
#define USB_HUB_ST_C_RESET     0x00100000u
#define USB_HID_BOOT_SUBCLASS  1u
#define USB_HID_PROTO_KBD      1u
#define USB_HID_PROTO_MOUSE    2u

#define USB_HID_KIND_KEYBOARD  1u
#define USB_HID_KIND_MOUSE     2u
#define USB_HID_KIND_HUB       3u

/* Event ring consumer state (interrupter 0). */
typedef struct {
    volatile uint32_t *base;
    uint32_t trbs;                  /* usable TRBs; index `trbs` is the link */
    uint32_t index;
    uint32_t cycle;
} UsbRing;

typedef struct {
    volatile uint32_t *cap;
    volatile uint32_t *op;
    volatile uint32_t *rt;
    volatile uint32_t *db;
    uint32_t max_ports;
    uint32_t max_slots;
    uint32_t ctx_dwords;            /* 8 for 32-byte contexts, 16 for 64-byte */
    /*
     * Where this controller says its USB2 and USB3 port ranges are, read from
     * its "Supported Protocol" extended capability.  A USB3 port's PORTSC does
     * not mean what a USB2 port's does - the link state carries the meaning the
     * enable bit carries on USB2, and the speed field is not what says the
     * device is SuperSpeed - so the ranges come from the controller instead of
     * being assumed from port numbering.
     */
    uint8_t proto_rev[USB_XHCI_PROTO_MAX];      /* 2 or 3 */
    uint8_t proto_first[USB_XHCI_PROTO_MAX];    /* first port of the range */
    uint8_t proto_ports[USB_XHCI_PROTO_MAX];    /* how many ports it covers */
    uint8_t proto_entries;
    uint8_t bus;                    /* where this controller was found */
    uint8_t slot;
    uint8_t function;
    bool started;
    /* The event ring consumer position belongs to the controller, not to the
     * driver: two controllers are drained in turn, and each one's consumer
     * cycle bit has to survive the other's turn. */
    uint32_t ev_index;
    uint32_t ev_cycle;
} UsbXhci;

/*
 * Shared HID translation state: what the last report of one device said.
 *
 * A USB keyboard reports the set of keys it is holding, so a scancode is
 * only new when it was not in the previous report - which means the previous
 * report has to be remembered per device, not per controller.  The same
 * state is used whichever bus the device arrived on, so an EHCI keyboard and
 * an xHCI keyboard are indistinguishable from this point on.
 */
typedef struct {
    uint8_t last_usage[6];
    uint8_t last_mods;
} UsbHidRoute;

typedef struct {
    bool in_use;
    uint8_t controller;             /* which xHCI block this device is on */
    uint8_t slot;
    uint8_t port;
    uint8_t speed;
    uint8_t kind;                   /* 0 = none, 1 = keyboard, 2 = mouse */
    uint8_t ep_number;
    uint8_t ep_dci;                 /* device context index of the endpoint */
    uint16_t ep0_mps;
    uint16_t transfer_bytes;
    uint32_t errors;
    uint32_t index;                 /* this device's slot in usb_hid[] */
    /*
     * Where the device hangs.
     *
     * A device on a root port has route 0 and names its port in root_port.  A
     * device behind a hub cannot be found from a register: the controller is
     * told the path in the slot context's route string, one port number per
     * tier, and a full or low speed device behind a hi-speed hub also names
     * the hub slot and port whose transaction translator carries it.
     */
    uint32_t path;                  /* route string of this device */
    uint32_t child_shift;           /* where a child's port number goes in it */
    uint8_t root_port;              /* the root port the whole chain hangs from */
    uint8_t tt_slot;                /* hi-speed hub acting as this device's TT */
    uint8_t tt_port;
    uint8_t tt_think;
    uint8_t depth;                  /* hubs above this device */
    uint8_t ports;                  /* hubs: how many downstream ports */
    bool is_hub;
    bool multi_tt;
    uint32_t next_scan;             /* hubs: when to look at its ports again */
    volatile uint32_t *ctx;         /* the context the controller walks */
    UsbRing ep0;
    UsbRing ep;
    volatile uint32_t *pending;     /* interrupt transfer currently posted */
    volatile uint8_t *report;
    UsbHidRoute route;
} UsbHidDevice;

static UsbXhci usb_xhci_all[USB_XHCI_MAX_CONTROLLERS];
static uint32_t usb_xhci_cur = 0u;      /* controller the helpers below act on */
/* Every access below means "the controller being brought up right now":
 * bringing one up is a straight-line sequence, so the code reads the same as
 * it did with a single controller while the state stays per controller. */
#define usb_xhci usb_xhci_all[usb_xhci_cur]
static uint32_t usb_xhci_count = 0u;    /* xHCI blocks that started */
/* How long a port whose enumeration failed is left alone before it is tried
 * again, so a device that cannot be configured costs one attempt every few
 * seconds instead of one every frame. */
#define USB_PORT_RETRY_TICKS (2u * TIMER_HZ)
#define USB_PORT_SLOTS        32u
static uint32_t usb_port_next_try[USB_XHCI_MAX_CONTROLLERS][USB_PORT_SLOTS];
static uint32_t usb_events_seen = 0;
static uint32_t usb_reports_seen = 0;
static uint32_t usb_key_reports_logged = 0;
static uint32_t usb_scancode_traced = 0;
static UsbRing usb_cmd[USB_XHCI_MAX_CONTROLLERS];
static UsbHidDevice usb_hid[USB_DEVICE_MAX];
static uint32_t usb_hid_count = 0;       /* devices that became input */
static uint32_t usb_dev_next = 0;        /* next unused record */
static uint32_t usb_hubs_total = 0;
static uint32_t usb_hid_keyboards = 0;
static uint32_t usb_hid_mice = 0;
static bool usb_hid_ready = false;
static uint32_t usb_cmd_slot = 0;
static uint32_t usb_cmd_code = 0;
/* Consecutive address attempts per record slot.  A port can still be settling
 * when it is first read, so a failed address is retried - but every attempt
 * burns a controller slot, and a controller never gives an address back, so
 * the retries are bounded and the device is then left as a silent one. */
static uint32_t usb_addr_attempts[USB_DEVICE_MAX];

/*
 * Every address this driver hands the controller - the device context array,
 * the rings, the event ring segment table, a device context, the report buffer
 * a transfer writes into - is DERIVED from one of the arrays below by adding a
 * whole number of entries to its base, and the low six bits of all of those
 * fields are reserved.  A controller masks them off and fetches from the
 * rounded address, which is memory that was never written for it: the command
 * ring answers with silence and a transfer ring with a stall or a dead
 * endpoint, and neither reports anything that says alignment was the reason.
 *
 * That is the second half of why a driver can pass every test on an emulated
 * controller - QEMU keeps the address it was given, however it is aligned - and
 * fail on the first real one.  So each stride here is rounded up to a whole
 * number of 64 byte lines: with the array itself aligned, every element of it
 * is too, whichever controller or device it belongs to.
 */
#define USB_ALIGN64(value) ((((value) + 63u) / 64u) * 64u)

static volatile uint64_t usb_dcbaa[USB_XHCI_MAX_CONTROLLERS]
                                    [USB_ALIGN64((USB_XHCI_MAX_SLOTS + 1u) * 8u) / 8u]
    __attribute__((aligned(64)));
/* The scratchpad page pool and the array of pointers that names it: entry 0 of
 * the device context array points at the array when a controller asks. */
static volatile uint64_t usb_scratch_array[USB_XHCI_MAX_CONTROLLERS]
                                          [USB_ALIGN64(USB_XHCI_SCRATCH_MAX * 8u) / 8u]
    __attribute__((aligned(64)));
/* Shared early-boot scratchpad storage.  Each controller gets a page-aligned
 * slice.  The kernel runs with paging disabled, so these physical addresses are
 * directly DMA-visible to xHCI as long as the slice stays below 4 GiB. */
static volatile uint8_t usb_scratch_pool[USB_XHCI_SCRATCH_POOL_BYTES]
    __attribute__((aligned(4096)));
static uint32_t usb_scratch_pool_used = 0u;
static uint32_t usb_scratch_base[USB_XHCI_MAX_CONTROLLERS];
static uint32_t usb_scratch_page_size[USB_XHCI_MAX_CONTROLLERS];
static bool usb_scratch_allocated[USB_XHCI_MAX_CONTROLLERS];
static uint32_t usb_scratch_count[USB_XHCI_MAX_CONTROLLERS];
/*
 * Each ring carries one extra TRB for its link, and a TRB is sixteen bytes -
 * four dwords - so the array is sized in dwords as (trbs + 1) * 4, rounded up
 * to a 64-byte line.  Sizing it as (trbs + 1) dwords instead costs three
 * quarters of the ring: the zeroing pass in usb_ring_init alone runs to the
 * programmed number of TRBs, and the link TRB is written just past them, so
 * bring-up writes hundreds of bytes past the end of these arrays and into
 * whatever static follows them.  Nothing about that is visible from a
 * register: the controller fetches from the address it was given, the sizes
 * it was told are the same either way, and the damage lands on the ring next
 * door - which is the ring events arrive through.
 */
static volatile uint32_t usb_cmd_ring[USB_XHCI_MAX_CONTROLLERS]
                                     [USB_ALIGN64((USB_CMD_TRBS + 1u) * 16u) / 4u]
    __attribute__((aligned(64)));
static volatile uint32_t usb_ev_ring[USB_XHCI_MAX_CONTROLLERS]
                                    [USB_ALIGN64((USB_EV_TRBS + 1u) * 16u) / 4u]
    __attribute__((aligned(64)));
static volatile uint32_t usb_erst[USB_XHCI_MAX_CONTROLLERS][USB_ALIGN64(16u) / 4u]
    __attribute__((aligned(64)));
static volatile uint32_t usb_in_ctx[USB_XHCI_MAX_CONTROLLERS][USB_ALIGN64(USB_CTX_BYTES) / 4]
    __attribute__((aligned(64)));
static volatile uint32_t usb_dev_ctx[USB_DEVICE_MAX][USB_ALIGN64(USB_CTX_BYTES) / 4]
    __attribute__((aligned(64)));
static volatile uint32_t usb_trb_ring[USB_DEVICE_MAX * 2]
                                    [USB_ALIGN64((USB_XFER_TRBS + 1u) * 16u) / 4u]
    __attribute__((aligned(64)));
static volatile uint8_t usb_report[USB_DEVICE_MAX][USB_ALIGN64(USB_REPORT_BYTES)]
    __attribute__((aligned(64)));
static uint8_t usb_desc[512] __attribute__((aligned(16)));

/* ---------------------------------------------------------------------- */
/* Register and timing helpers                                            */
/* ---------------------------------------------------------------------- */

static uint32_t usb_read32(volatile uint32_t *reg) {
    return *reg;
}

static void usb_write32(volatile uint32_t *reg, uint32_t value) {
    *reg = value;
}

static void usb_write64(volatile uint32_t *reg, uint32_t low) {
    reg[0] = low;
    reg[1] = 0;
}

static void usb_zero_bytes(volatile uint8_t *base, uint32_t length) {
    uint32_t i;

    for (i = 0u; i < length; ++i) {
        base[i] = 0u;
    }
}

static uint32_t usb_xhci_page_size_from_reg(uint32_t pagesize) {
    uint32_t bit;

    /* xHCI 1.22 defines a single implementation page size.  Reject a broken
     * bitmap rather than silently using 4 KiB against an 8/16/32 KiB part. */
    if (pagesize == 0u || (pagesize & (pagesize - 1u)) != 0u) {
        return 0u;
    }
    for (bit = 0u; bit < 16u; ++bit) {
        if ((pagesize & (1u << bit)) != 0u) {
            return 1u << (12u + bit);
        }
    }
    return 0u;
}

/* Short busy-wait on the timer, capped so a machine whose timer interrupt
 * never arrives still finishes booting. */
static void usb_wait_ticks(uint32_t ticks) {
    uint32_t start = timer_ticks;
    uint32_t spins = 0;

    while (timer_ticks - start < ticks) {
        if (++spins > 20000000u) {
            return;
        }
    }
}

/*
 * Bounded register poll in the style of the PS/2 driver's loops - bounded
 * twice, and the second bound is the one real hardware cares about.
 *
 * A spin cap alone is a wall-clock lie on silicon: one iteration is an MMIO
 * read, which costs hundreds of nanoseconds on a real chipset, so a cap of
 * forty million is tens of seconds per wait and a controller that never
 * clears the bit can cost the boot minutes while looking "bounded".  The
 * tick deadline is therefore checked alongside the spin count, and the
 * spin count stays as the fallback for a machine whose timer interrupt
 * has not been proven to run yet.
 */
static bool usb_wait_bits(volatile uint32_t *reg, uint32_t mask, uint32_t value,
                          uint32_t spins, uint32_t ticks) {
    uint32_t start = timer_ticks;
    uint32_t i;

    for (i = 0; i < spins; ++i) {
        if ((usb_read32(reg) & mask) == value) {
            return true;
        }
        if ((i & 0xFFFFu) == 0u && ticks != 0u &&
            timer_ticks - start >= ticks) {
            return false;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Rings                                                                  */
/* ---------------------------------------------------------------------- */

static void usb_ring_init(UsbRing *ring, volatile uint32_t *base, uint32_t trbs) {
    uint32_t i;

    ring->base = base;
    ring->trbs = trbs;
    ring->index = 0;
    ring->cycle = 1;
    for (i = 0; i < (trbs + 1u) * 4u; ++i) {
        base[i] = 0;
    }
    /* The link TRB closes the ring: its parameter is the address to jump
     * back to (a link with TC set really does use it), and it carries the
     * initial cycle bit. */
    base[trbs * 4u + 0u] = (uint32_t)(uintptr_t)base;
    base[trbs * 4u + 1u] = 0;
    base[trbs * 4u + 3u] = (TRB_LINK << 10) | TRB_ENT | 1u;
}

/* Hand out the next producer TRB.  Re-arming the link carries the cycle bit
 * the consumer expects and then toggles it, which is what makes each pass
 * around the ring a new generation. */
static volatile uint32_t *usb_ring_reserve(UsbRing *ring) {
    if (ring->index >= ring->trbs) {
        ring->base[ring->trbs * 4u + 3u] =
            (TRB_LINK << 10) | TRB_ENT | ring->cycle;
        ring->cycle ^= 1u;
        ring->index = 0;
    }
    return &ring->base[ring->index * 4u];
}

static void usb_ring_commit(UsbRing *ring) {
    ++ring->index;
}

static void usb_ring_push(UsbRing *ring, uint32_t d0, uint32_t d1, uint32_t d2,
                          uint32_t d3) {
    volatile uint32_t *trb = usb_ring_reserve(ring);

    trb[0] = d0;
    trb[1] = d1;
    trb[2] = d2;
    trb[3] = d3 | ring->cycle;
    usb_ring_commit(ring);
}

/* ---------------------------------------------------------------------- */
/* Event ring                                                             */
/* ---------------------------------------------------------------------- */

static volatile uint32_t *usb_event_peek(void) {
    return &usb_ev_ring[usb_xhci_cur][usb_xhci.ev_index * 4u];
}

static void usb_event_advance(void) {
    ++usb_events_seen;
    ++usb_xhci.ev_index;
    if (usb_xhci.ev_index >= USB_EV_TRBS) {
        usb_xhci.ev_index = 0;
        usb_xhci.ev_cycle ^= 1u;
    }
    usb_write64(usb_xhci.rt + XHCI_RT_ERDP / 4u,
                (uint32_t)(uintptr_t)&usb_ev_ring[usb_xhci_cur]
                    [usb_xhci.ev_index * 4u] | XHCI_ERDP_EHB);
}

static void usb_interrupter_reset(void) {
    usb_xhci.ev_index = 0;
    usb_xhci.ev_cycle = 1;
    usb_write64(usb_xhci.rt + XHCI_RT_ERDP / 4u,
                (uint32_t)(uintptr_t)usb_ev_ring[usb_xhci_cur] | XHCI_ERDP_EHB);
}

/*
 * Reading this ring is a two-sided walk: the controller writes each event at
 * its own producer position and flips its cycle bit once per lap, and this
 * driver reads at its consumer position and flips its own cycle bit at the
 * same lap boundary.  The two stay aligned only while every event is taken
 * off the ring exactly once, and the failure mode when one is not is not a
 * delay - it is a wedge: the driver then reads a slot the controller has not
 * written, sees the cycle bit of an untouched TRB, and concludes there is no
 * event, while completion events keep being written into a ring nothing is
 * reading.  Every wait below therefore advances the ring at the point where
 * it has read an event, and no wait may return an event it has left in place.
 */

/* ---------------------------------------------------------------------- */
/* HID reports                                                            */
/* ---------------------------------------------------------------------- */

/* HID usage -> PS/2 Set-1 scancode.  The Set-1 stream is what every
 * downstream consumer already decodes, so a USB key and a PS/2 key are
 * indistinguishable from the driver boundary onwards. */
static uint8_t usb_usage_to_scancode(uint8_t usage, bool *extended) {
    static const uint8_t letters[26] = {
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
        0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,
        0x15, 0x2C
    };
    /* Set-1 order is 1..9 then 0, but HID usage order is 1..9 then 0 as well:
     * usage 0x1E is the '1' key.  Keep the table indexed by that order, i.e.
     * entry 0 is '1' (0x02) and entry 9 is '0' (0x0B). */
    static const uint8_t digits[10] = {
        0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B
    };
    static const uint8_t keypad[10] = {
        0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52
    };

    *extended = false;
    if (usage >= 0x04u && usage <= 0x1Du) {
        return letters[usage - 0x04u];
    }
    if (usage >= 0x1Eu && usage <= 0x27u) {
        return digits[usage - 0x1Eu];
    }
    if (usage >= 0x3Au && usage <= 0x45u) {
        return (uint8_t)(0x3Bu + (usage - 0x3Au));          /* F1..F12 */
    }
    if (usage >= 0x59u && usage <= 0x62u) {
        return keypad[usage - 0x59u];                        /* keypad 1..9, 0 */
    }
    switch (usage) {
        case 0x28: return 0x1C;                              /* Enter */
        case 0x29: return 0x01;                              /* Escape */
        case 0x2A: return 0x0E;                              /* Backspace */
        case 0x2B: return 0x0F;                              /* Tab */
        case 0x2C: return 0x39;                              /* Space */
        case 0x2D: return 0x0C;
        case 0x2E: return 0x0D;
        case 0x2F: return 0x1A;
        case 0x30: return 0x1B;
        case 0x31: return 0x2B;
        case 0x33: return 0x27;
        case 0x34: return 0x28;
        case 0x35: return 0x29;
        case 0x36: return 0x33;
        case 0x37: return 0x34;
        case 0x38: return 0x35;
        case 0x39: return 0x3A;                              /* Caps Lock */
        case 0x47: return 0x46;                              /* Scroll Lock */
        case 0x49: *extended = true; return 0x52;            /* Insert */
        case 0x4A: *extended = true; return 0x47;            /* Home */
        case 0x4B: *extended = true; return 0x49;            /* Page Up */
        case 0x4C: *extended = true; return 0x53;            /* Delete */
        case 0x4D: *extended = true; return 0x4F;            /* End */
        case 0x4E: *extended = true; return 0x51;            /* Page Down */
        case 0x4F: *extended = true; return 0x4D;            /* Right */
        case 0x50: *extended = true; return 0x4B;            /* Left */
        case 0x51: *extended = true; return 0x50;            /* Down */
        case 0x52: *extended = true; return 0x48;            /* Up */
        case 0x53: return 0x45;                              /* Num Lock */
        case 0x54: *extended = true; return 0x35;            /* Keypad / */
        case 0x55: return 0x37;                              /* Keypad * */
        case 0x56: return 0x4A;                              /* Keypad - */
        case 0x57: return 0x4E;                              /* Keypad + */
        case 0x58: *extended = true; return 0x1C;            /* Keypad Enter */
        case 0x63: return 0x53;                              /* Keypad . */
        default: return 0;
    }
}

static void usb_scancode_make(uint8_t scancode, bool extended) {
    ++input_usb_scancodes;
    if (usb_scancode_traced < 8u) {
        ++usb_scancode_traced;
        serial_trace_hex_value("INFO", "USB key make scancode",
                               (uint32_t)scancode | (extended ? 0x100u : 0u));
    }
    if (extended) {
        handle_scancode(0xE0u);
    }
    handle_scancode(scancode);
}

static void usb_scancode_break(uint8_t scancode, bool extended) {
    ++input_usb_scancodes;
    if (extended) {
        handle_scancode(0xE0u);
    }
    handle_scancode((uint8_t)(scancode | 0x80u));
}

static void usb_modifier(UsbHidRoute *route, uint8_t bit, uint8_t modifiers,
                         uint8_t scancode, bool extended) {
    bool was = (route->last_mods & bit) != 0;
    bool now = (modifiers & bit) != 0;

    if (now == was) {
        return;
    }
    if (now) {
        usb_scancode_make(scancode, extended);
    } else {
        usb_scancode_break(scancode, extended);
    }
}

/*
 * Translate one boot-protocol keyboard report into PS/2 scancodes.  Shared by
 * every bus: a report is a report, whichever controller delivered it.
 */
static void usb_route_keyboard(UsbHidRoute *route, volatile uint8_t *report,
                               uint32_t length) {
    uint8_t modifiers = report[0];
    uint32_t i;
    uint32_t j;

    if (length < 3u) {
        return;
    }
    ++usb_reports_seen;
    if (report[2] != 0u && usb_key_reports_logged < 8u) {
        ++usb_key_reports_logged;
        serial_trace_uint_value("INFO", "USB HID key report, total reports",
                                usb_reports_seen);
        serial_trace_hex_value("INFO", "  modifiers/usage",
                               (uint32_t)report[0] |
                               ((uint32_t)report[2] << 8));
    }
    usb_modifier(route, 0x01u, modifiers, 0x1D, false);      /* Left Ctrl */
    usb_modifier(route, 0x02u, modifiers, 0x2A, false);      /* Left Shift */
    usb_modifier(route, 0x04u, modifiers, 0x38, false);      /* Left Alt */
    usb_modifier(route, 0x08u, modifiers, 0x5B, true);       /* Left GUI */
    usb_modifier(route, 0x10u, modifiers, 0x1D, true);       /* Right Ctrl */
    usb_modifier(route, 0x20u, modifiers, 0x36, false);      /* Right Shift */
    usb_modifier(route, 0x40u, modifiers, 0x38, true);       /* Right Alt */
    usb_modifier(route, 0x80u, modifiers, 0x5Cu, true);      /* Right GUI */
    route->last_mods = modifiers;

    /* Releases first: a key still listed in the report is untouched. */
    for (i = 0; i < 6u; ++i) {
        uint8_t usage = route->last_usage[i];
        bool still_held = false;
        bool extended = false;
        uint8_t scancode;

        if (usage == 0 || (usage >= 0xE0u && usage <= 0xE7u)) {
            continue;                       /* modifiers live in byte 0 */
        }
        for (j = 0; j < 6u; ++j) {
            if (report[2u + j] == usage) {
                still_held = true;
                break;
            }
        }
        if (still_held) {
            continue;
        }
        scancode = usb_usage_to_scancode(usage, &extended);
        if (scancode != 0) {
            usb_scancode_break(scancode, extended);
        }
    }
    for (i = 0; i < 6u; ++i) {
        uint8_t usage = report[2u + i];
        bool already = false;
        bool extended = false;
        uint8_t scancode;

        if (usage == 0 || (usage >= 0xE0u && usage <= 0xE7u)) {
            continue;
        }
        for (j = 0; j < 6u; ++j) {
            if (route->last_usage[j] == usage) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        scancode = usb_usage_to_scancode(usage, &extended);
        if (scancode != 0) {
            usb_scancode_make(scancode, extended);
        }
    }
    for (i = 0; i < 6u; ++i) {
        route->last_usage[i] = report[2u + i];
    }
    last_input_tick = timer_ticks;
    ui_input_dirty = true;
}

static void usb_mouse_report(volatile uint8_t *report, uint32_t length) {
    uint8_t packet[3];

    if (length < 3u) {
        return;
    }
    /* The PS/2 decoder downstream expects the always-set bit 3 in byte 0. */
    packet[0] = (uint8_t)((report[0] & 0x07u) | 0x08u);
    packet[1] = report[1];
    packet[2] = report[2];
    ++input_usb_mouse_packets;
    for (uint32_t i = 0; i < 3u; ++i) {
        mouse_apply_byte(packet[i]);
    }
}

/* ---------------------------------------------------------------------- */
/* Events                                                                 */
/* ---------------------------------------------------------------------- */

/* Ring a device slot's doorbell.  The target is the Device Context Index and
 * it belongs in the register's low byte: a target written into bits 15:8 is
 * read as zero (DCI 0, which no endpoint has) and the doorbell is simply
 * ignored, with no error reported anywhere.  The host controller doorbell is
 * value 0 in either encoding, which is why commands work even when this is
 * wrong. */
static void usb_ring_doorbell(uint8_t slot, uint8_t dci) {
    usb_write32(usb_xhci.db + slot, (uint32_t)dci);
}

/*
 * Device Context Index of an endpoint.
 *
 * This number is one index used in four places, and it has to be the same
 * number in all four or the endpoint is unreachable:
 *
 *   - the add/drop flag bit of the Input Control Context (bit 0 is the slot
 *     context, bit 1 is endpoint 0, and so on)
 *   - the offset of the endpoint's context in the input context, counted from
 *     its slot context (entry 0 there is the Input Control Context)
 *   - the Context Entries field of the slot context, which is the largest DCI
 *     in use
 *   - the value written to the device's doorbell register to make the
 *     controller fetch that endpoint's ring
 *
 * Endpoint 0 is DCI 1 - one context serving both directions - and every other
 * endpoint takes a pair, so endpoint n is 2n OUT or 2n+1 IN.  Linux and U-Boot
 * both number the doorbell target as (endpoint index + 1) with the endpoint
 * index being 2n or 2n+1, which is this same numbering, and it is what real
 * controllers accept.
 *
 * An emulated controller does not check it: QEMU keys its endpoint contexts
 * off whichever bits the add flags name, so a driver can be off by one here
 * and still work in every virtual machine - the transfer is fetched, the key
 * reports arrive, and the only machine that notices is the one that mattered.
 */
static uint32_t usb_endpoint_dci(uint8_t ep_number, bool dir_in) {
    if (ep_number == 0u) {
        return 1u;                       /* endpoint 0, both directions */
    }
    return (uint32_t)ep_number * 2u + (dir_in ? 1u : 0u);
}

/*
 * The Interval field is a period exponent, not the descriptor's bInterval.
 * For high and SuperSpeed it is (bInterval - 1), counting 125 us microframes.
 * For full and low speed bInterval counts 1 ms frames, so a keyboard that
 * asks for 10 would be polled about once a second; that is clamped to 32 ms,
 * which only ever means polling early (the device NAKs until it has news).
 */
static uint32_t usb_interval_exponent(uint8_t speed, uint8_t b_interval) {
    if (speed == USB_SPEED_HIGH || speed == USB_SPEED_SUPER) {
        if (b_interval == 0u) {
            return 0u;
        }
        return b_interval > 16u ? 15u : (uint32_t)b_interval - 1u;
    }
    return b_interval > 5u ? 5u : b_interval;
}

static void usb_post_interrupt_transfer(UsbHidDevice *device) {
    device->pending = usb_ring_reserve(&device->ep);
    device->pending[0] = (uint32_t)(uintptr_t)device->report;
    device->pending[1] = 0;
    device->pending[2] = (uint32_t)device->transfer_bytes;
    device->pending[3] = (TRB_NORMAL << 10) | TRB_IOC | TRB_ISP | device->ep.cycle;
    usb_ring_commit(&device->ep);
    usb_ring_doorbell(device->slot, device->ep_dci);
}

static void usb_handle_transfer_event(volatile uint32_t *trb) {
    uint32_t pointer = trb[0];
    uint32_t code = (trb[2] >> 24) & 0xFFu;
    uint32_t residual = trb[2] & 0xFFFFFFu;
    uint32_t i;

    for (i = 0; i < usb_dev_next; ++i) {
        UsbHidDevice *device = &usb_hid[i];
        uint32_t delivered;

        if (!device->in_use || device->pending == NULL ||
            (uint32_t)(uintptr_t)device->pending != pointer) {
            continue;
        }
        if (code == USB_XFER_SUCCESS || code == USB_XFER_SHORT) {
            delivered = device->transfer_bytes;
            if (code == USB_XFER_SHORT) {
                delivered = residual < delivered ? delivered - residual : 0;
            }
            if (device->kind == USB_HID_KIND_KEYBOARD) {
                usb_route_keyboard(&device->route, device->report, delivered);
            } else if (device->kind == USB_HID_KIND_MOUSE) {
                usb_mouse_report(device->report, delivered);
            }
        } else {
            ++device->errors;
            if (device->errors <= 2u) {
                serial_trace_uint_value("WARNING", "USB transfer failed, code", code);
            }
        }
        if (device->errors < 8u) {
            usb_post_interrupt_transfer(device);
        }
        return;
    }
}

/* Drain the event ring.  Returns the Transfer Event that completed `expected`
 * or NULL if the bounded spin ran out. */
static volatile uint32_t *usb_wait_transfer(volatile uint32_t *expected,
                                            uint32_t spins) {
    uint32_t i;
    uint32_t seen = 0;
    uint32_t deadline = timer_ticks + 120u;

    for (i = 0; i < spins; ++i) {
        volatile uint32_t *trb = usb_event_peek();
        uint32_t dword3 = trb[3];
        uint32_t type;

        if ((dword3 & TRB_CYCLE) != usb_xhci.ev_cycle) {
            if (i == 1000000u && seen == 0) {
                serial_trace("WARNING", "USB transfer wait: no event at all");
                serial_trace_hex_value("WARNING", "  usbsts",
                                       usb_read32(usb_xhci.op + XHCI_OP_USBSTS / 4u));
                serial_trace_hex_value("WARNING", "  erdp index/cycle",
                                       (usb_xhci.ev_index << 8) | usb_xhci.ev_cycle);
            }
            /* Same double bound as the command wait: spins alone are
             * milliseconds of wall clock on silicon, and a control
             * transfer to a slow device is allowed to take a real
             * fraction of a second. */
            if ((i & 0xFFFFu) == 0u &&
                (int32_t)(timer_ticks - deadline) >= 0) {
                return NULL;
            }
            continue;
        }
        ++seen;
        type = (dword3 >> 10) & 0x3Fu;
        if (seen <= 6u) {
            serial_trace_uint_value("INFO", "  USB event type", type);
            serial_trace_hex_value("INFO", "    trb pointer", trb[0]);
            serial_trace_hex_value("INFO", "    expected",
                                   (uint32_t)(uintptr_t)expected);
            serial_trace_uint_value("INFO", "    code", (trb[2] >> 24) & 0xFFu);
            serial_trace_uint_value("INFO", "    residual", trb[2] & 0xFFFFFFu);
        }
        if (type == TRB_TRANSFER_EVENT) {
            if ((uint32_t)(uintptr_t)expected == trb[0]) {
                uint32_t code = (trb[2] >> 24) & 0xFFu;

                if (code != USB_XFER_SUCCESS && code != USB_XFER_SHORT) {
                    serial_trace_uint_value("WARNING", "USB transfer failed, code", code);
                }
                /* The event has been read, so this is where the ring position
                 * moves past it - not in the caller.  A matched event left in
                 * place is read a second time by the next wait, and a ring
                 * whose events are not each read exactly once drifts off the
                 * controller's own position and then reads nothing at all
                 * - which is why the order matters more here than anywhere
                 * else in this file (see the note above usb_event_advance). */
                usb_event_advance();
                return trb;
            }
            usb_handle_transfer_event(trb);          /* someone else's report */
        }
        usb_event_advance();
    }
    return NULL;
}

static void usb_service_events(void) {
    uint32_t guard = 0;
    uint32_t iman;

    if (!usb_xhci.started) {
        return;
    }
    while (guard++ < 512u) {
        volatile uint32_t *trb = usb_event_peek();
        uint32_t dword3 = trb[3];
        uint32_t type;

        if ((dword3 & TRB_CYCLE) != usb_xhci.ev_cycle) {
            break;
        }
        type = (dword3 >> 10) & 0x3Fu;
        if (usb_events_seen <= 8u) {
            serial_trace_uint_value("INFO", "  poll event type", type);
            serial_trace_hex_value("INFO", "    pointer", trb[0]);
            serial_trace_uint_value("INFO", "    code", (trb[2] >> 24) & 0xFFu);
        }
        if (type == TRB_TRANSFER_EVENT) {
            usb_handle_transfer_event(trb);
        }
        usb_event_advance();
    }
    /* The controller's interrupt is never enabled; ack it anyway so its
     * pending bit cannot pile up while we poll. */
    iman = usb_read32(usb_xhci.rt + XHCI_RT_IMAN / 4u);
    if ((iman & 1u) != 0) {
        usb_write32(usb_xhci.rt + XHCI_RT_IMAN / 4u, iman);
    }
}

/* ---------------------------------------------------------------------- */
/* Commands and control transfers                                         */
/* ---------------------------------------------------------------------- */

/*
 * One attempt at a command: post the TRB, ring the host controller doorbell,
 * and wait for the Completion Event that names it.  The event is matched by
 * the address of the TRB that was posted, which is what makes it safe to wait
 * for one here at all - events for transfers already in flight arrive in
 * between and are drained as they come.
 */
static bool usb_command_once(const char *what, uint32_t d0, uint32_t d1,
                              uint32_t d2, uint32_t d3) {
    volatile uint32_t *posted = usb_ring_reserve(&usb_cmd[usb_xhci_cur]);
    uint32_t spins;
    uint32_t deadline = timer_ticks + 120u;

    usb_ring_push(&usb_cmd[usb_xhci_cur], d0, d1, d2, d3);
    usb_write32(usb_xhci.db + 0u, 0u);

    usb_cmd_slot = 0;
    usb_cmd_code = 0;
    for (spins = 0; spins < 4000000u; ++spins) {
        volatile uint32_t *trb = usb_event_peek();
        uint32_t dword3 = trb[3];
        uint32_t type;

        if ((dword3 & TRB_CYCLE) != usb_xhci.ev_cycle) {
            /* The spin cap is the bound for a machine whose timer never
             * advances; the deadline is the one that bounds a healthy
             * machine, where four million RAM peeks are over in
             * milliseconds and a real controller may legitimately take
             * longer than that to answer. */
            if ((spins & 0xFFFFu) == 0u &&
                (int32_t)(timer_ticks - deadline) >= 0) {
                break;
            }
            continue;
        }
        type = (dword3 >> 10) & 0x3Fu;
        if (type == TRB_CMD_COMPLETION &&
            (uint32_t)(uintptr_t)posted == trb[0]) {
            usb_cmd_code = (trb[2] >> 24) & 0xFFu;
            usb_cmd_slot = (trb[3] >> 24) & 0xFFu;
            usb_event_advance();
            if (usb_cmd_code != USB_XFER_SUCCESS) {
                serial_trace_uint_value("WARNING", what, usb_cmd_code);
                return false;
            }
            return true;
        }
        if (type == TRB_TRANSFER_EVENT) {
            usb_handle_transfer_event(trb);
        }
        usb_event_advance();
    }
    serial_trace_uint_value("WARNING", "USB command never completed, spins", spins);
    return false;
}

/*
 * Record what a command that was not answered leaves behind.
 *
 * These five numbers are what tells the two causes apart on a machine with no
 * other channel: how far into the command ring the controller got, whether a
 * completion event came back at all and with what code, what its status
 * register says, and whether the PCI command register still has bus mastering
 * and memory space set.  A ring that was never fetched with a clean status
 * register is one fault; a ring that was fetched and answered with an error
 * code is another; a status register with a host controller error in it is a
 * third.  The boot menu prints them, so the machine says which.
 */
static void usb_xhci_command_failed(const char *what) {
    uint32_t crcr = usb_read32(usb_xhci.op + XHCI_OP_CRCR / 4u);
    uint32_t base = (uint32_t)(uintptr_t)usb_cmd_ring[usb_xhci_cur];

    usb_xhci_command_state = USB_XHCI_COMMANDS_SILENT;
    usb_xhci_command_code = usb_cmd_code;
    usb_xhci_usbsts = usb_read32(usb_xhci.op + XHCI_OP_USBSTS / 4u);
    usb_xhci_pci_command = pci_config_read32(usb_xhci.bus, usb_xhci.slot,
                                             usb_xhci.function, 0x04);
    usb_xhci_command_deq = (crcr & ~0x1Fu) >= base ? (crcr & ~0x1Fu) - base : 0u;
    serial_trace_uint_value("WARNING", what, usb_xhci_command_code);
    serial_trace_hex_value("WARNING", "  command ring dequeue offset",
                           usb_xhci_command_deq);
    serial_trace_hex_value("WARNING", "  usbsts", usb_xhci_usbsts);
    serial_trace_hex_value("WARNING", "  crcr", crcr);
    serial_trace_hex_value("WARNING", "  pci command", usb_xhci_pci_command);
}

/* Defined with the controller bring-up at the bottom of the file: it is used
 * here to put a controller that stopped answering back into a known state. */
static bool usb_xhci_recover(void);

/* Whether a command that failed has already been retried after a controller
 * reset.  One retry: the retry costs a controller reset and re-programming,
 * and a controller that needed it twice will not be helped by a third. */
static bool usb_xhci_command_retried = false;

/*
 * Issue a command, and give a controller that does not answer one second
 * chance from a known state.
 *
 * The state a controller is left in by the firmware is not something this
 * driver can inspect: the ownership handoff says the firmware let go of the
 * ports, not that it left the command engine running where the specification
 * says a reset leaves it.  Halting, resetting and programming the controller
 * again is the one thing that clears every one of those states at once, and a
 * command that then completes is a machine with a keyboard instead of a
 * machine that reports a device on every port and never talks to one.
 */
static bool usb_command(const char *what, uint32_t d0, uint32_t d1, uint32_t d2,
                        uint32_t d3) {
    if (usb_command_once(what, d0, d1, d2, d3)) {
        usb_xhci_command_state = USB_XHCI_COMMANDS_OK;
        return true;
    }
    usb_xhci_command_failed(what);
    if (usb_xhci_command_retried) {
        return false;
    }
    usb_xhci_command_retried = true;
    serial_trace("WARNING",
                 "USB xHCI command ring silent: resetting the controller and retrying");
    if (usb_xhci_recover() && usb_command_once(what, d0, d1, d2, d3)) {
        usb_xhci_command_state = USB_XHCI_COMMANDS_OK;
        usb_xhci_command_code = 0u;
        return true;
    }
    usb_xhci_command_failed(what);
    return false;
}

static bool usb_control_transfer(UsbHidDevice *device, uint8_t request_type,
                                 uint8_t request, uint16_t value, uint16_t index,
                                 uint16_t length, uint8_t *buffer) {
    bool in = (request_type & 0x80u) != 0;
    bool no_data = length == 0;
    volatile uint32_t *setup = usb_ring_reserve(&device->ep0);
    volatile uint32_t *status;
    uint8_t packet[8];
    uint32_t setup_low;
    uint32_t setup_high;

    packet[0] = request_type;
    packet[1] = request;
    packet[2] = (uint8_t)(value & 0xFFu);
    packet[3] = (uint8_t)(value >> 8);
    packet[4] = (uint8_t)(index & 0xFFu);
    packet[5] = (uint8_t)(index >> 8);
    packet[6] = (uint8_t)(length & 0xFFu);
    packet[7] = (uint8_t)(length >> 8);
    setup_low = (uint32_t)packet[0] | ((uint32_t)packet[1] << 8) |
                ((uint32_t)packet[2] << 16) | ((uint32_t)packet[3] << 24);
    setup_high = (uint32_t)packet[4] | ((uint32_t)packet[5] << 8) |
                 ((uint32_t)packet[6] << 16) | ((uint32_t)packet[7] << 24);

    setup[0] = setup_low;
    setup[1] = setup_high;
    setup[2] = 8u;
    /* The chain bit is what makes setup, data and status one transfer
     * descriptor; without it the controller runs each TRB on its own and
     * the transfer never completes. */
    /* Bits 16 and up are reserved in a Setup TRB: the length the controller
     * is asked for is dword 2 (the 8 in setup[2], which is also what the
     * controller validates), and the direction of the stage is settled by the
     * packet's own request type.  Nothing is copied here from the data stage's
     * layout, whose direction flag lives at bit 16 of its own dword. */
    setup[3] = (TRB_SETUP << 10) | TRB_IDT | TRB_CHAIN | device->ep0.cycle;
    usb_ring_commit(&device->ep0);

    if (!no_data) {
        usb_ring_push(&device->ep0, (uint32_t)(uintptr_t)buffer, 0,
                      (uint32_t)length,
                      ((in ? 1u : 0u) << 16) | (TRB_DATA << 10) | TRB_CHAIN);
    }
    /* The status stage always runs the other way round from the data stage. */
    status = usb_ring_reserve(&device->ep0);
    status[0] = 0;
    status[1] = 0;
    status[2] = 0;
    status[3] = (TRB_STATUS << 10) | TRB_IOC | device->ep0.cycle |
                ((uint32_t)(in && !no_data ? 0u : 1u) << 16);
    usb_ring_commit(&device->ep0);

    usb_ring_doorbell(device->slot, 1u);                  /* endpoint 0 */
    if (usb_wait_transfer(status, 4000000u) == NULL) {
        serial_trace_uint_value("WARNING", "USB control transfer timed out, request",
                                request);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Enumeration                                                            */
/* ---------------------------------------------------------------------- */

static uint32_t usb_ep0_mps_for_speed(uint32_t speed) {
    if (speed == USB_SPEED_HIGH) {
        return 64u;
    }
    if (speed == USB_SPEED_SUPER) {
        return 512u;
    }
    return 8u;                                            /* full and low speed */
}

/* Declared here and defined with the controller bring-up at the bottom of the
 * file: which protocol a port speaks is a property of the controller, and the
 * port reset needs it long before the controller itself is brought up. */
static uint32_t usb_port_protocol(uint32_t port);

/* Reset a port and wait until the device on it is actually enabled.  A port
 * that has just been reset by the controller (or that is still debouncing a
 * freshly attached device) reports connect status before it reports enable
 * status, so both have to be waited for - the speed field is only meaningful
 * once the port is out of its polling state.
 *
 * The port's protocol decides how both of those read.  On a USB3 port the link
 * state carries what the enable bit carries on USB2, and what the device is is
 * settled by the protocol rather than by the speed field.  On a USB2 port the
 * speed field is the answer, and a zero there means the controller did not say
 * - never a usable speed, because it becomes an 8 byte endpoint-0 packet size
 * and an undefined speed in the slot context, after which nothing on the port
 * answers at all.  Whatever happens is recorded for the boot menu to report. */
static bool usb_port_reset(uint32_t port, uint32_t *speed_out) {
    volatile uint32_t *portsc = usb_xhci.op + XHCI_OP_PORTSC(port) / 4u;
    uint32_t protocol = usb_port_protocol(port);
    uint32_t value = usb_read32(portsc);
    uint32_t spins;

    usb_last_port = port;
    usb_last_port_protocol = protocol;
    usb_last_port_status = value;
    usb_last_port_speed = 0u;

    if ((value & XHCI_PORTSC_CCS) == 0) {
        usb_last_port_reason = "no connect";
        return false;
    }
    usb_write32(portsc, XHCI_PORTSC_PP | XHCI_PORTSC_CHANGES | XHCI_PORTSC_PR);
    if (!usb_wait_bits(portsc, XHCI_PORTSC_PR, 0u, 40000000u, 48u)) {
        usb_last_port_status = usb_read32(portsc);
        usb_last_port_reason = "reset did not finish";
        serial_trace_uint_value("WARNING", "USB port reset timed out, port", port);
        return false;
    }
    usb_write32(portsc, XHCI_PORTSC_PP | XHCI_PORTSC_CHANGES);
    {
        uint32_t enable_deadline = timer_ticks + 48u;

        for (spins = 0u; ; ++spins) {
            value = usb_read32(portsc);
            if ((value & XHCI_PORTSC_PED) != 0u) {
                break;
            }
            /* A USB3 port whose link has come up is usable even if the enable bit
             * has not been published yet on this controller. */
            if (protocol == 3u &&
                (value & XHCI_PORTSC_PLS_MASK) == XHCI_PORTSC_PLS_U0 &&
                (value & XHCI_PORTSC_CCS) != 0u) {
                break;
            }
            if (spins >= 40000000u ||
                ((spins & 0xFFFFu) == 0u &&
                 (int32_t)(timer_ticks - enable_deadline) >= 0)) {
                usb_last_port_status = value;
                usb_last_port_reason = "never enabled";
                serial_trace_uint_value("WARNING", "USB port never enabled, port", port);
                return false;
            }
        }
    }
    /*
     * No link-state write here, deliberately.
     *
     * A USB 2.0 port that reads U3 looks suspended, and the specification
     * does provide a link-state write to bring it back to U0 - but a port
     * that has just been enabled is also entitled to pass through U3 on its
     * way to U0, and a write that lands in that window asks a port that is
     * mid-reset to change state instead.  That shows up as a port left
     * re-polling a device it had already enabled, which on a machine whose
     * keyboard hangs off a hub costs the whole subtree a second enumeration.
     * The port state is recorded above for the boot menu instead: a port that
     * stays suspended long after its enable is a machine to look at with that
     * number in hand, and the value is worth more than a guess made blind.
     */
    if (protocol == 3u) {
        *speed_out = USB_SPEED_SUPER;
    } else {
        *speed_out = (value & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_POS;
        if (*speed_out == 0u) {
            serial_trace_hex_value("WARNING", "USB port speed undefined, PORTSC",
                                   value);
            *speed_out = USB_SPEED_FULL;
        }
    }
    usb_last_port_speed = *speed_out;
    return true;
}

static bool usb_address_device(UsbHidDevice *device) {
    uint32_t *in_ctx = (uint32_t *)(void *)usb_in_ctx[usb_xhci_cur];
    /* The context buffer belongs to the device, not to its slot number: slot
     * numbers restart at 1 on every controller, and a hub and a keyboard can
     * be told apart only by which one the record points at. */
    volatile uint32_t *device_ctx = device->ctx;
    uint32_t stride = usb_xhci.ctx_dwords;
    volatile uint32_t *slot_ctx;
    volatile uint32_t *ep0_ctx;
    uint32_t i;

    for (i = 0; i < USB_CTX_BYTES / 4u; ++i) {
        in_ctx[i] = 0;
    }
    in_ctx[1] = (1u << 0) | (1u << 1);            /* add slot + endpoint 0 */

    slot_ctx = in_ctx + stride;
    /* The route string names the port path for a device behind a hub, and is
     * zero for one on a root port - which is why the port number also has to
     * be in the context for both cases.  Context Entries is 1: this command
     * installs the slot context and endpoint 0 and nothing else. */
    slot_ctx[0] = (device->path & 0xFFFFFu) |
                  ((uint32_t)device->speed << 20) | (1u << 27);
    slot_ctx[1] = (uint32_t)device->root_port << 16;
    /* Split transactions for a full or low speed device behind a hi-speed
     * hub: the controller is told which hub slot and which of its ports the
     * transfers go through. */
    slot_ctx[2] = (uint32_t)device->tt_slot | ((uint32_t)device->tt_port << 8);

    ep0_ctx = slot_ctx + stride;
    ep0_ctx[1] = (3u << 1) | (USB_EP_TYPE_CONTROL << 3) |
                 ((uint32_t)device->ep0_mps << 16);
    ep0_ctx[2] = ((uint32_t)(uintptr_t)device->ep0.base) | 1u;   /* DCS = 1 */
    ep0_ctx[3] = 0;
    /* Average TRB Length sits in bits 15:0 of this dword (8 for a
     * control endpoint); Max ESIT Payload is bits 31:16 and stays zero
     * here.  Writing the eight shifted left put it in the payload field
     * and left the average at zero, which is a malformed endpoint
     * context on silicon that validates it - QEMU does not, which is how
     * it worked in every virtual machine and produced a TRB Error on a
     * real one. */
    ep0_ctx[4] = 8u;

    usb_dcbaa[usb_xhci_cur][device->slot] = (uint64_t)(uintptr_t)device_ctx;
    if (!usb_command("USB address device failed", (uint32_t)(uintptr_t)in_ctx, 0,
                     0, (TRB_ADDRESS_DEVICE << 10) |
                            ((uint32_t)device->slot << 24))) {
        return false;
    }
    return true;
}

static bool usb_evaluate_context(UsbHidDevice *device) {
    uint32_t *in_ctx = (uint32_t *)(void *)usb_in_ctx[usb_xhci_cur];
    /* The context buffer belongs to the device, not to its slot number: slot
     * numbers restart at 1 on every controller, and a hub and a keyboard can
     * be told apart only by which one the record points at. */
    volatile uint32_t *device_ctx = device->ctx;
    uint32_t stride = usb_xhci.ctx_dwords;
    volatile uint32_t *slot_ctx;
    volatile uint32_t *ep0_ctx;
    uint32_t i;

    for (i = 0; i < USB_CTX_BYTES / 4u; ++i) {
        in_ctx[i] = 0;
    }
    in_ctx[1] = (1u << 0) | (1u << 1);

    slot_ctx = in_ctx + stride;
    for (i = 0; i < stride; ++i) {
        slot_ctx[i] = device_ctx[i];
    }
    slot_ctx[0] = (slot_ctx[0] & ~(0x1Fu << 27)) | (1u << 27);
    if (device->is_hub) {
        /*
         * A hub says so in its slot context, together with how many ports it
         * has and whether it carries one translator per port.  The Hub flag
         * and the port count are what the controller uses to schedule the
         * hub's ports, and the think time is how long it must allow the
         * translator to answer - all three are known only once the hub
         * descriptor has been read, which is why this command runs again
         * after the hub is identified rather than at address time.
         */
        slot_ctx[0] |= (1u << 26) | (device->multi_tt ? (1u << 25) : 0u);
        slot_ctx[1] = (slot_ctx[1] & 0x0000FFFFu) |
                      ((uint32_t)device->root_port << 16) |
                      ((uint32_t)device->ports << 24);
        slot_ctx[2] = (slot_ctx[2] & ~(3u << 16)) |
                      ((uint32_t)device->tt_think << 16);
    }

    ep0_ctx = slot_ctx + stride;
    for (i = 0; i < stride; ++i) {
        ep0_ctx[i] = device_ctx[stride + i];
    }
    ep0_ctx[1] = (ep0_ctx[1] & ~(0xFFFFu << 16)) |
                 ((uint32_t)device->ep0_mps << 16);

    return usb_command("USB evaluate context failed", (uint32_t)(uintptr_t)in_ctx, 0,
                       0, (TRB_EVALUATE_CONTEXT << 10) | ((uint32_t)device->slot << 24));
}

static bool usb_configure_endpoint(UsbHidDevice *device, uint8_t ep_number,
                                   uint16_t mps, uint8_t interval) {
    uint32_t *in_ctx = (uint32_t *)(void *)usb_in_ctx[usb_xhci_cur];
    /* The context buffer belongs to the device, not to its slot number: slot
     * numbers restart at 1 on every controller, and a hub and a keyboard can
     * be told apart only by which one the record points at. */
    volatile uint32_t *device_ctx = device->ctx;
    uint32_t stride = usb_xhci.ctx_dwords;
    volatile uint32_t *slot_ctx;
    volatile uint32_t *ep_ctx;
    uint32_t dci = usb_endpoint_dci(ep_number, true);
    /* The add flag and the context are addressed by the same number: bit `dci`
     * of the input control context adds the endpoint, and the context itself
     * sits `dci` entries past the slot context (entry 0 there is the input
     * control context, entry 1 the slot context, entry 2 endpoint 0). */
    uint32_t index = dci;
    uint32_t exponent = usb_interval_exponent(device->speed, interval);
    uint32_t i;

    serial_trace_uint_value("INFO", "  HID endpoint mps", mps);
    serial_trace_uint_value("INFO", "  HID endpoint dci", dci);
    serial_trace_uint_value("INFO", "  HID endpoint interval", exponent);

    for (i = 0; i < USB_CTX_BYTES / 4u; ++i) {
        in_ctx[i] = 0;
    }
    /* The slot context plus this one endpoint.  The Add flags are indexed by
     * DCI, and endpoint 0 is never re-added here. */
    in_ctx[1] = (1u << 0) | (1u << index);

    slot_ctx = in_ctx + stride;
    for (i = 0; i < stride; ++i) {
        slot_ctx[i] = device_ctx[i];
    }
    /* Context Entries is the highest valid DCI. */
    slot_ctx[0] = (slot_ctx[0] & ~(0x1Fu << 27)) | (dci << 27);

    ep_ctx = slot_ctx + stride;                     /* endpoint 0, kept as is */
    for (i = 0; i < stride; ++i) {
        ep_ctx[i] = device_ctx[stride + i];
    }

    ep_ctx = slot_ctx + index * stride;
    for (i = 0; i < stride; ++i) {
        ep_ctx[i] = 0;
    }
    ep_ctx[0] = exponent << 16;
    ep_ctx[1] = (3u << 1) | (USB_EP_TYPE_INT_IN << 3) | ((uint32_t)mps << 16);
    ep_ctx[2] = ((uint32_t)(uintptr_t)device->ep.base) | 1u;
    ep_ctx[3] = 0;
    /* Periodic endpoints schedule on the Max ESIT Payload field. */
    ep_ctx[4] = (uint32_t)mps | ((uint32_t)mps << 16);

    if (!usb_command("USB configure endpoint failed",
                     (uint32_t)(uintptr_t)in_ctx, 0, 0,
                     (TRB_CONFIGURE_EP << 10) | ((uint32_t)device->slot << 24))) {
        return false;
    }
    device->ep_number = ep_number;
    device->ep_dci = (uint8_t)dci;
    return true;
}

static bool usb_parse_configuration(const uint8_t *config, uint32_t length,
                                    uint8_t *ep_number, uint16_t *ep_mps,
                                    uint8_t *interval, uint8_t *kind,
                                    uint8_t *interface_number) {
    uint32_t offset = 0;
    bool in_hid_interface = false;
    uint8_t candidate_kind = 0;
    uint8_t candidate_interface = 0;

    while (offset + 2u <= length) {
        uint8_t descriptor_length = config[offset];
        uint8_t descriptor_type = config[offset + 1u];

        if (descriptor_length < 2u || offset + descriptor_length > length) {
            break;
        }
        if (descriptor_type == 4u && descriptor_length >= 9u) {       /* interface */
            uint8_t interface_class = config[offset + 5u];
            uint8_t interface_subclass = config[offset + 6u];
            uint8_t interface_protocol = config[offset + 7u];

            candidate_interface = config[offset + 2u];
            in_hid_interface = interface_class == USB_CLASS_HID &&
                               interface_subclass <= USB_HID_BOOT_SUBCLASS;
            candidate_kind = 0;
            if (in_hid_interface) {
                if (interface_protocol == USB_HID_PROTO_KBD) {
                    candidate_kind = USB_HID_KIND_KEYBOARD;
                } else if (interface_protocol == USB_HID_PROTO_MOUSE) {
                    candidate_kind = USB_HID_KIND_MOUSE;
                } else {
                    in_hid_interface = false;
                }
            }
        } else if (descriptor_type == 5u && descriptor_length >= 7u &&
                   in_hid_interface) {
            uint8_t address = config[offset + 2u];
            uint8_t attributes = config[offset + 3u];

            if ((attributes & 0x03u) == 3u && (address & 0x80u) != 0) {
                *ep_number = (uint8_t)(address & 0x0Fu);
                *ep_mps = (uint16_t)(config[offset + 4u] |
                                     ((uint16_t)config[offset + 5u] << 8));
                *interval = config[offset + 6u];
                *kind = candidate_kind;
                *interface_number = candidate_interface;
                return true;
            }
        }
        offset += descriptor_length;
    }
    return false;
}

/*
 * Every bus implements a control transfer; the descriptor walk below is then
 * written once against that signature instead of once per controller family.
 */
typedef bool (*UsbControlTransferFn)(void *context, uint8_t request_type,
                                     uint8_t request, uint16_t value,
                                     uint16_t index, uint16_t length,
                                     uint8_t *buffer);

/*
 * The part of enumeration that is identical on every controller: read the
 * device descriptor, read the configuration, and work out whether this is a
 * boot keyboard or mouse and which endpoint carries its reports.  Addressing
 * the device and configuring the endpoint once it is known stay with the
 * driver that owns the controller, because that is where they differ.
 */
static bool usb_hid_identify(void *context, UsbControlTransferFn transfer,
                             uint8_t *device_mps, uint8_t *ep_number,
                             uint16_t *ep_mps, uint8_t *interval, uint8_t *kind,
                             uint8_t *interface_number) {
    uint8_t descriptor[18];
    uint16_t config_length;

    /* Where a device that never becomes input got stuck is recorded here as
     * well as in the trace: the boot menu shows it, so a machine whose
     * keyboard is silent can be diagnosed from a photograph. */
    usb_last_stage = USB_STAGE_DESCRIPTOR;
    if (!transfer(context, 0x80u, USB_REQ_GET_DESCRIPTOR,
                  (uint16_t)(USB_DESC_DEVICE << 8), 0u, 18u, descriptor)) {
        return false;
    }
    ++usb_devices_seen;
    usb_last_device_class = descriptor[4];
    serial_trace_hex_value("INFO", "  USB device id (vendor:product)",
                           ((uint32_t)descriptor[8] | ((uint32_t)descriptor[9] << 8)) << 16 |
                           ((uint32_t)descriptor[10] | ((uint32_t)descriptor[11] << 8)));
    *device_mps = descriptor[7];
    if (!transfer(context, 0x80u, USB_REQ_GET_DESCRIPTOR,
                  (uint16_t)(USB_DESC_CONFIG << 8), 0u, 9u, usb_desc)) {
        return false;
    }
    config_length = (uint16_t)(usb_desc[2] | ((uint16_t)usb_desc[3] << 8));
    if (config_length < 9u || config_length > (uint16_t)sizeof(usb_desc)) {
        config_length = (uint16_t)sizeof(usb_desc);
    }
    if (!transfer(context, 0x80u, USB_REQ_GET_DESCRIPTOR,
                  (uint16_t)(USB_DESC_CONFIG << 8), 0u, config_length, usb_desc)) {
        return false;
    }
    usb_last_stage = USB_STAGE_HID;
    if (!usb_parse_configuration(usb_desc, config_length, ep_number, ep_mps,
                                 interval, kind, interface_number)) {
        return false;                    /* answered, but no boot HID here */
    }
    usb_last_stage = USB_STAGE_NONE;
    return true;
}

static void usb_enumerate_port(uint32_t port);
static void usb_hub_scan(UsbHidDevice *hub);
static bool usb_hub_setup(UsbHidDevice *hub, const uint8_t *device_descriptor);

/*
 * Look for a device on every port of the current controller.  Called at boot
 * and again from the frame loop: firmware, a hot-plugged keyboard, and a port
 * that was still settling when it was first read all land here, and a port
 * whose enumeration failed is retried later rather than being given up on.
 */
static void usb_xhci_scan_ports(void) {
    uint32_t port;
    uint32_t connected = 0u;

    for (port = 1u; port <= usb_xhci.max_ports; ++port) {
        uint32_t status = usb_read32(usb_xhci.op + XHCI_OP_PORTSC(port) / 4u);
        uint32_t slot = port < USB_PORT_SLOTS ? port : USB_PORT_SLOTS - 1u;
        uint32_t i;
        bool known = false;

        if ((status & XHCI_PORTSC_CCS) == 0u) {
            usb_port_next_try[usb_xhci_cur][slot] = 0u;
            continue;
        }
        ++connected;
        /* A device behind a hub does not claim the root port: the hub does,
         * and its own ports are walked from the hub's record instead. */
        for (i = 0; i < usb_dev_next; ++i) {
            if (usb_hid[i].in_use && usb_hid[i].depth == 0u &&
                usb_hid[i].controller == (uint8_t)usb_xhci_cur &&
                usb_hid[i].root_port == (uint8_t)port) {
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        if (usb_dev_next >= USB_DEVICE_MAX) {
            usb_last_port = port;
            usb_last_port_status = status;
            usb_last_port_protocol = usb_port_protocol(port);
            usb_last_port_speed =
                (status & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_POS;
            usb_last_port_reason = "table full";
            continue;
        }
        if ((int32_t)(timer_ticks - usb_port_next_try[usb_xhci_cur][slot]) < 0) {
            /* A port that failed before, resting between attempts: keep the
             * boot menu's port line pointing at the port that is connected
             * rather than at the last port that already gave up. */
            usb_last_port = port;
            usb_last_port_status = status;
            usb_last_port_protocol = usb_port_protocol(port);
            usb_last_port_speed =
                (status & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_POS;
            usb_last_port_reason = "retry wait";
            continue;                        /* failed recently: wait longer */
        }
        serial_trace_uint_value("INFO", "USB scan port", port);
        serial_trace_hex_value("INFO", "  PORTSC", status);
        {
            uint32_t before = usb_dev_next;

            usb_enumerate_port(port);
            if (usb_dev_next == before) {
                /* It answered but could not be configured; leave it alone for
                 * a while so a stubborn device cannot stall the frame loop. */
                usb_port_next_try[usb_xhci_cur][slot] =
                    timer_ticks + USB_PORT_RETRY_TICKS;
            }
        }
    }
    if (usb_ports_connected == 0u && connected != 0u) {
        usb_ports_connected = connected;
    }
}

/*
 * What a device is attached to.
 *
 * Everything the controller has to be told about where a device sits lives
 * here, so one bring-up path serves a device on a root port and one several
 * hubs deep: a device on a root port names its port in root_port and has
 * route 0, a device behind a hub carries the port path in route, and a full
 * or low speed device behind a hi-speed hub names the hub slot and port that
 * its split transactions go through.  child_shift is where the port number of
 * one of this device's own downstream ports goes in a child's route string.
 *
 * The root port is never part of the route string: it has a field of its own
 * in the slot context, and the route string starts at the first port below
 * it.  Putting the root port in the route string as well asks the controller
 * for a port of that number on the hub, which is a device it cannot find.
 */
typedef struct {
    uint8_t root_port;
    uint8_t speed;
    uint8_t depth;
    uint8_t tt_slot;
    uint8_t tt_port;
    uint32_t route;
    uint32_t child_shift;
} UsbAttach;

/*
 * Bring up whatever answers at one point of the tree and report what it turned
 * out to be: a keyboard, a mouse, a hub, or something this driver does not
 * drive (0).
 *
 * The device is addressed before its class is known, because the class lives
 * in a descriptor that can only be read once the controller will talk to the
 * device at all.  A hub is then told apart, set up as a hub, and left in the
 * table so its own ports can be walked - which is what makes a keyboard on
 * the far side of a hub reachable, and a hub enumerating perfectly while
 * providing no input the thing that looks most like "the keyboard is not
 * detected".
 */
/*
 * Leave a device that answered in the table with no endpoint.
 *
 * Its port then counts as taken, so the controller is not asked to address it
 * again on every pass: a second Address Device for the same port is an error
 * and a controller never returns an address, so retrying a device that has
 * already been addressed burns a slot each time until the controller has none
 * left.  The record also keeps what the device said it was, which is what the
 * boot menu reports.
 */
static uint8_t usb_device_claim(UsbHidDevice *device) {
    device->in_use = true;
    device->kind = 0u;
    usb_addr_attempts[device->index] = 0u;
    ++usb_dev_next;
    return 0u;
}

static uint8_t usb_device_start(const UsbAttach *attach) {
    UsbHidDevice *device;
    uint8_t device_descriptor[18];
    uint8_t ep_number = 0;
    uint16_t ep_mps = 0;
    uint8_t interval = 0;
    uint8_t kind = 0;
    uint8_t interface_number = 0;
    uint16_t config_length;
    uint32_t slot;
    uint32_t index = usb_dev_next;
    uint32_t i;

    if (usb_dev_next >= USB_DEVICE_MAX) {
        return 0u;
    }
    if (!usb_command("USB enable slot failed", 0, 0, 0, (TRB_ENABLE_SLOT << 10))) {
        if (usb_xhci_command_state == USB_XHCI_COMMANDS_SILENT) {
            usb_last_port_reason = "xHCI command timeout";
        } else if (usb_xhci_command_code == 9u) {
            usb_last_port_reason = "no slots";
        } else {
            usb_last_port_reason = "enable slot failed";
        }
        return 0u;
    }
    slot = usb_cmd_slot;
    serial_trace_uint_value("INFO", "  slot assigned", slot);
    if (slot == 0 || slot > USB_XHCI_MAX_SLOTS) {
        return 0u;
    }

    device = &usb_hid[index];
    for (i = 0; i < (uint32_t)sizeof(*device); ++i) {
        ((uint8_t *)device)[i] = 0;
    }
    device->in_use = true;
    device->controller = (uint8_t)usb_xhci_cur;
    device->index = index;
    device->ctx = usb_dev_ctx[index];
    device->slot = (uint8_t)slot;
    device->root_port = attach->root_port;
    device->port = attach->root_port;
    device->speed = attach->speed;
    device->depth = attach->depth;
    device->path = attach->route;
    device->child_shift = attach->child_shift;
    device->tt_slot = attach->tt_slot;
    device->tt_port = attach->tt_port;
    device->ep0_mps = (uint16_t)usb_ep0_mps_for_speed(attach->speed);
    device->report = usb_report[index];
    usb_ring_init(&device->ep0, usb_trb_ring[index * 2u], USB_XFER_TRBS);
    usb_ring_init(&device->ep, usb_trb_ring[index * 2u + 1u], USB_XFER_TRBS);

    usb_last_stage = USB_STAGE_ADDRESS;
    if (!usb_address_device(device)) {
        if (++usb_addr_attempts[index] >= 3u) {
            usb_last_port_reason = "address failed";
            return usb_device_claim(device);
        }
        /* The slot the Enable Slot command reserved is still enabled in
         * the controller, and its DCBAA entry still names this record's
         * context - which is about to be handed to the next device that
         * lands on this index.  The slot is never rung, but leave the
         * array clean anyway: two slots naming one context is exactly the
         * kind of state a real controller is free to check. */
        usb_dcbaa[usb_xhci_cur][slot] = 0u;
        device->in_use = false;              /* the record is used again */
        return 0u;
    }

    /* Device descriptor, first at the speed's default packet size. */
    usb_last_stage = USB_STAGE_DESCRIPTOR;
    if (!usb_control_transfer(device, 0x80u, USB_REQ_GET_DESCRIPTOR,
                              (uint16_t)(USB_DESC_DEVICE << 8), 0, 18,
                              device_descriptor)) {
        usb_last_port_reason = "descriptors failed";
        serial_trace_uint_value("WARNING", "USB device descriptor failed, port",
                                attach->root_port);
        return usb_device_claim(device);
    }
    ++usb_devices_seen;
    usb_last_device_class = device_descriptor[4];
    serial_trace_hex_value("INFO", "  USB device id (vendor:product)",
                           ((uint32_t)device_descriptor[8] |
                            ((uint32_t)device_descriptor[9] << 8)) << 16 |
                           ((uint32_t)device_descriptor[10] |
                            ((uint32_t)device_descriptor[11] << 8)));
    if ((attach->speed == USB_SPEED_FULL || attach->speed == USB_SPEED_LOW) &&
        device_descriptor[7] != 0 && device_descriptor[7] != device->ep0_mps) {
        device->ep0_mps = device_descriptor[7];
        usb_evaluate_context(device);
    }

    usb_last_stage = USB_STAGE_CONFIG;
    if (!usb_control_transfer(device, 0x00u, USB_REQ_SET_CONFIG, 1u, 0, 0, NULL)) {
        usb_last_port_reason = "set config failed";
        return usb_device_claim(device);
    }

    /* A hub is a device whose whole purpose is other devices: it is set up
     * here and its ports are walked by the caller. */
    if (device_descriptor[4] == USB_CLASS_HUB) {
        if (!usb_hub_setup(device, device_descriptor)) {
            usb_last_port_reason = "hub setup failed";
            return usb_device_claim(device);
        }
        ++usb_dev_next;
        ++usb_hubs_total;
        serial_trace_uint_value("INFO", "  USB hub ports", device->ports);
        return USB_HID_KIND_HUB;
    }

    /* Configuration: the header first, then the whole thing. */
    if (!usb_control_transfer(device, 0x80u, USB_REQ_GET_DESCRIPTOR,
                              (uint16_t)(USB_DESC_CONFIG << 8), 0, 9, usb_desc)) {
        usb_last_port_reason = "config descriptor failed";
        serial_trace_uint_value("WARNING", "USB config header failed, port",
                                attach->root_port);
        return usb_device_claim(device);
    }
    config_length = (uint16_t)(usb_desc[2] | ((uint16_t)usb_desc[3] << 8));
    if (config_length < 9u || config_length > (uint16_t)sizeof(usb_desc)) {
        config_length = (uint16_t)sizeof(usb_desc);
    }
    if (!usb_control_transfer(device, 0x80u, USB_REQ_GET_DESCRIPTOR,
                              (uint16_t)(USB_DESC_CONFIG << 8), 0, config_length,
                              usb_desc)) {
        usb_last_port_reason = "config descriptor failed";
        return usb_device_claim(device);
    }
    if (!usb_parse_configuration(usb_desc, config_length, &ep_number, &ep_mps,
                                 &interval, &kind, &interface_number)) {
        usb_last_stage = USB_STAGE_HID;
        usb_last_port_reason = "not a boot HID";
        serial_trace_uint_value("WARNING", "USB device is not a boot HID, port",
                                attach->root_port);
        return usb_device_claim(device);
    }

    /* Boot protocol fixes the report layout: eight bytes for a keyboard,
     * three for a mouse, whatever the report descriptor would have said. */
    usb_control_transfer(device, 0x21u, USB_REQ_SET_PROTOCOL, 0u, interface_number, 0,
                         NULL);

    device->ep_number = ep_number;
    device->kind = kind;
    device->transfer_bytes = kind == USB_HID_KIND_KEYBOARD ? 8u : 3u;
    if (!usb_configure_endpoint(device, ep_number, ep_mps, interval)) {
        usb_last_port_reason = "endpoint failed";
        return usb_device_claim(device);
    }

    usb_post_interrupt_transfer(device);
    usb_last_stage = USB_STAGE_NONE;
    ++usb_dev_next;
    ++usb_hid_count;
    if (kind == USB_HID_KIND_KEYBOARD) {
        ++usb_hid_keyboards;
        ++usb_keyboards_total;
        serial_trace("INFO", "USB keyboard ready");
    } else {
        ++usb_hid_mice;
        ++usb_mice_total;
        serial_trace("INFO", "USB mouse ready");
    }
    serial_trace_uint_value("INFO", "  USB device port", attach->root_port);
    serial_trace_uint_value("INFO", "  USB device slot", slot);
    serial_trace_uint_value("INFO", "  USB device speed", attach->speed);
    return kind;
}

/*
 * Hub class requests.
 *
 * A downstream port is neither the device nor one of its endpoints, so the
 * request type says "other recipient" and the port number goes in wIndex, with
 * the feature in wValue.
 */
static bool usb_hub_port_feature(UsbHidDevice *hub, bool set, uint8_t port,
                                 uint8_t feature) {
    return usb_control_transfer(hub, USB_HUB_CTRL_OUT,
                                (uint8_t)(set ? USB_REQ_SET_FEATURE
                                              : USB_REQ_CLEAR_FEATURE),
                                feature, (uint16_t)port, 0u, NULL);
}

static bool usb_hub_port_status(UsbHidDevice *hub, uint8_t port, uint32_t *status) {
    uint8_t data[4];

    if (!usb_control_transfer(hub, USB_HUB_CTRL_IN, USB_REQ_GET_STATUS, 0u,
                              (uint16_t)port, 4u, data)) {
        return false;
    }
    *status = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
              ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    return true;
}

/*
 * Reset one downstream port and report the speed of what is on it.
 *
 * A hub port has no PORTSC of its own, so the reset is a class request and the
 * completion is the port's reset-change bit rather than an enable bit - a hub
 * sets both, and the change bit is the one that says the reset finished.  The
 * speed is not a register either: a USB 2.0 hub reports whether the device it
 * just enabled is low speed or high speed, and anything else on it is full
 * speed.  A SuperSpeed hub's ports are SuperSpeed by definition.
 *
 * Port power is requested first and a refusal is not treated as a failure: a
 * hub whose ports are hard-wired on rejects the request, and that is a hub
 * whose ports are powered anyway.
 */
static bool usb_hub_port_reset(UsbHidDevice *hub, uint8_t port, uint8_t *speed_out) {
    uint32_t status = 0u;
    uint32_t spins;

    usb_hub_port_feature(hub, true, port, USB_HUB_PORT_POWER);
    usb_wait_ticks(TIMER_HZ / 8u + 1u);
    if (!usb_hub_port_feature(hub, true, port, USB_HUB_PORT_RESET)) {
        serial_trace_uint_value("WARNING", "USB hub reset refused, port", port);
        return false;
    }
    for (spins = 0u; spins < 50u; ++spins) {
        if (!usb_hub_port_status(hub, port, &status)) {
            return false;
        }
        if ((status & (USB_HUB_ST_C_RESET | USB_HUB_ST_ENABLE)) != 0u) {
            break;
        }
        usb_wait_ticks(TIMER_HZ / 50u + 1u);
    }
    if ((status & (USB_HUB_ST_C_RESET | USB_HUB_ST_ENABLE)) == 0u) {
        serial_trace_uint_value("WARNING", "USB hub port never enabled, port", port);
        return false;
    }
    /* The change bits are write-one-to-clear, and leaving them set would make
     * the next look at the port report a second, imaginary reset. */
    usb_hub_port_feature(hub, false, port, USB_HUB_C_RESET);
    usb_hub_port_feature(hub, false, port, USB_HUB_C_CONNECTION);

    if (hub->speed == USB_SPEED_SUPER) {
        *speed_out = USB_SPEED_SUPER;
    } else if ((status & USB_HUB_ST_HIGH) != 0u) {
        *speed_out = USB_SPEED_HIGH;
    } else if ((status & USB_HUB_ST_LOW) != 0u) {
        *speed_out = USB_SPEED_LOW;
    } else {
        *speed_out = USB_SPEED_FULL;
    }
    return true;
}

/*
 * The first interrupt IN endpoint of a configuration, whatever its class.
 *
 * A hub's status endpoint is the one endpoint descriptor in the whole driver
 * that is not a HID report endpoint, and it is worth configuring: a hub that
 * is only "addressed" is a hub the controller will not treat as a configured
 * device, and its downstream ports are then not usable.
 */
static bool usb_first_interrupt_in(const uint8_t *config, uint32_t length,
                                   uint8_t *ep_number, uint16_t *ep_mps,
                                   uint8_t *interval) {
    uint32_t offset = 0u;

    while (offset + 2u <= length) {
        uint8_t descriptor_length = config[offset];
        uint8_t descriptor_type = config[offset + 1u];

        if (descriptor_length < 2u || offset + descriptor_length > length) {
            break;
        }
        if (descriptor_type == 5u && descriptor_length >= 7u) {
            uint8_t address = config[offset + 2u];
            uint8_t attributes = config[offset + 3u];

            if ((attributes & 0x03u) == 3u && (address & 0x80u) != 0u) {
                *ep_number = (uint8_t)(address & 0x0Fu);
                *ep_mps = (uint16_t)(config[offset + 4u] |
                                     ((uint16_t)config[offset + 5u] << 8));
                *interval = config[offset + 6u];
                return true;
            }
        }
        offset += descriptor_length;
    }
    return false;
}

/*
 * Finish bringing up a hub: how many ports it has, how long its translator
 * may take to answer, and that this slot is a hub at all.
 *
 * The Hub flag, the port count and the think time all come from the hub
 * descriptor, so they can only be written after it has been read - which is
 * why this runs a second slot-context command instead of being part of the
 * address command, where the device is still an unknown.
 */
static bool usb_hub_setup(UsbHidDevice *hub, const uint8_t *device_descriptor) {
    uint8_t descriptor[8];
    uint8_t ep_number = 0u;
    uint16_t ep_mps = 0u;
    uint8_t interval = 0u;
    uint16_t config_length = 0u;
    bool have_endpoint = false;
    uint32_t characteristics;

    /* bDeviceProtocol is 1 for a single translator, 2 when the hub carries
     * one per port. */
    hub->multi_tt = device_descriptor[6] == 2u;
    if (!usb_control_transfer(hub, USB_HUB_CTRL_DESC, USB_REQ_GET_DESCRIPTOR,
                              (uint16_t)(USB_DESC_HUB << 8), 0u, 8u, descriptor)) {
        serial_trace("WARNING", "USB hub descriptor failed");
        return false;
    }
    if (descriptor[2] == 0u) {
        serial_trace("WARNING", "USB hub reports no ports");
        return false;
    }
    hub->ports = descriptor[2] > USB_HUB_PORTS_MAX ? (uint8_t)USB_HUB_PORTS_MAX
                                                   : descriptor[2];
    /* wHubCharacteristics bits 6:5 hold the translator's think time in units
     * of eight full speed bit times. */
    characteristics = (uint32_t)descriptor[3] | ((uint32_t)descriptor[4] << 8);
    hub->tt_think = (uint8_t)((characteristics >> 5) & 3u);

    /* Find the status endpoint before the slot context is written, because
     * configuring it is the command that puts the slot in the Configured
     * state - the state a hub has to reach before anything behind it can be
     * addressed. */
    if (usb_control_transfer(hub, 0x80u, USB_REQ_GET_DESCRIPTOR,
                             (uint16_t)(USB_DESC_CONFIG << 8), 0u, 9u, usb_desc)) {
        config_length = (uint16_t)(usb_desc[2] | ((uint16_t)usb_desc[3] << 8));
        if (config_length < 9u || config_length > (uint16_t)sizeof(usb_desc)) {
            config_length = (uint16_t)sizeof(usb_desc);
        }
        have_endpoint = usb_control_transfer(hub, 0x80u, USB_REQ_GET_DESCRIPTOR,
                                            (uint16_t)(USB_DESC_CONFIG << 8), 0u,
                                            config_length, usb_desc) &&
                        usb_first_interrupt_in(usb_desc, config_length, &ep_number,
                                               &ep_mps, &interval);
    }

    hub->is_hub = true;
    if (!usb_evaluate_context(hub)) {
        hub->is_hub = false;
        return false;
    }
    if (have_endpoint) {
        /* A failure here leaves the hub usable for its ports but not
         * configured, which is how it was before; it is not fatal. */
        usb_configure_endpoint(hub, ep_number, ep_mps, interval);
    }
    hub->next_scan = timer_ticks;
    usb_last_stage = USB_STAGE_NONE;
    return true;
}

/* Whether a hub port already has a device in the table.  The route string
 * names the position in the tree, so a child is identified by it. */
static bool usb_hub_child_known(const UsbHidDevice *hub, uint8_t port) {
    uint32_t route = hub->path | ((uint32_t)port << hub->child_shift);
    uint32_t i;

    for (i = 0; i < usb_dev_next; ++i) {
        if (usb_hid[i].in_use && usb_hid[i].path == route &&
            usb_hid[i].depth == (uint8_t)(hub->depth + 1u)) {
            return true;
        }
    }
    return false;
}

/*
 * A device on a root port: reset the port, then hand it to the shared bring-up.
 * A hub that comes back is walked immediately, so a keyboard behind it is
 * already input by the time the boot menu draws.
 */
static void usb_enumerate_port(uint32_t port) {
    UsbAttach attach;
    uint32_t speed = 0u;

    usb_last_stage = USB_STAGE_PORT;
    serial_trace_uint_value("INFO", "USB device on port", port);
    if (!usb_port_reset(port, &speed)) {
        serial_trace_uint_value("WARNING", "USB port reset gave up, port", port);
        return;
    }
    serial_trace_uint_value("INFO", "  port speed", speed);
    serial_trace_uint_value("INFO", "  port protocol", usb_last_port_protocol);

    attach.root_port = (uint8_t)port;
    attach.speed = (uint8_t)speed;
    attach.depth = 0u;
    attach.tt_slot = 0u;
    attach.tt_port = 0u;
    attach.route = 0u;
    /* A device on a root port has no route string of its own, and the ports of
     * a hub hanging here are the first tier of a child's route: the port number
     * of one of them goes in its lowest nibble. */
    attach.child_shift = 0u;

    if (usb_device_start(&attach) == USB_HID_KIND_HUB) {
        usb_hub_scan(&usb_hid[usb_dev_next - 1u]);
    }
}

/*
 * Walk a hub's ports.
 *
 * Called when the hub is found and again from the frame loop, so a device
 * plugged into it later is found too.  A port whose device could not be brought
 * up is left alone for a while rather than retried every frame, and a hub found
 * on one of the ports is walked in turn - which is what makes a keyboard
 * behind a hub (and a hub behind that) reachable.
 */
static void usb_hub_scan(UsbHidDevice *hub) {
    uint8_t port;

    if (!hub->in_use || !hub->is_hub || hub->depth >= USB_DEPTH_MAX) {
        return;
    }
    if ((int32_t)(timer_ticks - hub->next_scan) < 0) {
        return;
    }
    hub->next_scan = timer_ticks + TIMER_HZ / 2u;
    for (port = 1u; port <= hub->ports; ++port) {
        UsbAttach attach;
        uint32_t status = 0u;
        uint8_t child_speed = 0u;
        uint8_t kind;

        if (usb_dev_next >= USB_DEVICE_MAX) {
            return;
        }
        if (usb_hub_child_known(hub, port)) {
            continue;
        }
        if (!usb_hub_port_status(hub, port, &status)) {
            return;
        }
        if ((status & USB_HUB_ST_CONNECT) == 0u) {
            continue;
        }
        serial_trace_uint_value("INFO", "USB hub child on port", port);
        usb_last_stage = USB_STAGE_PORT;
        if (!usb_hub_port_reset(hub, port, &child_speed)) {
            hub->next_scan = timer_ticks + USB_PORT_RETRY_TICKS;
            continue;
        }

        attach.root_port = hub->root_port;
        attach.speed = child_speed;
        attach.depth = (uint8_t)(hub->depth + 1u);
        attach.route = hub->path | ((uint32_t)port << hub->child_shift);
        attach.child_shift = hub->child_shift + 4u;
        /* A full or low speed device behind a hi-speed hub is carried by that
         * hub's transaction translator, and the controller has to be told
         * which hub and which of its ports to use. */
        if (hub->speed == USB_SPEED_HIGH &&
            (child_speed == USB_SPEED_FULL || child_speed == USB_SPEED_LOW)) {
            attach.tt_slot = hub->slot;
            attach.tt_port = port;
        } else {
            attach.tt_slot = 0u;
            attach.tt_port = 0u;
        }

        kind = usb_device_start(&attach);
        if (kind == 0u) {
            hub->next_scan = timer_ticks + USB_PORT_RETRY_TICKS;
            continue;
        }
        if (kind == USB_HID_KIND_HUB) {
            usb_hub_scan(&usb_hid[usb_dev_next - 1u]);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Bringing a register window firmware put above 4 GB back into reach     */
/* ---------------------------------------------------------------------- */

/*
 * The kernel runs with paging off, so it can only touch the first 4 GB - and
 * firmware that supports above-4G decoding likes to place the xHCI register
 * window above that line.  Such a machine then has no input at all, which is
 * exactly why the same hardware can work when booted through BIOS (whose USB
 * legacy emulation keeps feeding the 8042) and look frozen when booted
 * through UEFI.
 *
 * A BAR is relocatable by design, so instead of abandoning the controller the
 * register window is moved into a free slot of the low MMIO hole.  What is
 * free is decided from three sources: the firmware memory map (memory is
 * never MMIO), the fixed chipset windows that are not part of the PCI
 * resource tree, and every other device's own BAR - read with the standard
 * "write all ones and see what sticks" probe, which restores everything it
 * touches.
 *
 * A candidate is only accepted if the xHCI capability block reads back
 * through it, so a slot that some chipset mapping answers in front of us
 * (PCIe ECAM, most often) is discarded and the next one tried instead of
 * being mistaken for the controller.
 */

#define USB_MMIO_CEILING   0xFFF00000u   /* stay clear of the firmware flash */
#define USB_MMIO_CLAIMS    192u
#define USB_MMIO_TRIES     256u         /* windows actually probed */

typedef struct {
    uint32_t base;
    uint32_t size;
} UsbMmioClaim;

/* The PCIe enhanced-configuration window is chipset MMIO rather than a device
 * BAR, so the scan asks ACPI for its address; defined with the ACPI code. */
static bool acpi_mcfg_window(uint32_t *base_out, uint32_t *size_out);

static bool usb_claim_add(UsbMmioClaim *claims, uint32_t *count,
                          uint32_t base, uint32_t size) {
    if (size == 0u || base >= USB_MMIO_CEILING) {
        return true;                     /* nothing in reach to remember */
    }
    if (*count >= USB_MMIO_CLAIMS) {
        return false;                    /* out of room: caller must give up */
    }
    claims[*count].base = base;
    claims[*count].size = size;
    ++*count;
    return true;
}

/* Step past everything the candidate runs into, so a large claim (a
 * framebuffer apron, the chipset config window) costs one jump instead of one
 * step per megabyte spent inside it. */
static uint32_t usb_claim_skip(const UsbMmioClaim *claims, uint32_t count,
                              uint32_t base, uint32_t size) {
    for (;;) {
        uint32_t moved = base;
        uint32_t i;

        for (i = 0; i < count; ++i) {
            uint32_t end = claims[i].base + claims[i].size;
            uint32_t aligned;

            if (end < claims[i].base) {
                end = 0xFFFFFFFFu;
            }
            if (!(base < end && claims[i].base < base + size)) {
                continue;
            }
            aligned = (end + size - 1u) & ~(size - 1u);
            if (aligned > moved) {
                moved = aligned;
            }
        }
        if (moved == base) {
            return base;
        }
        base = moved;
    }
}

/*
 * Everything the firmware memory map describes is out of bounds, and the top
 * of it is where the low MMIO hole begins - the one address the search must
 * not start below.
 *
 * The type that needs care is 2 (reserved), because it covers two very
 * different things: firmware MMIO above memory, and reservations carved out
 * of memory itself (ACPI NVS, the boot loader's own buffers).  Size separates
 * them reliably enough: anything reserved *below* the top of memory is
 * memory.  The map is read twice for this - once to find that top, once to
 * claim - which is cheaper than sorting it.
 */
static uint32_t usb_claim_firmware_map(UsbMmioClaim *claims, uint32_t *count) {
    uintptr_t cursor;
    uintptr_t end;
    uint32_t top = 0x00100000u;

    if (crash_mmap_addr == 0 || crash_mmap_length == 0) {
        return top;
    }
    cursor = crash_mmap_addr;
    end = crash_mmap_addr + crash_mmap_length;
    while (cursor + sizeof(uint32_t) <= end) {
        const MultibootMmapEntry *entry = (const MultibootMmapEntry *)cursor;
        uint64_t limit;

        if (cursor + entry->size + sizeof(uint32_t) > end) {
            break;
        }
        cursor += entry->size + sizeof(uint32_t);
        if (entry->type == 2u) {
            continue;
        }
        limit = entry->base_addr + entry->length;
        if (limit > 0x100000000ull) {
            limit = 0x100000000ull;
        }
        if (entry->base_addr < limit && limit > (uint64_t)top) {
            top = (uint32_t)limit;
        }
    }

    cursor = crash_mmap_addr;
    while (cursor + sizeof(uint32_t) <= end) {
        const MultibootMmapEntry *entry = (const MultibootMmapEntry *)cursor;
        uint64_t limit;

        if (cursor + entry->size + sizeof(uint32_t) > end) {
            break;
        }
        cursor += entry->size + sizeof(uint32_t);
        limit = entry->base_addr + entry->length;
        if (limit > 0x100000000ull) {
            limit = 0x100000000ull;
        }
        if (entry->base_addr >= limit) {
            continue;
        }
        if (entry->type == 2u) {
            if (entry->base_addr >= (uint64_t)top) {
                continue;                /* MMIO above memory, not memory */
            }
            if (limit > (uint64_t)top) {
                limit = (uint64_t)top;   /* only the part inside memory */
            }
        }
        (void)usb_claim_add(claims, count, (uint32_t)entry->base_addr,
                            (uint32_t)(limit - entry->base_addr));
    }
    return top;
}

static void usb_claim_chipset(UsbMmioClaim *claims, uint32_t *count) {
    static const uint32_t fixed[][2] = {
        {0x000A0000u, 0x00060000u},      /* legacy VGA window + option ROMs */
        {0x000F0000u, 0x00010000u},      /* BIOS shadow */
        {0xFEC00000u, 0x00100000u},      /* I/O APIC */
        {0xFED00000u, 0x00100000u},      /* HPET */
        {0xFEE00000u, 0x00100000u},      /* local APIC */
        {0xFFC00000u, 0x00400000u}       /* firmware flash at the top */
    };
    uint32_t mcfg_base = 0u;
    uint32_t mcfg_size = 0u;
    uint32_t i;

    for (i = 0; i < (uint32_t)(sizeof(fixed) / sizeof(fixed[0])); ++i) {
        (void)usb_claim_add(claims, count, fixed[i][0], fixed[i][1]);
    }
    if (acpi_mcfg_window(&mcfg_base, &mcfg_size)) {
        serial_trace_hex_value("INFO", "USB relocation: PCIe config window", mcfg_base);
        (void)usb_claim_add(claims, count, mcfg_base, mcfg_size);
    }
}

/* One pass over the PCI bus reserving the memory window of every other
 * device.  The probe leaves each BAR exactly as it was found. */
static bool usb_claim_pci(UsbMmioClaim *claims, uint32_t *count,
                          uint8_t skip_bus, uint8_t skip_slot,
                          uint8_t skip_function) {
    uint16_t bus;

    for (bus = 0; bus < 256u; ++bus) {
        uint8_t slot;

        for (slot = 0; slot < 32u; ++slot) {
            uint8_t function;

            for (function = 0; function < 8u; ++function) {
                uint32_t id = pci_config_read32((uint8_t)bus, slot, function, 0x00);
                uint32_t class_reg;
                uint8_t index;

                if (id == 0xFFFFFFFFu) {
                    if (function == 0u) {
                        break;
                    }
                    continue;
                }
                if (bus == skip_bus && slot == skip_slot && function == skip_function) {
                    continue;
                }
                class_reg = pci_config_read32((uint8_t)bus, slot, function, 0x08);
                if (((class_reg >> 24) & 0xFFu) == 0x03u) {
                    /* A display adapter's window is far larger than a safe
                     * probe can capture, so its BAR is left alone and a
                     * generous apron reserved instead. */
                    uint32_t bar = pci_config_read32((uint8_t)bus, slot, function, 0x10);
                    if ((bar & 0x1u) == 0u &&
                        !usb_claim_add(claims, count, bar & ~0x0Fu, 0x10000000u)) {
                        return false;
                    }
                    continue;
                }
                for (index = 0; index < 6u; ++index) {
                    uint8_t offset = (uint8_t)(0x10u + 4u * index);
                    uint32_t value = pci_config_read32((uint8_t)bus, slot, function, offset);
                    uint32_t upper = 0u;
                    uint32_t mask;
                    uint32_t size;

                    if ((value & 0x1u) != 0u) {
                        continue;            /* I/O BAR: never MMIO */
                    }
                    if ((value & 0x4u) != 0u) {
                        upper = pci_config_read32((uint8_t)bus, slot, function,
                                                  (uint8_t)(offset + 4u));
                        ++index;             /* 64-bit BAR: takes two slots */
                    }
                    pci_config_write32((uint8_t)bus, slot, function, offset, 0xFFFFFFFFu);
                    mask = pci_config_read32((uint8_t)bus, slot, function, offset);
                    pci_config_write32((uint8_t)bus, slot, function, offset, value);
                    if ((mask & 0xFFFFFFF0u) == 0u) {
                        continue;            /* no address bits: no device here */
                    }
                    if (upper != 0u) {
                        continue;            /* already above 4 GB: not in our way */
                    }
                    size = (~(mask & 0xFFFFFFF0u)) + 1u;
                    if (size < 0x1000u || size > 0x80000000u) {
                        size = 0x1000u;      /* unreadable: guard the base only */
                    }
                    if (!usb_claim_add(claims, count, value & ~0x0Fu, size)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

/* Does the capability block at this address describe an xHCI controller?  A
 * chipset mapping that answers in front of the real device fails here: the
 * version, the port and slot counts, the runtime offsets and the page size
 * all have to be sane. */
static bool usb_window_answers_as_xhci(uint32_t base) {
    volatile uint32_t *cap = (volatile uint32_t *)(uintptr_t)base;
    uint32_t caplength = usb_read32(cap) & 0xFFu;
    uint32_t version = (usb_read32(cap) >> 16) & 0xFFFFu;
    uint32_t hcsparams1;

    if (caplength < 0x20u || caplength > 0x80u) {
        return false;
    }
    if (version < 0x0090u || version > 0x0200u) {
        return false;
    }
    hcsparams1 = usb_read32(cap + XHCI_CAP_HCSPARAMS1 / 4u);
    if ((hcsparams1 & 0xFFu) == 0u || ((hcsparams1 >> 24) & 0xFFu) == 0u) {
        return false;
    }
    if (usb_read32(cap + XHCI_CAP_RTSOFF / 4u) >= 0x100000u ||
        usb_read32(cap + XHCI_CAP_DBOFF / 4u) >= 0x100000u) {
        return false;
    }
    if (usb_read32((volatile uint32_t *)((uint8_t *)cap +
                                         caplength + XHCI_OP_PAGESIZE)) == 0u) {
        return false;
    }
    return true;
}

static bool usb_controller_relocate(uint8_t bus, uint8_t slot, uint8_t function,
                                    uint32_t *out_base) {
    const uint8_t offset = 0x10u;
    const uint8_t upper = 0x14u;
    uint32_t save0 = pci_config_read32(bus, slot, function, offset);
    uint32_t save1 = pci_config_read32(bus, slot, function, upper);
    uint32_t save_command = pci_config_read32(bus, slot, function, 0x04);
    uint32_t mask0;
    uint32_t mask1;
    uint32_t size;
    uint32_t candidate;
    uint32_t tries;
    uint32_t count = 0u;
    uint32_t top;
    UsbMmioClaim claims[USB_MMIO_CLAIMS];

    /* Probe with decoding off: while the all-ones value is in place the low
     * dword spells an address the device would otherwise claim. */
    pci_config_write32(bus, slot, function, 0x04, save_command & ~0x00000002u);
    pci_config_write32(bus, slot, function, offset, 0xFFFFFFFFu);
    pci_config_write32(bus, slot, function, upper, 0xFFFFFFFFu);
    mask0 = pci_config_read32(bus, slot, function, offset);
    mask1 = pci_config_read32(bus, slot, function, upper);
    pci_config_write32(bus, slot, function, offset, save0);
    pci_config_write32(bus, slot, function, upper, save1);
    pci_config_write32(bus, slot, function, 0x04, save_command);

    serial_trace_hex_value("INFO", "USB relocation: BAR mask low", mask0);
    serial_trace_hex_value("INFO", "USB relocation: BAR mask high", mask1);
    /* The size lives in the low dword: the bits it decodes read back as zero.
     * (The high dword of a 64-bit BAR is address only, so it reads back as
     * the ones written and says nothing about the size.) */
    size = (~(mask0 & 0xFFFFFFF0u)) + 1u;
    if (size == 0u) {
        serial_trace("WARNING", "USB relocation: window fills the 32-bit space");
        return false;
    }
    if (size < 0x1000u) {
        size = 0x1000u;
    }
    if (size > 0x08000000u) {
        serial_trace("WARNING", "USB relocation: implausible window size");
        return false;
    }
    serial_trace_hex_value("INFO", "USB relocation: window bytes", size);

    usb_claim_chipset(claims, &count);
    top = usb_claim_firmware_map(claims, &count);
    serial_trace_uint_value("INFO", "USB relocation: claimed so far", count);
    if (count >= USB_MMIO_CLAIMS ||
        !usb_claim_pci(claims, &count, bus, slot, function)) {
        serial_trace("WARNING", "USB relocation: too many claimed regions to be sure");
        return false;
    }
    serial_trace_uint_value("INFO", "USB relocation: claimed regions", count);

    candidate = (top + size - 1u) & ~(size - 1u);
    serial_trace_hex_value("INFO", "USB relocation: search start", candidate);
    for (tries = 0u; tries < USB_MMIO_TRIES; ) {
        uint32_t command = save_command & ~0x00000002u;

        /* Claimed regions cost nothing: step past the whole region at once,
         * so the count below only limits how many windows are really probed. */
        candidate = usb_claim_skip(claims, count, candidate, size);
        if (candidate + size > USB_MMIO_CEILING) {
            break;
        }
        ++tries;
        serial_trace_hex_value("INFO", "USB relocation: probe", candidate);
        pci_config_write32(bus, slot, function, 0x04, command);
        pci_config_write32(bus, slot, function, offset, candidate | 0x00000004u);
        pci_config_write32(bus, slot, function, upper, 0u);
        pci_config_write32(bus, slot, function, 0x04, command | 0x00000006u);
        if (pci_config_read32(bus, slot, function, offset) ==
                (candidate | 0x00000004u) &&
            usb_window_answers_as_xhci(candidate)) {
            serial_trace_hex_value("INFO", "USB controller moved to", candidate);
            *out_base = candidate;
            return true;
        }
        /* Somebody else answers there: put every register back and look on. */
        pci_config_write32(bus, slot, function, 0x04, command);
        pci_config_write32(bus, slot, function, offset, save0);
        pci_config_write32(bus, slot, function, upper, save1);
        pci_config_write32(bus, slot, function, 0x04, save_command);
        candidate += size;
    }
    serial_trace("WARNING", "USB relocation: no free window below 4 GB");
    return false;
}

/* ---------------------------------------------------------------------- */
/* Controller bring-up                                                    */
/* ---------------------------------------------------------------------- */

/*
 * One PCI walk shared by every USB driver here.  The cursor is the caller's,
 * so a driver that wants all of its controllers walks the bus tree once
 * instead of rescanning it per controller.
 */
typedef struct {
    uint16_t bus;
    uint8_t slot;
    uint8_t function;
} UsbPciCursor;

static void usb_pci_cursor_reset(UsbPciCursor *cursor) {
    cursor->bus = 0;
    cursor->slot = 0;
    cursor->function = 0;
}

/* Next function with this USB program interface, or false at the end of the
 * bus tree.  A missing function 0 ends its slot, as usual. */
static bool usb_pci_find(UsbPciCursor *cursor, uint8_t progif, uint8_t *bus_out,
                         uint8_t *slot_out, uint8_t *function_out) {
    for (; cursor->bus < 256u; ++cursor->bus) {
        for (; cursor->slot < 32u; ++cursor->slot) {
            for (; cursor->function < 8u; ++cursor->function) {
                uint8_t bus = (uint8_t)cursor->bus;
                uint32_t id = pci_config_read32(bus, cursor->slot, cursor->function, 0x00);
                uint32_t class_reg;

                if (id == 0xFFFFFFFFu) {
                    if (cursor->function == 0u) {
                        break;               /* empty slot: no functions at all */
                    }
                    continue;
                }
                class_reg = pci_config_read32(bus, cursor->slot, cursor->function, 0x08);
                if (((class_reg >> 24) & 0xFFu) != USB_PCI_CLASS_SERIAL ||
                    ((class_reg >> 16) & 0xFFu) != USB_PCI_SUBCLASS_USB ||
                    ((class_reg >> 8) & 0xFFu) != progif) {
                    continue;
                }
                *bus_out = bus;
                *slot_out = cursor->slot;
                *function_out = cursor->function;
                ++cursor->function;          /* resume after this one */
                return true;
            }
            cursor->function = 0u;
        }
        cursor->slot = 0u;
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* xHCI: taking the controller over from the firmware                    */
/* ---------------------------------------------------------------------- */

/*
 * USB Legacy Support (extended capability 1): the firmware can keep the
 * controller's ports under its own SMM driver, and until it lets go the
 * ports it holds answer as if nothing were attached.  Taking over means
 * setting the OS owned semaphore and waiting for the BIOS owned one to
 * clear - the same handoff a BIOS boot does through its own interface, and
 * one of the reasons a machine can have a working keyboard under one
 * firmware and none under the other.
 *
 * The capability is three dwords, and all of them matter:
 *
 *   +0  the header (capability ID and next pointer) - it is NOT the
 *       semaphore register, and writing ownership bits into it tells the
 *       firmware nothing while reading bit 16 of it back always reads zero:
 *       the previous code did exactly that and so never took ownership at
 *       all, on any machine, while the boot menu reported a clean handoff.
 *   +4  USBLEGSUP: bit 16 BIOS Owned Semaphore, bit 24 OS Owned Semaphore.
 *   +8  USBLEGCTLSTS: the SMI generation enables.  While the firmware owns
 *       the controller every one of those traps can fire an SMI - a port
 *       reset, a controller halt, even a register write from this driver -
 *       and its handler is then free to re-initialize the controller under
 *       the running driver: the ring goes silent with a healthy status
 *       register, which is the exact shape of a controller that answers
 *       its self-test and then never executes another command.
 *
 * The order below is therefore: OS owned semaphore first (so the handoff
 * SMI can fire and the firmware can let go by clearing its own bit), a
 * bounded wait for the firmware to do so, and only then - if it would
 * not - the SMI enables are zeroed and the BIOS owned bit is cleared by
 * force, because a machine whose SMM handler refuses still has ports and
 * a keyboard behind them and the alternative is no input at all.
 */
#define XHCI_LEGACY_BIOS_OWNED 0x00010000u
#define XHCI_LEGACY_OS_OWNED   0x01000000u

static void usb_xhci_take_legacy(volatile uint32_t *cap, uint32_t hccparams1) {
    uint32_t offset = (hccparams1 >> 16) & 0xFFFFu;
    uint32_t guard;

    for (guard = 0u; guard < 32u && offset != 0u; ++guard) {
        volatile uint32_t *entry = (volatile uint32_t *)
            ((uint8_t *)cap + (offset << 2));
        uint32_t header = usb_read32(entry);

        if ((header & 0xFFu) == 1u) {
            volatile uint32_t *semaphores = entry + 1u;   /* USBLEGSUP: +4 */
            volatile uint32_t *smi_control = entry + 2u;  /* USBLEGCTLSTS: +8 */
            uint32_t value = usb_read32(semaphores);

            serial_trace_hex_value("INFO", "USB xHCI legacy ownership", value);
            if ((value & XHCI_LEGACY_BIOS_OWNED) != 0u) {
                usb_write32(semaphores, value | XHCI_LEGACY_OS_OWNED);
                if (usb_wait_bits(semaphores, XHCI_LEGACY_BIOS_OWNED, 0u,
                                  4000000u, 64u)) {
                    /* Ownership moved.  Disarm every SMI trap NOW: measured
                     * on an AMI/C SM board, the firmware's handler keeps the
                     * port-reset and controller-reset traps armed after the
                     * semaphore handshake, and an SMI fired by this
                     * driver's own port writes re-initializes the controller
                     * under the running driver - the command ring goes
                     * silent with a healthy status register, and the boot
                     * menu reports a controller that answered its self-test
                     * and never answered anything again. */
                    usb_write32(smi_control, 0u);
                    serial_trace("INFO", "USB xHCI: firmware handed the ports over");
                    return;
                }
                /* The firmware would not let go in a second: take the
                 * controller over anyway.  SMI generation off first, so
                 * nothing its handler does can interrupt the bring-up that
                 * follows, then the BIOS owned bit cleared by hand. */
                usb_write32(smi_control, 0u);
                value = usb_read32(semaphores);
                usb_write32(semaphores,
                            (value & ~XHCI_LEGACY_BIOS_OWNED) |
                            XHCI_LEGACY_OS_OWNED);
                usb_xhci_legacy_kept = true;
                serial_trace("WARNING",
                             "USB xHCI: firmware kept ownership, taken by force");
                return;
            }
            /* The firmware never owned the ports, but SMI traps can still be
             * armed (a wake-on-USB handler, for instance).  Disarm them:
             * they serve nobody now, and an SMI fired by this driver's own
             * register writes is how a running controller dies. */
            usb_write32(smi_control, 0u);
            return;
        }
        offset = (header >> 8) & 0xFFu;
    }
}

/*
 * Read the controller's own port map.
 *
 * The extended capability list starts at the offset in HCCPARAMS1 (bits 31:16,
 * counted in dwords) and is a chain: each entry names the next one in bits 15:8.
 * Capability ID 2 is "Supported Protocol", whose major revision is 0x02 for
 * USB2 and 0x03 for USB3, and whose third dword gives the first port number the
 * entry covers (bits 7:0) and how many ports it covers (bits 15:8).
 *
 * This is the only place the two kinds of port can be told apart, and they do
 * have to be told apart: the same register bits mean different things on each,
 * so a USB3 port read as a USB2 one is how a connected, enabled SuperSpeed port
 * ends up with an undefined speed in its slot context.
 */
static void usb_xhci_protocols(volatile uint32_t *cap, uint32_t hccparams1) {
    uint32_t offset = (hccparams1 >> 16) & 0xFFFFu;
    uint32_t guard;

    usb_xhci.proto_entries = 0u;
    for (guard = 0u; guard < 32u && offset != 0u; ++guard) {
        volatile uint32_t *entry = (volatile uint32_t *)
            ((uint8_t *)cap + (offset << 2));
        uint32_t header = usb_read32(entry);

        if ((header & 0xFFu) == XHCI_XECP_SUPPORTED_PROTOCOL) {
            uint32_t major = (header >> 24) & 0xFFu;
            uint32_t layout = usb_read32(entry + 2u);
            uint32_t first = layout & 0xFFu;
            uint32_t ports = (layout >> 8) & 0xFFu;

            serial_trace_hex_value("INFO", "USB xHCI protocol capability", header);
            serial_trace_uint_value("INFO", "  major revision", major);
            serial_trace_uint_value("INFO", "  first port", first);
            serial_trace_uint_value("INFO", "  port count", ports);
            if ((major == 2u || major == 3u) && ports != 0u &&
                usb_xhci.proto_entries < USB_XHCI_PROTO_MAX) {
                uint32_t slot = usb_xhci.proto_entries++;

                usb_xhci.proto_rev[slot] = (uint8_t)major;
                usb_xhci.proto_first[slot] = (uint8_t)first;
                usb_xhci.proto_ports[slot] = (uint8_t)ports;
            }
        }
        offset = (header >> 8) & 0xFFu;
    }
}

/* Which protocol a port speaks, or 0 when the controller did not say.  A port
 * outside every range the controller published is reported as unknown rather
 * than assumed to be USB2 - the two are not interchangeable, and a boot menu
 * that guessed would name the wrong one on exactly the machine where it
 * matters.  Unknown is then read the conservative way (the speed field is
 * taken at its word), not as USB3. */
static uint32_t usb_port_protocol(uint32_t port) {
    uint32_t i;

    for (i = 0u; i < usb_xhci.proto_entries; ++i) {
        uint32_t first = usb_xhci.proto_first[i];

        if (port >= first && port < first + usb_xhci.proto_ports[i]) {
            return usb_xhci.proto_rev[i];
        }
    }
    return 0u;
}

/* Ports whose power is controllable are powered after a controller reset:
 * until the port is powered a device on it reads as "nothing attached". */
static void usb_xhci_power_ports(void) {
    uint32_t port;

    for (port = 1u; port <= usb_xhci.max_ports; ++port) {
        volatile uint32_t *portsc = usb_xhci.op + XHCI_OP_PORTSC(port) / 4u;
        uint32_t value = usb_read32(portsc);

        if ((value & XHCI_PORTSC_PP) == 0u) {
            usb_write32(portsc, (value & XHCI_PORTSC_WRITABLE) | XHCI_PORTSC_PP);
        }
    }
}

/* Resolve the register window of one controller and put the controller state
 * in usb_xhci for the current slot. */
static bool usb_xhci_attach(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t bar0 = pci_config_read32(bus, slot, function, 0x10);
    uint32_t bar1 = pci_config_read32(bus, slot, function, 0x14);
    uint32_t command;

    if ((bar0 & 0x1u) != 0u) {
        return false;                        /* I/O mapped, not ours */
    }
    serial_trace_hex_value("INFO", "USB xHCI BAR0", bar0);
    serial_trace_hex_value("INFO", "USB xHCI BAR1", bar1);
    command = pci_config_read32(bus, slot, function, 0x04);
    pci_config_write32(bus, slot, function, 0x04,
                       command | 0x00000006u);   /* mem space + mastering */
    if (bar1 != 0u) {
        /* Firmware put the registers above 4 GB, which nothing in this
         * kernel can address; bring the window down instead of giving up. */
        uint32_t moved = 0u;

        if (!usb_controller_relocate(bus, slot, function, &moved)) {
            serial_trace("WARNING",
                         "USB controller above 4G and could not be moved");
            return false;
        }
        usb_xhci.cap = (volatile uint32_t *)(uintptr_t)moved;
    } else {
        usb_xhci.cap = (volatile uint32_t *)(uintptr_t)(bar0 & ~0x0Fu);
    }
    usb_xhci.bus = bus;
    usb_xhci.slot = slot;
    usb_xhci.function = function;
    return true;
}

/*
 * One attempt at bringing the controller up: halt it, reset it, program the
 * device context array, the command ring, the event ring and the interrupter,
 * then let it run.  Everything the controller reads by DMA is written before
 * the run bit, because a running controller is entitled to fetch from a
 * register the moment it is written.
 */
static bool usb_controller_bring_up(void) {
    volatile uint32_t *cap = usb_xhci.cap;
    uint32_t caplength = usb_read32(cap + XHCI_CAP_CAPLENGTH / 4u) & 0xFFu;
    uint32_t hcsparams1 = usb_read32(cap + XHCI_CAP_HCSPARAMS1 / 4u);
    uint32_t hccparams1 = usb_read32(cap + XHCI_CAP_HCCPARAMS1 / 4u);
    uint32_t i;

    usb_xhci.op = (volatile uint32_t *)((uint8_t *)cap + caplength);
    usb_xhci.rt = (volatile uint32_t *)((uint8_t *)cap +
                                        usb_read32(cap + XHCI_CAP_RTSOFF / 4u));
    usb_xhci.db = (volatile uint32_t *)((uint8_t *)cap +
                                        usb_read32(cap + XHCI_CAP_DBOFF / 4u));
    usb_xhci.max_ports = (hcsparams1 >> 24) & 0xFFu;
    usb_xhci.max_slots = hcsparams1 & 0xFFu;
    usb_xhci.ctx_dwords = (hccparams1 & (1u << 2)) != 0 ? 16u : 8u;
    if (usb_xhci.max_ports == 0 || usb_xhci.max_slots == 0) {
        return false;
    }
    if (usb_xhci.max_slots > USB_XHCI_MAX_SLOTS) {
        usb_xhci.max_slots = USB_XHCI_MAX_SLOTS;
    }
    if (usb_xhci_page_size_from_reg(
            usb_read32(usb_xhci.op + XHCI_OP_PAGESIZE / 4u)) == 0u) {
        serial_trace("WARNING", "USB xHCI: unsupported PAGESIZE encoding");
        return false;
    }
    usb_xhci_take_legacy(cap, hccparams1);
    usb_xhci_protocols(cap, hccparams1);

    /*
     * Bus mastering and memory space, stated again here rather than once when
     * the BAR was read.  Between those two points this driver writes an
     * all-ones value over the BAR to size it, and a firmware that reposts a
     * device's config space (or an SMI handler that does it for it) can leave
     * the command register back at the value the platform came up with - with
     * bus mastering off, the controller is still a working set of registers
     * that never touches memory, so every port reads correctly and not one
     * command is ever fetched or event ever written.  That is the exact shape
     * of "the ports work and nothing else does".
     */
    {
        uint32_t command = pci_config_read32(usb_xhci.bus, usb_xhci.slot,
                                            usb_xhci.function, 0x04);

        if ((command & 0x00000006u) != 0x00000006u) {
            pci_config_write32(usb_xhci.bus, usb_xhci.slot, usb_xhci.function,
                               0x04, command | 0x00000006u);
        }
    }

    /* Halt, reset, wait for the controller to publish itself ready. */
    usb_write32(usb_xhci.op + XHCI_OP_USBCMD / 4u, 0u);
    if (!usb_wait_bits(usb_xhci.op + XHCI_OP_USBSTS / 4u, XHCI_USBSTS_HCH,
                       XHCI_USBSTS_HCH, 40000000u, 32u)) {
        serial_trace("WARNING", "USB controller would not halt");
        return false;
    }
    usb_write32(usb_xhci.op + XHCI_OP_USBCMD / 4u, XHCI_USBCMD_HCRST);
    if (!usb_wait_bits(usb_xhci.op + XHCI_OP_USBCMD / 4u, XHCI_USBCMD_HCRST, 0u,
                       40000000u, 32u)) {
        serial_trace("WARNING", "USB controller reset never cleared");
        return false;
    }
    if (!usb_wait_bits(usb_xhci.op + XHCI_OP_USBSTS / 4u, XHCI_USBSTS_CNR, 0u,
                       80000000u, 64u)) {
        serial_trace("WARNING", "USB controller never became ready");
        return false;
    }
    usb_write32(usb_xhci.op + XHCI_OP_USBSTS / 4u, XHCI_USBSTS_CLEAR);

    for (i = 0; i <= USB_XHCI_MAX_SLOTS; ++i) {
        usb_dcbaa[usb_xhci_cur][i] = 0;
    }
    /*
     * Scratchpad buffers are mandatory whenever HCSPARAMS2 advertises them.
     * Do not enter Run mode with a zero DCBAA[0]: that state is exactly what
     * the previous bounded pool implementation created on this machine
     * (the boot screen showed "scr 512/0").  Allocate a page-aligned slice from
     * the shared early-boot pool and fail controller bring-up if it cannot be
     * satisfied.
     */
    {
        uint32_t hcsparams2 = usb_read32(cap + XHCI_CAP_HCSPARAMS2 / 4u);
        /*
         * Max Scratchpad Buffers grew from five bits to ten in xHCI 1.1
         * (Hi stays at 31:27, Lo moved into 25:21).  A 0.95/1.0 controller
         * keeps 25:21 reserved - reading it as part of a ten bit count
         * turns the sixteen pages an Intel chipset asks for into five
         * hundred and twelve, which then silently burns two megabytes of
         * the shared pool per controller instead of sixty four kilobytes
         * and can exhaust it for the next controller on the same machine.
         * The version register says which of the two layouts this is.
         */
        uint32_t hciversion = (usb_read32(cap + XHCI_CAP_CAPLENGTH / 4u) >> 16)
                              & 0xFFFFu;
        uint32_t scratch = hciversion >= 0x0110u
            ? (((hcsparams2 >> 27) & 0x1Fu) << 5) |
              ((hcsparams2 >> 21) & 0x1Fu)
            : ((hcsparams2 >> 27) & 0x1Fu);
        uint32_t pagesize_reg =
            usb_read32(usb_xhci.op + XHCI_OP_PAGESIZE / 4u);
        uint32_t page_size = usb_xhci_page_size_from_reg(pagesize_reg);
        uint32_t bytes;
        uint32_t base;

        usb_scratch_count[usb_xhci_cur] = scratch;
        usb_xhci_scratch_asked = scratch;
        usb_xhci_scratch_granted = 0u;

        if (scratch > USB_XHCI_SCRATCH_MAX) {
            serial_trace_uint_value("WARNING",
                                    "USB xHCI: invalid scratchpad count",
                                    scratch);
            return false;
        }
        if (scratch != 0u) {
            serial_trace_uint_value("INFO", "USB xHCI scratchpad pages asked",
                                    scratch);
            if (usb_scratch_allocated[usb_xhci_cur]) {
                base = usb_scratch_base[usb_xhci_cur];
                if (usb_scratch_page_size[usb_xhci_cur] != page_size) {
                    serial_trace("WARNING",
                                 "USB xHCI: scratchpad page size changed after reset");
                    return false;
                }
            } else {
                uint32_t aligned = (usb_scratch_pool_used + page_size - 1u)
                                   & ~(page_size - 1u);

                if (scratch > 0u && scratch > (0xFFFFFFFFu / page_size)) {
                    return false;
                }
                bytes = scratch * page_size;
                if (aligned > USB_XHCI_SCRATCH_POOL_BYTES ||
                    bytes > USB_XHCI_SCRATCH_POOL_BYTES - aligned) {
                    serial_trace_uint_value("WARNING",
                                            "USB xHCI scratchpad pool exhausted, pages",
                                            scratch);
                    return false;
                }
                base = (uint32_t)(uintptr_t)&usb_scratch_pool[aligned];
                usb_scratch_base[usb_xhci_cur] = base;
                usb_scratch_page_size[usb_xhci_cur] = page_size;
                usb_scratch_allocated[usb_xhci_cur] = true;
                usb_scratch_pool_used = aligned + bytes;
            }

            for (i = 0u; i < scratch; ++i) {
                volatile uint8_t *page =
                    usb_scratch_pool + (base - (uint32_t)(uintptr_t)usb_scratch_pool)
                    + page_size * i;

                usb_zero_bytes(page, page_size);
                usb_scratch_array[usb_xhci_cur][i] =
                    (uint64_t)(uintptr_t)page;
            }
            usb_dcbaa[usb_xhci_cur][0] =
                (uint64_t)(uintptr_t)usb_scratch_array[usb_xhci_cur];
            usb_xhci_scratch_granted = scratch;
        } else {
            usb_scratch_allocated[usb_xhci_cur] = false;
            usb_scratch_page_size[usb_xhci_cur] = page_size;
            usb_dcbaa[usb_xhci_cur][0] = 0u;
            usb_xhci_scratch_granted = 0u;
        }
    }
    usb_ring_init(&usb_cmd[usb_xhci_cur], usb_cmd_ring[usb_xhci_cur], USB_CMD_TRBS);

    usb_write64(usb_xhci.op + XHCI_OP_DCBAAP / 4u,
                (uint32_t)(uintptr_t)usb_dcbaa[usb_xhci_cur]);
    /*
     * The command ring's cycle state goes in the low bit of this register, and
     * it is not decoration: the controller compares the cycle bit of the first
     * TRB it fetches against RCS, and the ring was initialised with cycle bit
     * 1 (see usb_ring_init).  Writing the bare address leaves the controller
     * waiting for a TRB whose cycle bit is 0, and because a queue that has
     * never been fetched from produces no event either, the symptom is not an
     * error but silence: the controller starts, ports report what is attached
     * to them, and no command is ever answered - no slot, no address, no
     * descriptors, no input, on every port.
     *
     * The same bit exists on the endpoint rings as the DCS bit of their
     * dequeue pointers, where it is part of a context and was not forgotten
     * (see usb_address_device).  This one is a register field instead, and an
     * emulated controller that ignores it boots correctly with or without it -
     * which is exactly how a driver can pass every test on a virtual machine
     * and fail on the first real one.
     */
    usb_write64(usb_xhci.op + XHCI_OP_CRCR / 4u,
                (uint32_t)(uintptr_t)usb_cmd_ring[usb_xhci_cur] | XHCI_CRCR_RCS);

    usb_erst[usb_xhci_cur][0] = (uint32_t)(uintptr_t)usb_ev_ring[usb_xhci_cur];
    usb_erst[usb_xhci_cur][1] = 0;
    usb_erst[usb_xhci_cur][2] = USB_EV_TRBS;
    usb_erst[usb_xhci_cur][3] = 0;
    /*
     * Count first, then the address, then the dequeue pointer - and this order
     * is not cosmetic.
     *
     * The controller latches the event ring segment table when the high half of
     * ERSTBA is written, and it takes the number of segments from ERSTSZ as it
     * stands at that moment.  An address published while the count still reads
     * zero describes a disabled interrupter, so the latched ring is empty: the
     * very next event the controller produces is found to lie outside its own
     * ring (er_start == 0, er_size == 0), which the controller answers by
     * setting Host Controller Error and giving up on the command ring as well.
     *
     * That failure is worse than a hang: the controller still resets ports and
     * still reports what is attached to them, so the driver sees a live
     * controller and a plugged-in device, and every command it sends afterwards
     * is never fetched and never answered.  It reads on screen as a keyboard
     * that enumerates as far as "port" and stops, on every port, on every
     * machine - the same symptom as the command ring never being rung at all,
     * which is why the order follows the one the Linux xHCI driver uses rather
     * than the order the register names invite.
     */
    usb_write32(usb_xhci.rt + XHCI_RT_ERSTSZ / 4u, 1u);
    usb_write64(usb_xhci.rt + XHCI_RT_ERSTBA / 4u,
                (uint32_t)(uintptr_t)usb_erst[usb_xhci_cur]);
    usb_interrupter_reset();

    usb_write32(usb_xhci.op + XHCI_OP_CONFIG / 4u, usb_xhci.max_slots);
    usb_write32(usb_xhci.op + XHCI_OP_USBCMD / 4u, XHCI_USBCMD_RS);
    if (!usb_wait_bits(usb_xhci.op + XHCI_OP_USBSTS / 4u, XHCI_USBSTS_HCH, 0u,
                       40000000u, 32u)) {
        serial_trace("WARNING", "USB controller did not start");
        return false;
    }
    if (!usb_wait_bits(usb_xhci.op + XHCI_OP_USBSTS / 4u, XHCI_USBSTS_CNR, 0u,
                       40000000u, 32u)) {
        serial_trace("WARNING", "USB controller not ready after starting");
        return false;
    }
    if ((usb_read32(usb_xhci.op + XHCI_OP_USBSTS / 4u) & XHCI_USBSTS_HSE) != 0u) {
        serial_trace("WARNING", "USB controller entered Host System Error");
        return false;
    }
    usb_xhci.started = true;
    return true;
}

/*
 * What the controller looks like before any command has been given to it.
 *
 * Whether it answers is not known yet - only a command can ask that - but the
 * registers that would explain a silent ring read the same now, and reading
 * them here means the boot menu has them even on a machine whose ports are all
 * empty and which therefore never issues a command at all.
 */
static void usb_xhci_probe_state(void) {
    uint32_t crcr = usb_read32(usb_xhci.op + XHCI_OP_CRCR / 4u);
    uint32_t base = (uint32_t)(uintptr_t)usb_cmd_ring[usb_xhci_cur];

    usb_xhci_usbsts = usb_read32(usb_xhci.op + XHCI_OP_USBSTS / 4u);
    usb_xhci_pci_command = pci_config_read32(usb_xhci.bus, usb_xhci.slot,
                                             usb_xhci.function, 0x04);
    /* The command ring register reports the address the controller will fetch
     * from next, so the difference from the ring's own base is how far into the
     * ring it got - zero meaning it never read one at all. */
    usb_xhci_command_deq = (crcr & ~0x1Fu) >= base ? (crcr & ~0x1Fu) - base : 0u;
    serial_trace_hex_value("INFO", "USB xHCI usbsts", usb_xhci_usbsts);
    serial_trace_hex_value("INFO", "USB xHCI crcr", crcr);
    serial_trace_hex_value("INFO", "USB xHCI pci command", usb_xhci_pci_command);
}

/*
 * Put a controller that stopped answering back into a known state: halt it,
 * reset it and program it again.
 *
 * The device context array is republished for everything already enumerated,
 * because a controller reset takes the slots with it and the port rescan only
 * replaces the devices it still has a record for.  A device whose context is
 * still pointed at gets its transfers back if the controller comes up, and one
 * whose slot is gone fails the way it would have failed anyway.
 */
static bool usb_xhci_recover(void) {
    uint32_t i;

    if (!usb_controller_bring_up()) {
        return false;
    }
    for (i = 0u; i < usb_dev_next; ++i) {
        if (usb_hid[i].in_use &&
            usb_hid[i].controller == (uint8_t)usb_xhci_cur) {
            usb_dcbaa[usb_xhci_cur][usb_hid[i].slot] =
                (uint64_t)(uintptr_t)usb_hid[i].ctx;
        }
    }
    usb_xhci_power_ports();
    return true;
}

/*
 * Bring the controller up, and make sure it will answer commands before
 * anything is built on top of it.
 *
 * A controller whose command ring stays silent has already been reset and
 * programmed once, and the one remaining suspect is the state the firmware
 * left it in before this driver touched it: a command engine it halted with an
 * error latched, or an interrupter it will not write events to.  Halting it
 * again, resetting it and programming it a second time costs one boot
 * iteration and is the difference between a machine with no keyboard and one
 * whose boot menu counts keys.  The probe's result is reported either way, so
 * a controller that stays silent says so instead of looking like a machine
 * whose keyboard was never plugged in.
 */
static bool usb_controller_start(void) {
    usb_xhci_command_state = USB_XHCI_COMMANDS_NOT_RUN;
    usb_xhci_command_retried = false;
    if (!usb_controller_bring_up()) {
        return false;
    }
    usb_xhci_probe_state();
    /* Exercise only the xHC command/event machinery before touching any USB
     * device.  The xHCI specification defines No Op as a command-ring test: a
     * processed No Op must generate a successful Command Completion Event.
     * Thus a failure here isolates the remaining problem to controller DMA,
     * ring/event state, or PCI/MMIO setup rather than HID enumeration. */
    if (!usb_command("USB xHCI no-op failed", 0u, 0u, 0u,
                     (TRB_NO_OP_COMMAND << 10))) {
        serial_trace("WARNING", "USB xHCI command/event self-test failed");
        return false;
    }
    /* Power first, then let the port state machines settle: right after a
     * controller reset a port can still be in connect detection (which reads
     * as "nothing attached"), and an unpowered port always does.  A device
     * that is slower than this window is caught by the rescan in the frame
     * loop, so boot never depends on guessing the delay right. */
    usb_xhci_power_ports();
    usb_wait_ticks(20);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Entry points                                                           */
/* ---------------------------------------------------------------------- */

/*
 * The other USB host controller families live in their own files, which are
 * included after this one and feed the same scancode and mouse decoders.
 * Their entry points are declared here because they are used before they are
 * defined - the bring-up order puts xHCI first only because a machine that
 * has it has nothing else on its USB ports.
 *
 * Which companion family a USB 2.0 chipset carries decides which of these
 * matters: Intel and SiS pair their EHCI block with UHCI, AMD, VIA and
 * nVidia with OHCI.  The EHCI driver deliberately leaves the ports to those
 * companions, so both are needed for a full-size keyboard to arrive from
 * every machine of that era.
 *
 * Neither family is expected on every machine, though, and their absence is
 * not a fault to report: the eighth-series chipsets and later pair an EHCI
 * block with an xHCI one instead - two EHCI controllers with no companion at
 * all, and the ports that matter owned by the xHCI block above.  A boot menu
 * that shows EHCI 2/2 with UHCI 0/0 and OHCI 0/0 is describing that machine
 * correctly, which is why those two counts are reported rather than required.
 */
static void usb_ehci_init_all(void);
static void usb_ehci_poll_all(void);
static void usb_ehci_rescan(void);
static void usb_uhci_init_all(void);
static void usb_uhci_poll_all(void);
static void usb_uhci_rescan(void);
static void usb_ohci_init_all(void);
static void usb_ohci_poll_all(void);
static void usb_ohci_rescan(void);

/* Bring up every xHCI block the machine has.  Stopping at the first one
 * would be enough on a machine with a single controller, but a desktop board
 * commonly carries two or three, and the keyboard is on whichever one owns
 * its port - not on the one the PCI scan happens to meet first. */
static void usb_xhci_init_all(void) {
    UsbPciCursor cursor;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;

    usb_pci_cursor_reset(&cursor);
    while (usb_xhci_count < USB_XHCI_MAX_CONTROLLERS &&
           usb_pci_find(&cursor, USB_PCI_PROGIF_XHCI, &bus, &slot, &function)) {
        ++usb_bus_found[USB_BUS_XHCI];
        usb_xhci_cur = usb_xhci_count;
        usb_xhci_all[usb_xhci_cur].started = false;
        if (!usb_xhci_attach(bus, slot, function)) {
            continue;
        }
        serial_trace_hex_value("INFO", "USB xHCI controller at",
                               ((uint32_t)bus << 24) | ((uint32_t)slot << 16) |
                               ((uint32_t)function << 8));
        if (!usb_controller_start()) {
            continue;
        }
        ++usb_xhci_count;
        ++usb_bus_started[USB_BUS_XHCI];
        usb_xhci_scan_ports();
    }
    usb_xhci_cur = 0u;
    serial_trace_uint_value("INFO", "USB HID devices", usb_hid_count);
    serial_trace_uint_value("INFO", "USB HID keyboards", usb_hid_keyboards);
    serial_trace_uint_value("INFO", "USB HID mice", usb_hid_mice);
}

/*
 * Re-look for devices that were not there when boot scanned: a port that was
 * still settling, a keyboard plugged in later, or a device that has been
 * unplugged and put back.  Half a second apart is often enough to be
 * invisible to the user and rare enough not to cost anything measurable.
 */
static void usb_rescan_all(void) {
    uint32_t controller;
    uint32_t i;

    for (controller = 0u; controller < usb_xhci_count; ++controller) {
        if (usb_dev_next < USB_DEVICE_MAX) {
            usb_xhci_cur = controller;
            usb_xhci_scan_ports();
        }
    }
    /* A device behind a hub is found through the hub and not through a port of
     * the controller, so the hubs are walked as well; a hub that has just been
     * added is picked up on the next pass. */
    for (i = 0u; i < usb_dev_next; ++i) {
        if (usb_hid[i].in_use && usb_hid[i].is_hub) {
            usb_xhci_cur = usb_hid[i].controller;
            usb_hub_scan(&usb_hid[i]);
        }
    }
    usb_xhci_cur = 0u;
    usb_ehci_rescan();
    usb_uhci_rescan();
    usb_ohci_rescan();
}

static void usb_hid_init(void) {
    if (usb_hid_ready) {
        return;
    }
    usb_hid_ready = true;
    /* The order matters only in that PS/2 is tried first by the machine
     * itself: whatever answers here feeds the same scancode decoder, so an
     * xHCI, an EHCI and a UHCI keyboard are all "the keyboard". */
    usb_xhci_init_all();
    usb_ehci_init_all();
    usb_uhci_init_all();
    usb_ohci_init_all();
    serial_trace_uint_value("INFO", "USB keyboards total", usb_keyboards_total);
    serial_trace_uint_value("INFO", "USB mice total", usb_mice_total);
    serial_trace_uint_value("INFO", "USB hubs walked", usb_hubs_total);
    usb_hubs_walked = usb_hubs_total;
}

static void usb_hid_poll(void) {
    uint32_t controller;
    static uint32_t polls = 0;
    static uint32_t next_rescan = 0;

    ++polls;
    if (polls <= 3u) {
        serial_trace_uint_value("INFO", "USB poll entered, call", polls);
    }
    for (controller = 0u; controller < usb_xhci_count; ++controller) {
        usb_xhci_cur = controller;
        usb_service_events();
    }
    usb_xhci_cur = 0u;
    usb_ehci_poll_all();
    usb_uhci_poll_all();
    usb_ohci_poll_all();
    if ((int32_t)(timer_ticks - next_rescan) >= 0) {
        next_rescan = timer_ticks + TIMER_HZ / 2u;
        usb_rescan_all();
    }
    if (polls > 30u && usb_events_seen != 0u && usb_reports_seen == 0u) {
        /* One line to say the controller is alive but silent: seen on the
         * serial log when a device is attached but never reports. */
        static bool said = false;

        if (!said) {
            said = true;
            serial_trace_uint_value("INFO", "USB events but no reports yet",
                                    usb_events_seen);
        }
    }
}

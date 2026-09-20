// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: vbe.c, runtime video mode setting through the video BIOS.
//
// This repository is licensed under the GNU General Public License.

/*
 * VBE services, so a machine whose card has no programmable registers can
 * still change its display mode - the thing Settings' resolution list is for.
 *
 * A card like Bochs/QEMU BGA or VMware SVGA is driven by its own registers and
 * needs none of this. Everything else (real hardware, an emulated BIOS, a VM
 * whose video device is not one of those) hands the bootloader one fixed mode and, without
 * the code below, is stuck with it for the whole session: the resolution list
 * can only re-size the desktop inside that mode, which is why the sizes it
 * offers look like they do nothing on those machines.
 *
 * The video BIOS is the only thing that can program them, and it lives in real
 * mode, so each call goes through the bridge in realmode.asm.  That file owns
 * the low-memory layout and publishes it as rm_layout; this file reads it, so
 * the two halves cannot disagree about where the register file is.
 *
 * What is offered is decided by the firmware, not by a wish list: the mode
 * list is walked once at boot (VBE function 4F00 for the controller, 4F01 for
 * each mode), and only modes that are supported, graphical and have a linear
 * framebuffer are kept.  The resolution list then asks this layer whether a
 * size is real on this machine, and the Apply path programs it with 4F02.
 */

/* ------------------------------------------------------------------------ */
/* The bridge (realmode.asm)                                                 */
/* ------------------------------------------------------------------------ */

extern const uint32_t rm_layout[17];
extern int realmode_call(void);
extern void realmode_install_gdt(void);

/* Indices into rm_layout: the low-memory addresses the 16-bit half uses. */
enum {
    RM_L_BASE,
    RM_L_REGS,
    RM_L_SAVED,
    RM_L_BUF,
    RM_L_STUB,
    RM_L_STACK,
    RM_L_EAX,
    RM_L_EBX,
    RM_L_ECX,
    RM_L_EDX,
    RM_L_ESI,
    RM_L_EDI,
    RM_L_EBP,
    RM_L_FLAGS,
    RM_L_DS,
    RM_L_ES,
    RM_L_FAULT /* the vector a BIOS call faulted on, or 0xFF for none */
};

#define RM_REG_COUNT 10

static uint8_t *vbe_reg_base(void) {
    return (uint8_t *)(uintptr_t)rm_layout[RM_L_REGS];
}

static void vbe_regs_clear(void) {
    volatile uint32_t *regs = (volatile uint32_t *)(void *)vbe_reg_base();

    for (int i = 0; i < RM_REG_COUNT; ++i) {
        regs[i] = 0;
    }
}

static void vbe_reg_write(int field, uint32_t value) {
    volatile uint32_t *slot = (volatile uint32_t *)(void *)(vbe_reg_base() + rm_layout[field]);
    *slot = value;
}

static uint32_t vbe_reg_read(int field) {
    const volatile uint32_t *slot =
        (const volatile uint32_t *)(const void *)(vbe_reg_base() + rm_layout[field]);
    return *slot;
}

/* ------------------------------------------------------------------------ */
/* VBE constants                                                             */
/* ------------------------------------------------------------------------ */

#define VBE_FUNC_CONTROLLER_INFO 0x4F00
#define VBE_FUNC_MODE_INFO       0x4F01
#define VBE_FUNC_SET_MODE        0x4F02
#define VBE_FUNC_CURRENT_MODE    0x4F03
#define VBE_FUNC_SUCCESS         0x004F
#define VBE_MODE_LINEAR          0x4000
#define VBE_MODE_NUMBER_MASK     0x01FF
#define VBE_NO_MODE              0xFFFF

/* VbeInfoBlock */
#define VBE_INFO_SIGNATURE       0x00
#define VBE_INFO_VERSION         0x04
#define VBE_INFO_MODE_PTR        0x0E
#define VBE_INFO_SIZE            512u

/*
 * ModeInfoBlock.  These are the positions the firmware really writes, read
 * back from the VGA BIOS on this machine rather than assumed: attributes at
 * the front, then the window fields, and the geometry through 0x1B - the part
 * every VBE version from 1.0 to 3.0 agrees on:
 *
 *   0x00 attributes          0x12 X resolution
 *   0x10 bytes per scanline  0x14 Y resolution
 *   0x19 bits per pixel      0x1B memory model
 *
 * The tail is where the versions part company, so it is not hard-coded: see
 * the layout table below.
 */
#define VBE_MI_ATTRIBUTES    0x00
#define VBE_MI_PITCH         0x10
#define VBE_MI_WIDTH         0x12
#define VBE_MI_HEIGHT        0x14
#define VBE_MI_BPP           0x19
#define VBE_MI_MEMORY_MODEL  0x1B

#define VBE_MI_SIZE          256u

/* A framebuffer base below 1 MB or not page aligned is not one. */
#define VBE_LINEAR_BASE_MIN      0x000A0000u
#define VBE_LINEAR_BASE_ALIGN    0x00000FFFu

#define VBE_MODE_SUPPORTED       0x0001
#define VBE_MODE_GRAPHICS        0x0010
#define VBE_MODE_LINEAR_OK       0x0080
#define VBE_MEMORY_MODEL_PACKED  4
#define VBE_MEMORY_MODEL_DIRECT  6

/* Depth classes the settings list asks about: 8bpp indexed, 15/16bpp, 24/32. */
#define VBE_DEPTH_NONE 0
#define VBE_DEPTH_8    1
#define VBE_DEPTH_16   2
#define VBE_DEPTH_32   3

#define VBE_MAX_SIZES 40

/*
 * The VGA BIOS's own "who serves INT 10h" byte in the BIOS data area, and the
 * bit in it that the vgabios behind QEMU's cards tests at the top of its VBE
 * entry.  See vbe_bios_call for what it is used for and why it is only touched
 * after a call has visibly been ignored.
 */
#define VBE_BDA_VGA_STATE 0x4B9
#define VBE_BDA_OWNER_BIT 0x40

/*
 * What the firmware said about one mode, kept from the walk at boot.  A mode
 * set that is accepted but not described afterwards can still be adopted from
 * here: the framebuffer base, the stride and the colour layout are properties
 * of the mode and the card, not of the moment the call was made.
 */
typedef struct {
    uint32_t linear_base;
    uint16_t pitch;
    uint8_t bpp;
    uint8_t depth;
    uint8_t has_masks;
    uint8_t red_mask_size;
    uint8_t red_position;
    uint8_t green_mask_size;
    uint8_t green_position;
    uint8_t blue_mask_size;
    uint8_t blue_position;
} VbeModeRecord;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t mode_8;
    uint16_t mode_16;
    uint16_t mode_32;
    VbeModeRecord record_8;
    VbeModeRecord record_16;
    VbeModeRecord record_32;
} VbeSize;

/*
 * Where one version of the ModeInfoBlock keeps its trailing fields.  Only the
 * colour masks move between versions (VBE 3.0 dropped the 1.x BankSize byte);
 * the linear-framebuffer base stays at 0x28 in every version a linear mode can
 * come from.  Which layout is in force is decided per mode by validating the
 * masks it yields, so a wrong entry in this table cannot silently decode a
 * mode with the wrong channels - it simply fails the check and the next entry
 * is tried.
 */
typedef struct {
    uint8_t red_size;
    uint8_t red_position;
    uint8_t green_size;
    uint8_t green_position;
    uint8_t blue_size;
    uint8_t blue_position;
    uint8_t phys_base;
    uint8_t lin_pitch;
} VbeTailLayout;

static const VbeTailLayout vbe_tail_layouts[] = {
    /* VBE 2.0/3.0: masks at 0x1E..0x23, physical base 0x28, linear pitch 0x30. */
    {0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x28, 0x30},
    /* VBE 1.0, and the 2.0 cards that kept its positions: 0x1F..0x24, pitch
     * at 0x34 where 3.0 splits its last reserved dword. */
    {0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x28, 0x34}
};

/* One mode, read out of a ModeInfoBlock. */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    uint8_t bpp;
    uint8_t depth;
    uint32_t linear_base;
    uint8_t red_mask_size;
    uint8_t red_position;
    uint8_t green_mask_size;
    uint8_t green_position;
    uint8_t blue_mask_size;
    uint8_t blue_position;
    bool has_masks;
} VbeModeInfo;

static VbeSize vbe_sizes[VBE_MAX_SIZES];
static int vbe_size_count = 0;
static bool vbe_usable = false;
static bool vbe_probed = false;

/*
 * Is the firmware allowed to be asked anything yet?
 *
 * Set by vbe_prepare(), which the desktop calls when the resolution list is
 * first opened - never by the boot.  This is the fix for a boot that showed
 * nothing and left the speaker beeping: the probe used to run out of the video
 * init, before the kernel had an interrupt table, so a fault inside the
 * real-mode BIOS call was undeliverable - a triple fault, a reset, and a POST
 * beep on every attempt, with nothing on screen and nothing in the log.  The
 * firmware is now only ever called from a running desktop.
 */
static bool vbe_probe_enabled = false;

/*
 * The mode number the firmware last reported as current, or VBE_NO_MODE while
 * that is not known.  Kept so that undoing a bad set can go back to the mode
 * the machine was really in - including one the bootloader set, which no
 * firmware mode list needs to contain.
 */
static uint16_t vbe_current_mode = VBE_NO_MODE;

/* Did the last call fault inside the firmware?  A firmware that faults is not
 * one to ask again, so the first one ends the mode-setting feature. */
static bool vbe_call_faulted = false;

/*
 * What the probe found, kept as one small answer rather than a sentence.
 *
 * "The resolution row will not move" has several unrelated causes - a machine
 * whose firmware was never asked, one whose firmware refused, one whose list
 * held nothing this kernel can present - and they are indistinguishable from
 * the outside.  The Settings window and the debugger show this value, so the
 * state of the feature is a fact on screen instead of a guess.
 */
typedef enum {
    VBE_STATUS_IDLE = 0,      /* not asked yet */
    VBE_STATUS_DISABLED,      /* HALOXOS_CONFIG_VBE_MODES is 0 in this build */
    VBE_STATUS_BLOCK_BUSY,    /* the bridge's low-memory block is in use */
    VBE_STATUS_NO_CONTROLLER, /* the firmware refused 4F00 */
    VBE_STATUS_NO_SIGNATURE,  /* 4F00 answered without a VESA signature */
    VBE_STATUS_BAD_LIST,      /* the mode-list pointer is unusable */
    VBE_STATUS_NO_MODES,      /* a controller, but no mode this kernel can show */
    VBE_STATUS_FAULTED,       /* the firmware faulted inside a call */
    VBE_STATUS_READY          /* the firmware's own list is the resolution row */
} VbeStatus;

static uint8_t vbe_status = VBE_STATUS_IDLE;

/* Entries of the firmware's mode list: walked, refused by the firmware, and
 * refused here because the block described nothing presentable.  Counted so a
 * run can say whether a short list came from the machine or from this file. */
static uint16_t vbe_list_entries = 0;
static uint16_t vbe_list_call_failed = 0;
static uint16_t vbe_list_rejected = 0;
static uint16_t vbe_list_kept = 0;

/*
 * The multiboot block the boot passed, kept for the probe's low-memory
 * ownership check.  Nothing here touches the firmware at boot: the probe runs
 * the first time the resolution list is actually needed (see vbe_probe).
 */
static const MultibootInfo *vbe_boot_info = NULL;

/*
 * How many entries of the firmware's mode list are looked at.  A real BIOS
 * publishes a few dozen; a corrupt or circular list would otherwise keep this
 * loop calling into the firmware for as long as it takes to look like a
 * hang.  Hitting the cap is not an error - the list is truncated.
 */
#define VBE_MAX_MODE_LIST 256

/*
 * The display controller's own view of the mode, read from its timing
 * registers rather than the firmware's answers.
 *
 * This is the only party that cannot lie about a mode set: 4F02 is a request
 * the firmware may accept, echo back and report as current in 4F03 without its
 * mode set having reached the hardware, and believing that is what put the
 * desktop's new geometry on a screen that could not show it.  The controller
 * has to be reprogrammed for a mode of another size or pitch, so its registers
 * are the evidence - and asking them costs no BIOS call at all, which matters
 * on a machine whose firmware does not survive being called during boot.
 */
static void vbe_crtc_geometry(uint16_t *width, uint16_t *pitch) {
    /* 0x01: horizontal display end, in characters of 8 pixels. */
    outb(VGA_CRTC_INDEX, 0x01);
    *width = (((uint16_t)inb(VGA_CRTC_DATA) + 1u) * 8u);
    /* 0x13: offset, in 8-byte units - the scan-line pitch in bytes. */
    outb(VGA_CRTC_INDEX, 0x13);
    *pitch = ((uint16_t)inb(VGA_CRTC_DATA) * 8u);
}

/*
 * Does a register reading describe this value?  Some cards clock characters
 * at 16 pixels, which halves or doubles what the register says, so the two
 * encodings either side of the plain one are accepted.  Nothing else is: a
 * reading that is neither the geometry asked for nor the one that was on
 * screen is not this mode.
 */
static bool vbe_crtc_matches(uint16_t decoded, uint16_t wanted) {
    return decoded == wanted || decoded == (uint16_t)(wanted * 2u) ||
           decoded == (uint16_t)(wanted / 2u);
}

/*
 * The display controller's timing registers, as a fingerprint.
 *
 * A VBE mode set is a request the firmware is free to accept without acting
 * on: measured on QEMU's cirrus, 4F02 answers AX = 4F, echoes the mode in BX
 * and makes 4F03 report the new number - while the display controller keeps
 * the timing it already had, so the screen is still the old size and the
 * desktop's new geometry lands on it as a squashed image.  Nothing in the
 * firmware's answers distinguishes that from a real mode change, but the
 * CRTC does: a mode of a different size or pitch has to reprogram it.
 */
static const uint8_t vbe_crtc_timing_registers[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09,
    0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x17, 0x18
};

#define VBE_CRTC_REGS (sizeof(vbe_crtc_timing_registers) / sizeof(vbe_crtc_timing_registers[0]))

static void vbe_crtc_fingerprint(uint8_t *out) {
    for (unsigned i = 0; i < VBE_CRTC_REGS; ++i) {
        outb(VGA_CRTC_INDEX, vbe_crtc_timing_registers[i]);
        out[i] = inb(VGA_CRTC_DATA);
    }
}

static bool vbe_crtc_unchanged(const uint8_t *a, const uint8_t *b) {
    for (unsigned i = 0; i < VBE_CRTC_REGS; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

/* Set once a call has been seen to need the BIOS data area's owner bit up
 * front, so later calls do not have to discover it by being ignored. */
static bool vbe_needs_owner_flag = false;

/* ------------------------------------------------------------------------ */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------ */

static uint16_t vbe_read16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t vbe_read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t *vbe_info_block(void) {
    return (uint8_t *)(uintptr_t)rm_layout[RM_L_BUF];
}

static uint8_t *vbe_mode_block(void) {
    return vbe_info_block() + VBE_INFO_SIZE;
}

/* The BIOS data area's VGA state byte, which carries the owner bit above.  The
 * address is held in a volatile object so that the accesses through it are not
 * folded into a fixed address the compiler can argue about. */
static volatile uint32_t vbe_owner_flag_address = VBE_BDA_VGA_STATE;

static volatile uint8_t *vbe_owner_flag_byte(void) {
    return (volatile uint8_t *)(uintptr_t)vbe_owner_flag_address;
}

/*
 * A call the firmware never looked at comes back with carry clear and AX still
 * holding the function number it was given - as opposed to a VBE failure, which
 * answers AX = 0x014F and the like.
 */
static bool vbe_call_was_ignored(uint16_t ax) {
    return (uint16_t)vbe_reg_read(RM_L_EAX) == ax;
}

/*
 * One INT 10h through the bridge, registers and buffer already in place.
 * Returns false when the bridge had to recover from a fault inside the
 * firmware call (see realmode.asm) instead of running it: the register file
 * then holds whatever the fault left and is not worth reading.
 */
static bool vbe_bios_issue(uint16_t ax, uint16_t bx, uint16_t cx, uint32_t buffer_linear) {
    vbe_regs_clear();
    vbe_reg_write(RM_L_EAX, ax);
    vbe_reg_write(RM_L_EBX, bx);
    vbe_reg_write(RM_L_ECX, cx);
    vbe_reg_write(RM_L_ES, (buffer_linear >> 4) & 0xFFFFu);
    vbe_reg_write(RM_L_EDI, buffer_linear & 0xFu);
    return realmode_call() == 0;
}

/*
 * Report a firmware call that faulted, once, and stop asking this firmware
 * anything else: a machine whose BIOS faults on one VBE function is not a
 * machine to keep poking.
 */
static bool vbe_firmware_faulted(void) {
    const volatile uint8_t *vector = (const volatile uint8_t *)(const void *)rm_layout[RM_L_FAULT];

    vbe_call_faulted = true;
    vbe_usable = false;
    serial_trace_hex_value("WARNING", "VBE: the firmware faulted during a BIOS call; vector",
                           *vector);
    serial_trace("WARNING", "VBE: runtime mode setting disabled on this machine");
    return false;
}

/*
 * One INT 10h through the bridge, with ES:DI set up so it addresses
 * `buffer_linear` exactly (segment = linear >> 4, offset = the low nibble).
 * Carry set, or an AX other than 4Fh, means the BIOS refused.
 *
 * A call the video BIOS never serviced is retried once with the owner bit in
 * the BIOS data area set (see above) - a mode set through the BIOS clears that
 * bit, and from then on the VBE entry ignores everything until it is put back.
 */
static bool vbe_bios_call(uint16_t ax, uint16_t bx, uint16_t cx, uint32_t buffer_linear) {
    volatile uint8_t *owner = vbe_owner_flag_byte();
    uint32_t flags;
    bool ignored;

    if (vbe_needs_owner_flag) {
        *owner |= VBE_BDA_OWNER_BIT;
    }

    if (!vbe_bios_issue(ax, bx, cx, buffer_linear)) {
        return vbe_firmware_faulted();
    }
    flags = vbe_reg_read(RM_L_FLAGS);
    ignored = (flags & 0x0001u) == 0 && vbe_call_was_ignored(ax);

    if (ignored && !vbe_needs_owner_flag && (*owner & VBE_BDA_OWNER_BIT) == 0) {
        *owner |= VBE_BDA_OWNER_BIT;
        if (!vbe_bios_issue(ax, bx, cx, buffer_linear)) {
            return vbe_firmware_faulted();
        }
        flags = vbe_reg_read(RM_L_FLAGS);
        if ((flags & 0x0001u) == 0 && !vbe_call_was_ignored(ax)) {
            /* That was it: this firmware wants the bit set before every call,
             * not just this one. */
            vbe_needs_owner_flag = true;
            serial_trace("INFO", "VBE: the video BIOS needs its owner bit asserted");
        }
    }

    if ((flags & 0x0001u) != 0) {
        return false;
    }
    return (uint16_t)vbe_reg_read(RM_L_EAX) == VBE_FUNC_SUCCESS;
}

/*
 * Which mode the machine is in.  Cached: the answer is asked for once (during
 * the probe) and the only later caller - undoing a mode set - has to be able
 * to use it without another BIOS call.  Pass VBE_NO_MODE here deliberately to
 * force a fresh read.
 */
static bool vbe_read_current_mode(void) {
    if (vbe_current_mode != VBE_NO_MODE) {
        return true;
    }
    if (!vbe_bios_call(VBE_FUNC_CURRENT_MODE, 0, 0, 0)) {
        return false;
    }
    vbe_current_mode = (uint16_t)(vbe_reg_read(RM_L_EBX) & VBE_MODE_NUMBER_MASK);
    if (vbe_current_mode == 0) {
        vbe_current_mode = VBE_NO_MODE;
        return false;
    }
    return true;
}

/* The size a mode number belongs to, or NULL when the firmware never listed
 * it (a mode the bootloader set, most of the time). */
static const VbeSize *vbe_size_of_mode(uint16_t mode) {
    for (int i = 0; i < vbe_size_count; ++i) {
        if (vbe_sizes[i].mode_8 == mode || vbe_sizes[i].mode_16 == mode ||
            vbe_sizes[i].mode_32 == mode) {
            return &vbe_sizes[i];
        }
    }
    return NULL;
}

static uint8_t vbe_depth_class(uint8_t bpp, uint8_t memory_model) {
    if (bpp == 8 && (memory_model == VBE_MEMORY_MODEL_PACKED || memory_model == 3)) {
        return VBE_DEPTH_8;
    }
    if (bpp >= 15 && bpp <= 16 && memory_model == VBE_MEMORY_MODEL_DIRECT) {
        return VBE_DEPTH_16;
    }
    if ((bpp == 24 || bpp == 32) && memory_model == VBE_MEMORY_MODEL_DIRECT) {
        return VBE_DEPTH_32;
    }
    return VBE_DEPTH_NONE;
}

static int vbe_size_index(uint16_t width, uint16_t height) {
    for (int i = 0; i < vbe_size_count; ++i) {
        if (vbe_sizes[i].width == width && vbe_sizes[i].height == height) {
            return i;
        }
    }
    return -1;
}

/* Ascending by area, so the resolution arrows walk from small to large the way
 * the Settings app expects. */
static void vbe_sort_sizes(void) {
    for (int i = 1; i < vbe_size_count; ++i) {
        VbeSize entry = vbe_sizes[i];
        int j = i - 1;
        uint32_t area = (uint32_t)entry.width * entry.height;

        while (j >= 0 && (uint32_t)vbe_sizes[j].width * vbe_sizes[j].height > area) {
            vbe_sizes[j + 1] = vbe_sizes[j];
            --j;
        }
        vbe_sizes[j + 1] = entry;
    }
}

/* The parts of a mode-info block that every VBE version puts in the same
 * place.  The tail is filled in by whoever picks a layout. */
static void vbe_read_geometry(const uint8_t *mi, VbeModeInfo *out) {
    out->pitch = vbe_read16(mi + VBE_MI_PITCH);
    out->width = vbe_read16(mi + VBE_MI_WIDTH);
    out->height = vbe_read16(mi + VBE_MI_HEIGHT);
    out->bpp = mi[VBE_MI_BPP];
    out->depth = vbe_depth_class(out->bpp, mi[VBE_MI_MEMORY_MODEL]);
    out->linear_base = 0;
    out->has_masks = false;
}

/* Does a reading describe a mode that could actually be programmed? */
static bool vbe_mode_is_real(const VbeModeInfo *m) {
    uint32_t bytes_per_pixel;

    if (m->depth == VBE_DEPTH_NONE || m->width == 0 || m->height == 0) {
        return false;
    }
    if (m->width > MAX_OUTPUT_WIDTH || m->height > MAX_OUTPUT_HEIGHT) {
        return false;
    }
    if (m->linear_base < VBE_LINEAR_BASE_MIN ||
        (m->linear_base & VBE_LINEAR_BASE_ALIGN) != 0) {
        return false;
    }
    bytes_per_pixel = ((uint32_t)m->bpp + 7u) / 8u;
    return (uint32_t)m->pitch >= (uint32_t)m->width * bytes_per_pixel;
}

/*
 * Are the three channels a believable description of a pixel of this depth?
 * Non-zero widths, every channel inside the pixel, and no two overlapping -
 * which is what picks the right layout, since reading the wrong offsets gives
 * widths or positions that contradict those rules.
 */
static bool vbe_masks_are_valid(const VbeModeInfo *m) {
    uint16_t ends[3];

    if (m->red_mask_size == 0 || m->green_mask_size == 0 || m->blue_mask_size == 0 ||
        m->red_mask_size > 8 || m->green_mask_size > 8 || m->blue_mask_size > 8) {
        return false;
    }
    ends[0] = (uint16_t)m->red_position + m->red_mask_size;
    ends[1] = (uint16_t)m->green_position + m->green_mask_size;
    ends[2] = (uint16_t)m->blue_position + m->blue_mask_size;
    for (int i = 0; i < 3; ++i) {
        if (ends[i] > m->bpp) {
            return false;
        }
        for (int j = i + 1; j < 3; ++j) {
            uint16_t size_a = i == 0 ? m->red_mask_size : (i == 1 ? m->green_mask_size : m->blue_mask_size);
            uint16_t start_a = i == 0 ? m->red_position : (i == 1 ? m->green_position : m->blue_position);
            uint16_t size_b = j == 0 ? m->red_mask_size : (j == 1 ? m->green_mask_size : m->blue_mask_size);
            uint16_t start_b = j == 0 ? m->red_position : (j == 1 ? m->green_position : m->blue_position);

            if (start_a < start_b + size_b && start_b < start_a + size_a) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Read a mode-info block.  The geometry is at fixed offsets; the tail is the
 * per-version part, so each documented layout is tried and the first one that
 * yields a programmable mode wins.  False means the block describes nothing
 * this kernel could put on screen.
 */
static bool vbe_read_mode_info(const uint8_t *mi, VbeModeInfo *out) {
    vbe_read_geometry(mi, out);

    for (unsigned i = 0; i < sizeof(vbe_tail_layouts) / sizeof(vbe_tail_layouts[0]); ++i) {
        const VbeTailLayout *tail = &vbe_tail_layouts[i];
        VbeModeInfo candidate = *out;
        uint16_t lin_pitch = tail->lin_pitch != 0 ? vbe_read16(mi + tail->lin_pitch) : 0;

        candidate.linear_base = vbe_read32(mi + tail->phys_base);
        if (lin_pitch != 0) {
            /* VBE 3.0's linear pitch, when the firmware publishes one. */
            candidate.pitch = lin_pitch;
        }
        if (candidate.bpp > 8) {
            /* Masks only exist for direct colour; an indexed mode is decoded
             * through its palette. */
            candidate.red_mask_size = mi[tail->red_size];
            candidate.red_position = mi[tail->red_position];
            candidate.green_mask_size = mi[tail->green_size];
            candidate.green_position = mi[tail->green_position];
            candidate.blue_mask_size = mi[tail->blue_size];
            candidate.blue_position = mi[tail->blue_position];
            candidate.has_masks = true;
        }

        if (vbe_mode_is_real(&candidate) &&
            (!candidate.has_masks || vbe_masks_are_valid(&candidate))) {
            *out = candidate;
            return true;
        }
    }
    return false;
}

/* The description the walk kept for one mode, archived from its block. */
static void vbe_save_record(VbeModeRecord *record, const VbeModeInfo *info) {
    record->linear_base = info->linear_base;
    record->pitch = info->pitch;
    record->bpp = info->bpp;
    record->depth = info->depth;
    record->has_masks = info->has_masks ? 1u : 0u;
    record->red_mask_size = info->red_mask_size;
    record->red_position = info->red_position;
    record->green_mask_size = info->green_mask_size;
    record->green_position = info->green_position;
    record->blue_mask_size = info->blue_mask_size;
    record->blue_position = info->blue_position;
}

/*
 * How good a pixel is for the desktop: 16 bits per pixel packs a 5-6-5
 * layout (six bits of green) while 15 packs 5-5-5, and the same goes for 32
 * against 24.
 *
 * This matters because a card usually offers more than one mode per size and
 * the firmware's own order is nothing to go by: a lot of VGA BIOSes list the
 * packed 15-bit mode (VBE 0x110) before the 16-bit one (0x111), so the old
 * "first mode wins" rule could pick the 15-bit pixel on a machine that had a
 * perfectly good 5-6-5 mode of the same size.  A real 16-bit pixel is
 * therefore preferred over a 15-bit one, and 32 over 24.
 */
static int vbe_pixel_quality(uint8_t bpp) {
    if (bpp == 16 || bpp == 32) {
        return 2;
    }
    return (bpp == 15 || bpp == 24) ? 1 : 0;
}

/* Store a mode's description, or replace an equal-size one this mode beats
 * (see vbe_pixel_quality).  Indexed modes keep the first: they differ only in
 * resolution, where the firmware's order is as good a rule as any. */
static void vbe_store_mode(VbeSize *size, uint16_t mode, const VbeModeInfo *info) {
    VbeModeRecord *record;
    uint16_t *slot;

    if (info->depth == VBE_DEPTH_8) {
        if (size->mode_8 != 0) {
            return;
        }
        slot = &size->mode_8;
        record = &size->record_8;
    } else if (info->depth == VBE_DEPTH_16) {
        slot = &size->mode_16;
        record = &size->record_16;
    } else {
        slot = &size->mode_32;
        record = &size->record_32;
    }

    if (*slot != 0 && vbe_pixel_quality(info->bpp) <= vbe_pixel_quality(record->bpp)) {
        return;
    }

    *slot = mode;
    vbe_save_record(record, info);
}

/* The mode-info block of the last successful VBE_FUNC_MODE_INFO call. */
static void vbe_record_mode(uint16_t mode) {
    const uint8_t *mi = vbe_mode_block();
    uint16_t attributes = vbe_read16(mi + VBE_MI_ATTRIBUTES);
    VbeModeInfo info;
    int index;

    if ((attributes & VBE_MODE_SUPPORTED) == 0 ||
        (attributes & VBE_MODE_GRAPHICS) == 0 ||
        (attributes & VBE_MODE_LINEAR_OK) == 0) {
        ++vbe_list_rejected;
        return;
    }
    if (!vbe_read_mode_info(mi, &info)) {
        ++vbe_list_rejected;
        return;
    }

    index = vbe_size_index(info.width, info.height);
    if (index < 0) {
        if (vbe_size_count >= VBE_MAX_SIZES) {
            ++vbe_list_rejected;
            return;
        }
        index = vbe_size_count++;
        vbe_sizes[index].width = info.width;
        vbe_sizes[index].height = info.height;
        vbe_sizes[index].mode_8 = 0;
        vbe_sizes[index].mode_16 = 0;
        vbe_sizes[index].mode_32 = 0;
    }

    vbe_store_mode(&vbe_sizes[index], mode, &info);
}

/*
 * Is the low-memory block free?  The bridge owns it (see rm_layout), and the
 * pointers the multiboot structure carries (the module list, the memory map)
 * are raw low-memory addresses this kernel still reads later, so they must not
 * be inside it.  A machine that puts them there keeps the old behaviour: no
 * runtime mode setting, rather than a corrupted boot.
 */
static bool vbe_low_block_is_free(const MultibootInfo *mbi) {
    uint32_t start = rm_layout[RM_L_BASE];
    uint32_t end = rm_layout[RM_L_STACK];
    uint16_t ebda_segment = *(volatile uint16_t *)(uintptr_t)0x040E;
    uint32_t ebda_base = (uint32_t)ebda_segment << 4;

    /*
     * The firmware's own extended BIOS data area.  The block sits in
     * conventional memory under the assumption that the EBDA is above it, and
     * on a machine that puts the EBDA lower (or keeps its scratch there) the
     * bridge would be writing over state the firmware still needs - which is
     * a machine that dies part way through a BIOS call with nothing on screen.
     * The BIOS data area says where it really starts, so ask before writing.
     */
    if (ebda_base != 0 && ebda_base < end) {
        serial_trace_hex_value("WARNING", "VBE: EBDA overlaps the bridge block", ebda_base);
        return false;
    }

    if (mbi == NULL) {
        return false;
    }
    if ((uint32_t)(uintptr_t)mbi + sizeof(MultibootInfo) > start &&
        (uint32_t)(uintptr_t)mbi < end) {
        return false;
    }
    if ((mbi->flags & (1u << 6)) != 0 && mbi->mmap_addr != 0 && mbi->mmap_length != 0 &&
        mbi->mmap_addr + mbi->mmap_length > start && mbi->mmap_addr < end) {
        return false;
    }
    if ((mbi->flags & (1u << 3)) != 0 && mbi->mods_addr != 0 && mbi->mods_count != 0 &&
        mbi->mods_addr + mbi->mods_count * 16u > start && mbi->mods_addr < end) {
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* The VBE layer itself                                                      */
/* ------------------------------------------------------------------------ */

/*
 * Remember the multiboot block for the probe, without calling the firmware.
 * Called at boot, where nothing else in this file may run: the boot path has
 * to be able to reach the boot menu and the desktop even if the firmware in
 * this machine cannot answer a real-mode BIOS call at all.
 */
static void vbe_note_boot_info(const MultibootInfo *mbi) {
    vbe_boot_info = mbi;
}

/*
 * Walk the firmware's mode list once, on first use.  `vbe_boot_info` is needed
 * only for the ownership check above.
 */
static bool vbe_probe(void) {
    const MultibootInfo *mbi = vbe_boot_info;
    uint8_t *info = vbe_info_block();
    const uint16_t *modes;
    uint32_t mode_list;
    int index;

    if (vbe_probed) {
        return vbe_usable;
    }

    /*
     * Not before the desktop asks.  This returns without latching anything, so
     * the probe still happens when it is enabled later - it is only the boot
     * path that must never get here, because the firmware call it would make
     * runs before the kernel's interrupt table exists.
     */
    if (!vbe_probe_enabled) {
        return false;
    }

    vbe_probed = true;
    vbe_usable = false;
    vbe_size_count = 0;
    vbe_list_entries = 0;
    vbe_list_call_failed = 0;
    vbe_list_rejected = 0;
    vbe_list_kept = 0;

#if !HALOXOS_CONFIG_VBE_MODES
    vbe_status = VBE_STATUS_DISABLED;
    serial_trace("INFO", "VBE: runtime mode setting disabled by HALOXOS_CONFIG_VBE_MODES");
    return false;
#else
    if (!vbe_low_block_is_free(mbi)) {
        vbe_status = VBE_STATUS_BLOCK_BUSY;
        serial_trace("WARNING", "VBE: low-memory block busy, runtime mode setting disabled");
        return false;
    }
    serial_trace("INFO", "VBE: probing the video BIOS for real display modes");

    /* The bridge brings its own GDT with it; from here on the BIOS can be
     * called from real mode. */
    realmode_install_gdt();

    /* VBE 3.0 only fills in the extended controller block when the caller
     * pre-seeds the signature, so ask with it in place. */
    info[0] = 'V';
    info[1] = 'B';
    info[2] = 'E';
    info[3] = '2';

    if (!vbe_bios_call(VBE_FUNC_CONTROLLER_INFO, 0, 0, (uint32_t)(uintptr_t)info)) {
        vbe_status = VBE_STATUS_NO_CONTROLLER;
        serial_trace("INFO", "VBE: no VBE controller on this video BIOS");
        return false;
    }
    if (info[0] != 'V' || info[1] != 'E' || info[2] != 'S' || info[3] != 'A') {
        vbe_status = VBE_STATUS_NO_SIGNATURE;
        serial_trace("INFO", "VBE: no VBE signature, runtime mode setting disabled");
        return false;
    }

    /* VideoModePtr is a real-mode far pointer: offset first, then segment. */
    mode_list = (uint32_t)(vbe_read16(info + VBE_INFO_MODE_PTR) |
                           ((uint32_t)vbe_read16(info + VBE_INFO_MODE_PTR + 2) << 4));

    /*
     * The list has to be somewhere a real-mode pointer can address (low
     * memory), and non-null.  A firmware that answers with a pointer into its
     * own ROM window or with nothing at all is not one to read 1024 entries
     * from - that is a walk through whatever happens to be mapped there, one
     * video BIOS call per entry.
     */
    if (mode_list == 0 || mode_list >= 0x00100000u || (mode_list & 1u) != 0) {
        vbe_status = VBE_STATUS_BAD_LIST;
        serial_trace_hex_value("WARNING", "VBE: unusable mode list pointer", mode_list);
        return false;
    }
    modes = (const uint16_t *)(uintptr_t)mode_list;

    for (index = 0; index < VBE_MAX_MODE_LIST; ++index) {
        uint16_t mode = modes[index];

        if (mode == VBE_NO_MODE) {
            break;
        }
        ++vbe_list_entries;
        if (vbe_bios_call(VBE_FUNC_MODE_INFO, 0, mode, (uint32_t)(uintptr_t)vbe_mode_block())) {
            vbe_record_mode(mode);
        } else {
            ++vbe_list_call_failed;
        }
    }

    vbe_sort_sizes();
    vbe_list_kept = (uint16_t)vbe_size_count;
    vbe_usable = vbe_size_count > 0;
    vbe_status = vbe_usable ? VBE_STATUS_READY : VBE_STATUS_NO_MODES;

    /*
     * Learn which mode the machine is in while the firmware is being talked to
     * anyway: it is what a refused mode set is undone to, and the mode the
     * bootloader set is normally not in the list, so nothing else here knows
     * it.  A firmware that faults on it is a firmware with nothing more to
     * offer, so the feature ends here rather than at the next call.
     */
    if (vbe_usable) {
        (void)vbe_read_current_mode();
        if (vbe_call_faulted) {
            vbe_usable = false;
            vbe_status = VBE_STATUS_FAULTED;
        }
    }

    serial_trace_uint_value("INFO", "VBE: list entries", vbe_list_entries);
    serial_trace_uint_value("INFO", "VBE: calls refused", vbe_list_call_failed);
    serial_trace_uint_value("INFO", "VBE: blocks rejected", vbe_list_rejected);
    serial_trace_uint_value("INFO", "VBE: sizes kept", vbe_list_kept);

    if (vbe_usable) {
        serial_trace("INFO", "VBE: runtime mode setting available");
    } else {
        serial_trace("INFO", "VBE: no usable linear framebuffer modes");
    }
    return vbe_usable;
#endif /* HALOXOS_CONFIG_VBE_MODES */
}

/*
 * Is the resolution list the firmware's own list?  Deliberately asks nothing:
 * this is read from the boot path (and from every list query), and the answer
 * is whatever the last user-triggered vbe_prepare() left behind.
 */
static bool vbe_mode_available(void) {
    return vbe_usable;
}

/*
 * What to call the state the probe left behind, in a few words.  Shown in
 * Settings beside the count of sizes it found: "Video BIOS: 5 sizes" is the
 * whole story of whether this machine's row is the firmware's or built-in,
 * and on a machine that answers with nothing it names the reason instead, so
 * a report of "the resolutions will not change" can be told apart from "the
 * firmware listed nothing".
 */
static const char *vbe_status_text(void) {
    switch (vbe_status) {
        case VBE_STATUS_READY: return "list";
        case VBE_STATUS_IDLE: return "not checked";
        case VBE_STATUS_DISABLED: return "off in this build";
        case VBE_STATUS_BLOCK_BUSY: return "memory in use";
        case VBE_STATUS_NO_CONTROLLER: return "no controller";
        case VBE_STATUS_NO_SIGNATURE: return "no VESA";
        case VBE_STATUS_BAD_LIST: return "bad mode list";
        case VBE_STATUS_NO_MODES: return "no linear modes";
        case VBE_STATUS_FAULTED: return "firmware fault";
        default: return "none";
    }
}

/*
 * Ask the firmware what it can program, now that the desktop is running.
 * Called the first time Settings is opened; the first call does the work and
 * every later one is free.
 */
static void vbe_prepare(void) {
    if (vbe_probe_enabled) {
        return;
    }
    vbe_probe_enabled = true;
    (void)vbe_probe();
}


/* Which VBE depth class a settings palette mode needs: Indexed and Low are
 * 8bpp, True-Color! is direct colour. */
static uint8_t vbe_depth_for_palette(uint8_t palette_mode) {
    return palette_mode == 2 ? VBE_DEPTH_16 : VBE_DEPTH_8;
}

/* Defined with the archived records, below; the support query needs it. */
static uint16_t vbe_pick_mode_record(uint16_t width, uint16_t height, uint8_t depth,
                                    const VbeModeRecord **record);

/*
 * Can this machine show that size?  This is what the resolution list's arrows
 * and colours ask.
 *
 * The colour row is a preference, not a filter.  Asking only for the depth the
 * selected colour mode wants made the row dead-end on any machine whose small
 * modes are in the other depth: with True-Color! selected, a 320x200 that the
 * firmware offers as an indexed mode was refused, so the arrow back from the
 * boot size had nowhere to go and the row read as stuck - which is exactly
 * what a machine reports as "the resolutions will not change".  A size this
 * firmware can program at all is selectable; the depth it can take is what
 * gets programmed (vbe_pick_mode_record falls back, vbe_set_mode adopts what
 * the firmware reports, and the colour row follows the result).
 */
static bool vbe_size_supported(uint16_t width, uint16_t height, uint8_t palette_mode) {
    const VbeModeRecord *record = NULL;
    uint8_t preferred = vbe_depth_for_palette(palette_mode);
    uint8_t alternate = preferred == VBE_DEPTH_8 ? VBE_DEPTH_16 : VBE_DEPTH_8;

    (void)vbe_probe();
    if (vbe_pick_mode_record(width, height, preferred, &record) != 0) {
        return true;
    }
    return vbe_pick_mode_record(width, height, alternate, &record) != 0;
}

/*
 * Which of a size's three modes this depth class wants, with the description
 * the walk archived for exactly that mode.  A mode picked through its bigger
 * neighbour has to be adopted from that neighbour's record, not the one the
 * depth would suggest, which is why they travel together.
 */
static uint16_t vbe_pick_mode_record(uint16_t width, uint16_t height, uint8_t depth,
                                    const VbeModeRecord **record) {
    int index = vbe_size_index(width, height);
    const VbeSize *entry;

    if (index < 0) {
        return 0;
    }
    entry = &vbe_sizes[index];
    if (depth == VBE_DEPTH_8) {
        if (entry->mode_8 != 0) {
            *record = &entry->record_8;
            return entry->mode_8;
        }
        /*
         * Indexed asked for, and this size has no indexed mode - a small mode
         * is often direct-colour-only, the other way round from the rest of
         * the list.  The size is still one this machine can show, so the
         * deeper mode is the answer rather than no answer: the caller adopts
         * the depth the firmware reports (see vbe_apply_record), and the
         * colour row follows it (see apply_settings).
         */
        if (entry->mode_16 != 0) {
            *record = &entry->record_16;
            return entry->mode_16;
        }
        if (entry->mode_32 != 0) {
            *record = &entry->record_32;
        }
        return entry->mode_32;
    }
    if (depth == VBE_DEPTH_16) {
        if (entry->mode_16 != 0) {
            *record = &entry->record_16;
            return entry->mode_16;
        }
        /* True-Color! on a card that only offers 24/32bpp direct colour: the
         * present step handles every depth the framebuffer reports, so the
         * deeper mode is the right answer rather than no answer. */
        if (entry->mode_32 != 0) {
            *record = &entry->record_32;
        }
        return entry->mode_32;
    }
    if (entry->mode_32 != 0) {
        *record = &entry->record_32;
        return entry->mode_32;
    }
    if (entry->mode_16 != 0) {
        *record = &entry->record_16;
    }
    return entry->mode_16;
}

/* Point the framebuffer at a mode just described by a mode-info block. */
static void vbe_apply_mode_info(const VbeModeInfo *info) {
    fb.address = (uint8_t *)(uintptr_t)info->linear_base;
    fb.width = info->width;
    fb.height = info->height;
    fb.pitch = info->pitch;
    fb.bpp = info->bpp;
    fb.type = fb.bpp == 8 ? MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED : MULTIBOOT_FRAMEBUFFER_TYPE_RGB;

    if (fb.type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB && info->has_masks &&
        vbe_masks_are_valid(info)) {
        fb.red_mask_size = info->red_mask_size;
        fb.red_position = info->red_position;
        fb.green_mask_size = info->green_mask_size;
        fb.green_position = info->green_position;
        fb.blue_mask_size = info->blue_mask_size;
        fb.blue_position = info->blue_position;
    } else {
        set_default_framebuffer_format(fb.bpp);
    }
}

/* Point the framebuffer at a mode described by one of the archived records. */
static void vbe_apply_record(const VbeModeRecord *record) {
    fb.address = (uint8_t *)(uintptr_t)record->linear_base;
    fb.pitch = record->pitch;
    fb.bpp = record->bpp;
    fb.type = fb.bpp == 8 ? MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED : MULTIBOOT_FRAMEBUFFER_TYPE_RGB;

    if (fb.type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB && record->has_masks) {
        /* The card's own channel layout, not an assumed one: the present step
         * decodes with exactly these fields. */
        fb.red_mask_size = record->red_mask_size;
        fb.red_position = record->red_position;
        fb.green_mask_size = record->green_mask_size;
        fb.green_position = record->green_position;
        fb.blue_mask_size = record->blue_mask_size;
        fb.blue_position = record->blue_position;
    } else {
        set_default_framebuffer_format(fb.bpp);
    }
}

/* Does a framebuffer's colour depth belong to this VBE depth class?  Used to
 * tell "the machine is already in the mode being asked for" from a request
 * that really has to be programmed. */
static bool vbe_depth_matches(uint8_t bpp, uint8_t depth) {
    return depth == VBE_DEPTH_8 ? bpp == 8 : bpp >= 15;
}

/*
 * Undo a mode set that did not survive its checks: put the mode the machine
 * was already in back on the display and put the framebuffer description back
 * with it, so the desktop keeps describing the screen it is really on.  The
 * shadow planes still hold the desktop, so the repaint that follows puts the
 * image back; nothing here decides what the desktop shows.
 *
 * The way back is the firmware's own mode number for the size and depth the
 * machine was in - it was in one of the modes this machine listed, so the
 * number is in the list.  (A mode the bootloader set from outside the list, as
 * is normal at boot, cannot be programmed back through VBE; the geometry in
 * `fb` is restored either way, and the log says so.)
 */
static void vbe_rollback(const Framebuffer *saved, uint16_t previous_mode, uint16_t attempted) {
    const VbeModeRecord *record = NULL;
    uint16_t back = previous_mode;

    if (back == VBE_NO_MODE || back == 0 || back == attempted) {
        /*
         * The firmware never said which mode the machine was in, so the way
         * back is its own entry for the size and depth that was on screen.
         */
        back = vbe_pick_mode_record((uint16_t)saved->width, (uint16_t)saved->height,
                                    saved->bpp == 8 ? VBE_DEPTH_8 : VBE_DEPTH_16, &record);
    }
    if (back != 0 && back != VBE_NO_MODE && back != attempted && !vbe_call_faulted) {
        if (vbe_bios_call(VBE_FUNC_SET_MODE, (uint16_t)(back | VBE_MODE_LINEAR), 0, 0)) {
            vbe_current_mode = back;
        } else {
            serial_trace("WARNING", "VBE: could not restore the previous mode");
        }
    }

    fb = *saved;
    update_present_maps();
    ui_input_dirty = true;
}

/*
 * The real thing: ask the video BIOS for a linear mode of this size and depth,
 * then describe the framebuffer as the firmware describes it - or, when the
 * firmware will not say, as the walk at boot said it would be.  Returns false
 * when this machine has no such mode, so the caller can refuse the request
 * outright instead of leaving the screen on a mode its own maps do not
 * describe.
 *
 * The set is transactional.  A mode set is the one operation here that can
 * leave a real card in a state this kernel cannot describe - the firmware
 * reporting success while programming its own idea of the size, or answering
 * afterwards with a stale block - and the failure mode of believing it is a
 * screenful of pixels nothing matches.  So the previous mode number and the
 * framebuffer description are taken first, the firmware's answer is checked,
 * and anything that does not add up puts both back.
 */
static bool vbe_set_mode(uint16_t width, uint16_t height, uint8_t bpp) {
    uint8_t depth = (bpp == 8) ? VBE_DEPTH_8 : VBE_DEPTH_16;
    const VbeModeRecord *record = NULL;
    Framebuffer saved = fb;
    VbeModeInfo fresh;
    uint16_t mode;
    uint16_t granted;
    uint16_t actual;
    uint16_t previous_mode = VBE_NO_MODE;
    bool described = false;
    uint8_t crtc_before[VBE_CRTC_REGS];
    uint8_t crtc_after[VBE_CRTC_REGS];
    bool timing_must_change;

    if (!vbe_usable || vbe_call_faulted) {
        return false;
    }

    mode = vbe_pick_mode_record(width, height, depth, &record);
    if (mode == 0) {
        /*
         * The depth the colour row asked for is not offered at this size, but
         * the other class may be - which is why vbe_size_supported offers such
         * a size to the row at all.  Program what the machine has: the
         * framebuffer ends up describing the depth really in force and the
         * colour row follows it (see apply_settings).
         */
        depth = depth == VBE_DEPTH_8 ? VBE_DEPTH_16 : VBE_DEPTH_8;
        mode = vbe_pick_mode_record(width, height, depth, &record);
    }
    if (mode == 0 || record == NULL) {
        return false;
    }

    /*
     * Already there?  Asking the firmware to set the mode the machine is in
     * costs a display blank on real hardware and buys nothing, so it is not
     * asked.  The mode number is the firmware's own answer from the probe
     * (a mode the bootloader set is unknown here, and is set normally).
     */
    (void)vbe_read_current_mode();
    previous_mode = vbe_current_mode;
    if (previous_mode == mode && saved.width == width && saved.height == height &&
        vbe_depth_matches(saved.bpp, depth)) {
        return true;
    }

    /*
     * Will this set have to move the display controller?  A mode of another
     * size or pitch must reprogram the timing registers and the start/offset
     * fields; a mode of the same size and pitch (a depth change, or the mode
     * the machine is already in) need not touch them at all.  Only the first
     * kind is evidence-checked below.
     */
    timing_must_change = (width != saved.width || height != saved.height ||
                          record->pitch != saved.pitch);
    vbe_crtc_fingerprint(crtc_before);

    if (!vbe_bios_call(VBE_FUNC_SET_MODE, (uint16_t)(mode | VBE_MODE_LINEAR), 0, 0)) {
        serial_trace("WARNING", "VBE: the video BIOS refused the mode");
        return false;
    }

    /* Some BIOSes answer with the mode they really programmed; the rest leave
     * BX alone, in which case the requested number is the one to read back. */
    granted = (uint16_t)(vbe_reg_read(RM_L_EBX) & VBE_MODE_NUMBER_MASK);
    actual = granted != 0 ? granted : mode;

    /*
     * Did the display really end up on the mode that was asked for?  Only a
     * firmware that names a DIFFERENT size it also offered is treated as
     * having silently programmed something else: an unlisted number, or a
     * firmware with no 4F03, is left alone, since there is no evidence
     * against the request - and a check that fails a working machine would be
     * worse than the quirk it guards against.
     */
    vbe_current_mode = VBE_NO_MODE; /* this call is what changes it */
    if (vbe_read_current_mode() && !vbe_call_faulted) {
        const VbeSize *in_force = vbe_size_of_mode(vbe_current_mode);

        if (in_force != NULL && (in_force->width != width || in_force->height != height)) {
            serial_trace("WARNING", "VBE: the video BIOS set another size - putting the old mode back");
            vbe_rollback(&saved, previous_mode, actual);
            return false;
        }
    }

    /*
     * Now ask what is really on screen.  A firmware that has just been handed
     * a mode set is not always in a position to answer: some keep quiet, some
     * answer with the *previous* mode's block.  A stale answer is worse than
     * no answer - the desktop would take a geometry the display is not in - so
     * the block is only believed when it describes the size that was asked for
     * at the depth class that was asked for, and the descriptor the walk kept
     * for this exact mode is the fallback.
     */
    if (vbe_bios_call(VBE_FUNC_MODE_INFO, 0, actual, (uint32_t)(uintptr_t)vbe_mode_block()) &&
        !vbe_call_faulted &&
        vbe_read_mode_info(vbe_mode_block(), &fresh) &&
        fresh.width == width && fresh.height == height && fresh.depth == record->depth) {
        vbe_apply_mode_info(&fresh);
        described = true;
    }
    if (vbe_call_faulted) {
        /* The firmware faulted part way through the set: put back what the
         * machine was showing and refuse the request. */
        vbe_rollback(&saved, previous_mode, actual);
        return false;
    }
    if (!described) {
        /*
         * The firmware would not describe the mode it just set.  The record
         * from the boot walk still can, but only if it is internally sound:
         * adopting a description whose stride cannot hold a row, or whose
         * framebuffer base is not one, would draw the desktop into memory
         * that is not the screen.
         */
        VbeModeInfo archived;

        archived.width = width;
        archived.height = height;
        archived.linear_base = record->linear_base;
        archived.pitch = record->pitch;
        archived.bpp = record->bpp;
        archived.depth = record->depth;
        if (!vbe_mode_is_real(&archived)) {
            serial_trace("WARNING", "VBE: no usable description of the new mode - putting the old mode back");
            vbe_rollback(&saved, previous_mode, actual);
            return false;
        }
        fb.width = width;
        fb.height = height;
        vbe_apply_record(record);
    }

    /*
     * Did the display really move?  The firmware cannot be asked this: it
     * answers that the mode it was given is in force even when its mode set
     * did not reach the hardware, and that answer is what left the desktop
     * drawing a geometry the screen was not in.  The controller itself can be
     * read, though, and a mode of another size or pitch that left every timing
     * register exactly as it was never happened - so the previous mode and the
     * framebuffer description go back and the caller is told the switch was
     * refused, rather than the desktop being painted into a screen that cannot
     * show it.
     */
    if (timing_must_change) {
        vbe_crtc_fingerprint(crtc_after);
        if (vbe_crtc_unchanged(crtc_before, crtc_after)) {
            serial_trace("WARNING", "VBE: the display timing never moved - putting the old mode back");
            vbe_rollback(&saved, previous_mode, actual);
            return false;
        }
    }

    vbe_current_mode = actual;
    return true;
}

/* ------------------------------------------------------------------------ */
/* What the resolution row may offer                                          */
/* ------------------------------------------------------------------------ */

/*
 * The sizes the firmware itself listed, so Settings can offer what this
 * machine really has instead of only the seven built-in entries - a card with
 * 832x624 or 1152x864 in its list could never be asked for them before, and a
 * card with fewer usable sizes had arrows that skipped over most of the table.
 * The list is the firmware's, ascending by area, so it walks small to large
 * exactly like the built-in columns do.
 *
 * The widescreen checkbox stays meaningful by splitting on aspect ratio: a
 * size is "wide" when it is wider than 4:3, which is the same distinction the
 * built-in table draws between its two columns.
 */
/*
 * The firmware's list is ONE ascending run of sizes, and the widescreen flag
 * does not split it.
 *
 * The two columns belong to the built-in table, where the same index has to
 * name a 4:3 size and a 16:9 one.  A firmware list has no such structure: its
 * entries are the modes the card can really program, and the split only hid
 * them from each other - the smallest mode a machine offers is usually a
 * low-resolution one (320x200, 640x400) and those are wider than 4:3, so with
 * the 4:3 column selected the arrow had nowhere to go below the boot size and
 * the row looked stuck.  One run means either arrow reaches every size the
 * machine has, smallest to largest, which is what a resolution list is for.
 */
static int vbe_size_list_length(bool widescreen) {
    (void)widescreen;
    return vbe_size_count;
}

/*
 * Does the firmware list any mode in this depth class at all?  The colour
 * mode row asks this the same way the resolution row asks
 * vbe_size_supported: a palette mode whose depth this card has no mode for
 * cannot be applied, so it is greyed rather than silently refused on Apply.
 */
static bool vbe_depth_offered(uint8_t depth_class) {
    for (int i = 0; i < vbe_size_count; ++i) {
        if (depth_class == VBE_DEPTH_8) {
            if (vbe_sizes[i].mode_8 != 0) {
                return true;
            }
        } else if (vbe_sizes[i].mode_16 != 0 || vbe_sizes[i].mode_32 != 0) {
            /* A size whose direct-colour entry is a 24/32bpp mode still
             * serves True-Color! - vbe_pick_mode_record falls back to it. */
            return true;
        }
    }
    return false;
}

/* The nth size of the firmware's run (see vbe_size_list_length). */
static bool vbe_size_list_at(bool widescreen, int index, VideoModeOption *out) {
    (void)widescreen;

    if (index < 0 || index >= vbe_size_count) {
        return false;
    }
    out->width = vbe_sizes[index].width;
    out->height = vbe_sizes[index].height;
    return true;
}

/* Where a size sits in the firmware's run, or -1 when the firmware does not
 * list that size at all. */
static int vbe_size_list_index_of(uint16_t width, uint16_t height, bool *widescreen) {
    int index = vbe_size_index(width, height);

    if (index < 0) {
        return -1;
    }
    *widescreen = false;
    return index;
}

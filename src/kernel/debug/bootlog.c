// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: bootlog.c, on-medium debug serial log (bios-serial.log)
//              persistence for real hardware.
//
// This repository is licensed under the GNU General Public License.

/*
 * Modern machines have no serial port, so there is nothing on the other
 * end of COM1 to catch the trace the way the QEMU run targets do. The
 * kernel therefore mirrors every serial byte in RAM (serial.c) and,
 * when the boot medium is a disk it can drive itself, persists that
 * mirror into a REAL FILE - bios-serial.log - inside the FAT filesystem
 * of that disk. Plug it into any computer afterwards and the log is
 * simply there, a plain text file; no tool, no extraction step.
 *
 *   - DD'd ISO sticks: the Makefile appends a small embedded FAT12
 *     partition to the ISO (after the volume, before the DOOM WAD tail)
 *     and registers it in the hybrid MBR, carrying a pre-allocated,
 *     zero-filled bios-serial.log. Discovery reaches it through BIOS
 *     INT 13h reads on the real-mode bridge - the same firmware path
 *     GRUB itself booted through - so the file is visible on a USB stick
 *     the kernel has no driver for. On a bare-metal boot the same bridge
 *     writes the trace back into that validated file, but only on the
 *     drive the loader actually booted from. A hypervisor's firmware disk
 *     stack can wait for an interrupt the bridge masks forever, so after
 *     the graphics mode is programmed those calls stop and the trace is
 *     COM1/RAM only; a non-boot firmware disk is never written.
 *
 *   - `make disk` hard-disk boots: the same template file is placed in
 *     the image's existing FAT16 partition at build time and is written
 *     through the polled primary-master ATA driver.
 *
 * Safety: before a single byte is written, the kernel walks MBR ->
 * boot-sector BPB -> root directory -> the file's FAT cluster chain ->
 * a hidden stamp in the file's allocation tail (past EOF, invisible to
 * any host, holding magic + version + checksum). A foreign FAT volume
 * with a coincidental .LOG file fails the stamp and is never touched;
 * writes stay strictly inside the file's own clusters.
 *
 * Two write paths share the discovered target (when discovery armed a
 * write-safe one):
 *
 *   - Auto-save (serial_bootlog_autosave_tick, from the main loop): as
 *     the trace grows, only the NEW sectors are streamed out, roughly
 *     every 1 KiB of trace or every couple of seconds, and the directory
 *     entry's size field is grown to match. An accidental power cut -
 *     the plug pulled, an outage, a hang reset - loses at most the last
 *     window, never the whole boot. Runs in the boot menu, the login
 *     screen and the desktop alike.
 *
 *   - Final flush (serial_bootlog_flush, from power off / restart /
 *     halt / the crash handler): appends the reason line and syncs to
 *     the very end, so a deliberate shutdown or a BSOD dump is on the
 *     medium complete.
 *
 * A stick flashed by ISO-mode file extraction carries the OS
 * files in the writer's own FAT32 volume, which this writer deliberately does
 * not touch (FAT32 has a cluster-chained root directory and live
 * allocation semantics), so discovery warns once and points at DD mode.
 * A CD-R is read-only by physics and is skipped. A firmware disk that is
 * not the boot drive is readable for diagnostics but never written, so a
 * stray internal disk carrying an old template cannot be touched. Release
 * builds compile a no-op and keep none of this.
 */

#if HALOXOS_CONFIG_DEBUG

/* ---- hidden tail stamp (last 512 bytes of the file's allocation) ----- */
#define BOOTLOG_MAGIC      0x4C584C48u /* 'H','L','X','L'                 */
#define BOOTLOG_VERSION    2u          /* 2 = FAT-file format             */

#define BS_MAGIC       0
#define BS_VERSION     4
#define BS_RESERVED    8
#define BS_CAPACITY   12
#define BS_WRITE_BYTES 16
#define BS_BOOT_COUNT  20
#define BS_CHECKSUM    24

/* FAT limits the walker accepts; 128 clusters is 64 KiB at the smallest
 * cluster size the template builder produces. */
#define BOOTLOG_FAT_MAX_CLUSTERS 128

static uint32_t bootlog_get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void bootlog_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static bool bootlog_stamp_valid(const uint8_t *sector, uint32_t capacity) {
    uint32_t x = 0;

    if (bootlog_get32(sector + BS_MAGIC) != BOOTLOG_MAGIC ||
        bootlog_get32(sector + BS_VERSION) != BOOTLOG_VERSION ||
        bootlog_get32(sector + BS_CAPACITY) != capacity) {
        return false;
    }
    for (uint32_t off = 0; off < BS_CHECKSUM; off += 4) {
        x ^= bootlog_get32(sector + off);
    }
    return x == bootlog_get32(sector + BS_CHECKSUM);
}

/* ---- real-mode bridge (realmode.asm) ---------------------------------- */
/* The bridge externs and the RM_L_* register-field indices are declared
 * by vbe.c, which kernel.c includes before this file; redeclaring the
 * externs here with compatible types is fine, redeclaring the enum
 * would not be. RM_L_INT is the one index vbe.c does not name: the INT
 * selector word appended to rm_layout by the boot-log bridge extension
 * (entry 17, RM_R_INT = offset 0x26 in the register file). */
extern const uint32_t rm_layout[];
extern int realmode_call(void);
extern void realmode_install_gdt(void);
extern void realmode_configure_stack(const MultibootInfo *mbi);
extern uint32_t realmode_stack_bytes(void);

#define RM_L_INT 17

/*
 * Transfer window below 1 MiB the BIOS can address: linear 0x20000,
 * 32 KiB at a time. Clear of the bridge block (0x8000), the firmware's
 * real-mode stack (0x6000s..0x9F00), the boot sector and the EBDA -
 * ordinary free conventional memory nothing in the boot chain keeps
 * live after GRUB hands over.
 */
#define BOOTLOG_XFER_LINEAR 0x20000u
#define BOOTLOG_XFER_BYTES  0x8000u
#define BOOTLOG_XFER_SEG    0x2000u

static uint8_t *bootlog_rm_regs(void) {
    return (uint8_t *)(uintptr_t)rm_layout[RM_L_REGS];
}

/* Last bridge result.  Keep this separately from the register file: a
 * recovered real-mode exception means the register contents are undefined,
 * while AH is useful when a firmware deliberately rejects an INT 13h call.
 * The two probes below snapshot these fields before any subsequent disk I/O
 * can replace them. */
static bool bootlog_rm_last_faulted;
static uint8_t bootlog_rm_last_fault_vector;
static uint8_t bootlog_rm_last_ah;

/* The stub restores the firmware's IVT on its way out.  Verify it from here
 * as well: the IVT is the only piece of firmware state a call can silently
 * leave changed, and on a real machine a changed INT 09h entry is a dead
 * keyboard rather than a visible error.  A mismatch is reported on the boot
 * menu (see serial_bootlog_status_text). */
static uint32_t bootlog_rm_calls;
static bool bootlog_ivt_ok = true;
static uint8_t bootlog_ivt_bad_index = 0xFFu;
static uint32_t bootlog_ivt_before[16];

static void bootlog_ivt_snapshot(void) {
    const volatile uint32_t *ivt = (const volatile uint32_t *)(uintptr_t)0u;

    for (int i = 0; i < 16; ++i) {
        bootlog_ivt_before[i] = ivt[i];
    }
}

static void bootlog_ivt_verify(void) {
    const volatile uint32_t *ivt = (const volatile uint32_t *)(uintptr_t)0u;

    for (int i = 0; i < 16; ++i) {
        if (ivt[i] != bootlog_ivt_before[i]) {
            if (bootlog_ivt_ok) {
                /* One line per boot, with the offending vector and both
                 * values: enough to tell a leaked trampoline from a
                 * firmware write without another round trip. */
                serial_trace_hex_value("WARNING", "boot log: IVT dword index changed", (uint32_t)i);
                serial_trace_hex_value("WARNING", "boot log: IVT dword before", bootlog_ivt_before[i]);
                serial_trace_hex_value("WARNING", "boot log: IVT dword after", ivt[i]);
            }
            bootlog_ivt_ok = false;
            bootlog_ivt_bad_index = (uint8_t)i;
            return;
        }
    }
}

static void bootlog_rm_set(int field, uint32_t value) {
    volatile uint32_t *slot =
        (volatile uint32_t *)(void *)(bootlog_rm_regs() + rm_layout[field]);
    *slot = value;
}

static uint32_t bootlog_rm_get(int field) {
    const volatile uint32_t *slot =
        (const volatile uint32_t *)(const void *)(bootlog_rm_regs() + rm_layout[field]);
    return *slot;
}

/*
 * One firmware call through the bridge. The register file is cleared
 * first (which also zeroes the INT selector word), then the caller's
 * registers and the interrupt vector go in. Returns false when the call
 * itself faulted in real mode (the bridge's own report path).
 *
 * ECX and ES are carried because the CHS disk path needs CX (cylinder/
 * sector) and ES:BX (transfer buffer); the extended INT 13h path only
 * needs EAX/EBX/EDX/DS:SI and passes zeros here.
 */
static bool bootlog_rm_call(uint8_t vector, uint32_t eax, uint32_t ebx,
                            uint32_t ecx, uint32_t edx, uint32_t esi,
                            uint32_t es) {
    volatile uint32_t *regs = (volatile uint32_t *)(void *)bootlog_rm_regs();
    volatile uint16_t *vector_slot =
        (volatile uint16_t *)(void *)(bootlog_rm_regs() + rm_layout[RM_L_INT]);

    bootlog_rm_last_faulted = false;
    bootlog_rm_last_fault_vector = 0xFFu;
    bootlog_rm_last_ah = 0;

    for (int i = 0; i < 10; ++i) {
        regs[i] = 0; /* 0x00..0x27: EAX..ES plus the INT selector word */
    }
    bootlog_rm_set(RM_L_EAX, eax);
    bootlog_rm_set(RM_L_EBX, ebx);
    bootlog_rm_set(RM_L_ECX, ecx);
    bootlog_rm_set(RM_L_EDX, edx);
    bootlog_rm_set(RM_L_ESI, esi);
    bootlog_rm_set(RM_L_ES, es);
    *vector_slot = vector;
    bootlog_ivt_snapshot();
    ++bootlog_rm_calls;
    if (realmode_call() != 0) {
        bootlog_rm_last_faulted = true;
        bootlog_rm_last_fault_vector =
            *(const volatile uint8_t *)(uintptr_t)rm_layout[RM_L_FAULT];
        bootlog_ivt_verify();
        return false;
    }
    bootlog_ivt_verify();
    bootlog_rm_last_ah =
        (uint8_t)((bootlog_rm_get(RM_L_EAX) >> 8) & 0xFFu);
    return true;
}

/*
 * INT 13h extended read/write. The DAP is rebuilt for every call in the
 * bridge's low-memory scratch (RM_BUF): the VBE path only uses that
 * scratch while a VBE call is in flight, so the two never overlap.
 * Returns true on BIOS success (no fault, carry clear).
 */
static bool bootlog_int13_ext(uint8_t drive, bool write, uint16_t count,
                              uint32_t buf_seg, uint16_t buf_off, uint64_t lba) {
    volatile uint8_t *dap = (volatile uint8_t *)(uintptr_t)rm_layout[RM_L_BUF];

    if (count == 0 || count > 64u) {
        return false;
    }
    dap[0] = 0x10; /* DAP size */
    dap[1] = 0;    /* reserved */
    dap[2] = (uint8_t)count;
    dap[3] = 0;
    dap[4] = (uint8_t)buf_off;
    dap[5] = (uint8_t)(buf_off >> 8);
    dap[6] = (uint8_t)buf_seg;
    dap[7] = (uint8_t)(buf_seg >> 8);
    /* 64-bit absolute block number at offset 8 (writing it also clears
     * the two bytes some firmwares validate). */
    for (int i = 0; i < 8; ++i) {
        dap[8 + i] = (uint8_t)(lba >> (8 * i));
    }
    /* AH=42h read / AH=43h write with AL=0 (no verify); DS:SI -> DAP */
    if (!bootlog_rm_call(0x13, write ? 0x4300u : 0x4200u, 0, 0, drive,
                         rm_layout[RM_L_BUF], 0)) {
        return false;
    }
    return (bootlog_rm_get(RM_L_FLAGS) & 1u) == 0; /* carry */
}

/* The extension probe and legacy geometry probe may both be rejected by a
 * CSM.  Preserve each reply so the final diagnostic distinguishes a firmware
 * error return from a fault in the real-mode bridge. */
static bool bootlog_int13_ext_faulted;
static uint8_t bootlog_int13_ext_fault_vector;
static uint8_t bootlog_int13_ext_ah;
static bool bootlog_int13_chs_faulted;
static uint8_t bootlog_int13_chs_fault_vector;
static uint8_t bootlog_int13_chs_ah;

/*
 * INT 13h AH=41h: does this BIOS drive support the extended services?
 *
 * DS:SI must address a writable buffer: on success the BIOS stores its
 * 30-byte drive parameter table there.  The old call passed SI=0 with DS=0,
 * which is linear 0x0000 - the real-mode IVT.  A BIOS that honours the spec
 * therefore overwrote the first eight interrupt vectors (INT 08h the timer,
 * INT 09h the keyboard, ...) with a disk parameter table on every probe.
 * The return path then read CF and left the firmware's own handlers dead.
 */
static bool bootlog_int13_ext_present(uint8_t drive) {
    volatile uint8_t *param = (volatile uint8_t *)(uintptr_t)rm_layout[RM_L_BUF];

    for (int i = 0; i < 32; ++i) {
        param[i] = 0;
    }
    bootlog_int13_ext_faulted = false;
    bootlog_int13_ext_fault_vector = 0xFFu;
    bootlog_int13_ext_ah = 0;
    if (!bootlog_rm_call(0x13, 0x4100u, 0x55AAu, 0, drive,
                         rm_layout[RM_L_BUF], 0)) {
        bootlog_int13_ext_faulted = bootlog_rm_last_faulted;
        bootlog_int13_ext_fault_vector = bootlog_rm_last_fault_vector;
        return false;
    }
    bootlog_int13_ext_ah = bootlog_rm_last_ah;
    if ((bootlog_rm_get(RM_L_FLAGS) & 1u) != 0) {
        return false;
    }
    return (bootlog_rm_get(RM_L_EBX) & 0xFFFFu) == 0xAA55u;
}

/*
 * INT 13h AH=08h: the drive's CHS geometry.  A CSM that boots USB in
 * legacy mode can service AH=02h/03h even when it does not expose
 * AH=41h/42h; this is the only way to turn the FAT's absolute LBAs into
 * the cylinder/head/sector triple those calls take.  The answer is
 * cached per drive: discovery may ask about several drives before one
 * answers.
 */
static uint8_t bootlog_chs_drive = 0xFFu;
static bool bootlog_chs_valid;
static uint16_t bootlog_chs_heads;
static uint16_t bootlog_chs_spt;
static uint16_t bootlog_chs_max_cyl;
static bool bootlog_int13_ext_ok;
static bool bootlog_target_chs;

static bool bootlog_int13_chs_geometry(uint8_t drive) {
    uint32_t ecx;
    uint32_t edx;
    uint32_t max_cyl;
    uint32_t spt;
    uint32_t heads;

    if (bootlog_chs_drive == drive) {
        return bootlog_chs_valid;
    }
    bootlog_chs_drive = drive;
    bootlog_chs_valid = false;

    bootlog_int13_chs_faulted = false;
    bootlog_int13_chs_fault_vector = 0xFFu;
    bootlog_int13_chs_ah = 0;

    if (!bootlog_rm_call(0x13, 0x0800u, 0, 0, drive, 0, 0)) {
        bootlog_int13_chs_faulted = bootlog_rm_last_faulted;
        bootlog_int13_chs_fault_vector = bootlog_rm_last_fault_vector;
        return false;
    }
    bootlog_int13_chs_ah = bootlog_rm_last_ah;
    if ((bootlog_rm_get(RM_L_FLAGS) & 1u) != 0) {
        return false;
    }

    ecx = bootlog_rm_get(RM_L_ECX);
    edx = bootlog_rm_get(RM_L_EDX);
    spt = ecx & 0x3Fu;                         /* CL bits 0-5 */
    max_cyl = ((ecx & 0xC0u) << 2) | ((ecx >> 8) & 0xFFu);
    heads = ((edx >> 8) & 0xFFu) + 1u;         /* DH is the max head index */

    if (spt == 0 || heads == 0 || heads > 256u) {
        return false;
    }
    if (max_cyl > 1023u) {
        max_cyl = 1023u;                       /* CHS cannot name more */
    }
    bootlog_chs_spt = (uint16_t)spt;
    bootlog_chs_heads = (uint16_t)heads;
    bootlog_chs_max_cyl = (uint16_t)max_cyl;
    bootlog_chs_valid = true;
    return true;
}

/*
 * INT 13h AH=02h/03h: one sector at a time, ES:BX = the transfer window.
 * The caller has already staged or will copy out the 512 bytes there.
 * An LBA past the geometry's last cylinder is refused rather than
 * silently wrapped onto a different part of the stick.
 */
static bool bootlog_int13_chs_rw(uint8_t drive, bool write, uint32_t lba512) {
    uint32_t per_cylinder;
    uint32_t cylinder;
    uint32_t rest;
    uint32_t head;
    uint32_t sector;
    uint16_t cx;
    uint16_t dx;

    if (!bootlog_int13_chs_geometry(drive)) {
        return false;
    }
    per_cylinder = (uint32_t)bootlog_chs_heads * (uint32_t)bootlog_chs_spt;
    cylinder = lba512 / per_cylinder;
    rest = lba512 % per_cylinder;
    if (cylinder > bootlog_chs_max_cyl || cylinder > 1023u) {
        return false;
    }
    head = rest / bootlog_chs_spt;
    sector = rest % bootlog_chs_spt + 1u;

    /* CH = cylinder bits 0-7, CL bits 6-7 = cylinder bits 8-9,
     * CL bits 0-5 = sector (1-based). */
    cx = (uint16_t)(sector | ((cylinder & 0x300u) >> 2) |
                    ((cylinder & 0xFFu) << 8));
    dx = (uint16_t)((head << 8) | drive);

    if (!bootlog_rm_call(0x13, write ? 0x0301u : 0x0201u, 0, cx, dx, 0,
                         BOOTLOG_XFER_SEG)) {
        return false;
    }
    return (bootlog_rm_get(RM_L_FLAGS) & 1u) == 0;
}

static bool bootlog_int13_read_sector(uint8_t drive, uint32_t lba512, uint8_t *out) {
    volatile uint8_t *xfer = (volatile uint8_t *)BOOTLOG_XFER_LINEAR;
    bool ok = false;

    if (bootlog_target_chs) {
        ok = bootlog_int13_chs_rw(drive, false, lba512);
    }
    if (!ok && bootlog_int13_ext_ok) {
        ok = bootlog_int13_ext(drive, false, 1, BOOTLOG_XFER_SEG, 0, lba512);
    }
    if (!ok && !bootlog_target_chs) {
        ok = bootlog_int13_chs_rw(drive, false, lba512);
        if (ok) {
            bootlog_target_chs = true;
        }
    }
    if (!ok) {
        return false;
    }
    disk_io_add_bytes(512u);
    for (int i = 0; i < 512; ++i) {
        out[i] = xfer[i];
    }
    return true;
}

static bool bootlog_int13_write_sector(uint8_t drive, uint32_t lba512, const uint8_t *data) {
    volatile uint8_t *xfer = (volatile uint8_t *)BOOTLOG_XFER_LINEAR;
    bool ok = false;

    for (int i = 0; i < 512; ++i) {
        xfer[i] = data[i];
    }
    if (bootlog_target_chs) {
        ok = bootlog_int13_chs_rw(drive, true, lba512);
    }
    if (!ok && bootlog_int13_ext_ok) {
        ok = bootlog_int13_ext(drive, true, 1, BOOTLOG_XFER_SEG, 0, lba512);
    }
    if (!ok && !bootlog_target_chs) {
        ok = bootlog_int13_chs_rw(drive, true, lba512);
        if (ok) {
            bootlog_target_chs = true;
        }
    }
    if (ok) {
        disk_io_add_bytes(512u);
    }
    return ok;
}

/* ---- the persistence target (discovered once per boot) ---------------- */

enum {
    BOOTLOG_TARGET_NONE = 0,   /* no bios-serial.log target anywhere */
    BOOTLOG_TARGET_ATA = 1,    /* the disk image on primary master */
    BOOTLOG_TARGET_INT13 = 2  /* a dd'd ISO stick through BIOS INT 13h */
};

static uint8_t bootlog_target_kind;
static uint8_t bootlog_target_drive;
/* A firmware disk that is not the ATA disk the kernel can drive itself.
 * Writing to it means an INT 13h AH=43h call through the real-mode bridge,
 * which runs with interrupts disabled.  A USB BIOS stack may wait for an
 * interrupt that can never arrive, so the machine freezes before the video
 * mode is even set.  Reads are still used to diagnose the medium; writes
 * are refused. */
static bool bootlog_int13_write_unsafe;
static uint8_t bootlog_scan_attempts;
static uint32_t bootlog_last_scan_tick;
static const MultibootInfo *bootlog_boot_info;
static bool bootlog_bridge_warned;
/* Set once the medium is deliberately abandoned (a last-resort xHCI reset
 * or a restart handoff makes the firmware's INT 13h boot path unusable).
 * Discovery must stop then, or the auto-save tick would keep issuing disk
 * calls into a controller that no longer answers. */
static bool bootlog_detached;
/* Set once the video mode is programmed; see bootlog_firmware_disk_allowed. */
static bool bootlog_graphics_ready;
/* Cached CPUID hypervisor-present bit (-1 = not queried yet). */
static int8_t bootlog_hypervisor_state = -1;

/* Compact discovery result for the boot-menu status line.  On a machine
 * with no serial port this is the only way to see WHY the on-medium log
 * stayed empty; the same detail also goes to COM1. */
enum {
    BOOTLOG_REASON_NONE = 0,
    BOOTLOG_REASON_PROBING,
    BOOTLOG_REASON_NO_DRIVE,
    BOOTLOG_REASON_BRIDGE,
    BOOTLOG_REASON_NO_EXT,
    BOOTLOG_REASON_READ,
    BOOTLOG_REASON_NO_VOLUME,
    BOOTLOG_REASON_ATA_DIFFERENT,
    BOOTLOG_REASON_FIRMWARE_WRITE,
    BOOTLOG_REASON_FIRMWARE_WINDOW,
    BOOTLOG_REASON_WRITE,
    BOOTLOG_REASON_READ_ONLY
};

static uint8_t bootlog_reason = BOOTLOG_REASON_PROBING;
static bool bootlog_saved_ok;
static char bootlog_status_line[96];

/*
 * One record per firmware disk actually probed, kept for the boot-menu
 * diagnostics.  On a machine with no serial port this block is the only
 * channel that can say WHICH layer refused:
 *
 *   ext AH01   the firmware answered "no extended services"
 *   ext F08    the real-mode call faulted on vector 8
 *   rd no      the drive answered INT 13h but sector 0 would not read
 *   LOG no     the volume is readable but does not carry OUR validated file
 *
 * Only "LOG YES" means the file's hidden stamp matched and writes are safe.
 */
typedef struct {
    uint8_t drive;
    uint8_t reset_ah;
    uint8_t ext;        /* 1 when AH=41h reported extensions */
    uint8_t ext_ah;
    uint8_t ext_fault;  /* 0xFF when the call returned normally */
    uint8_t chs;        /* 1 when AH=08h gave a geometry */
    uint8_t chs_ah;
    uint8_t chs_fault;
    uint8_t read;       /* sector 0 readable */
    uint8_t ours;       /* the log volume's hidden stamp matched */
} BootlogProbe;

#define BOOTLOG_PROBE_MAX 4u
#define BOOTLOG_DIAG_LINES 6u
#define BOOTLOG_SCAN_ATTEMPTS 8u

static BootlogProbe bootlog_probes[BOOTLOG_PROBE_MAX];
static uint8_t bootlog_probe_count;
static char bootlog_diag_lines[BOOTLOG_DIAG_LINES][96];
static uint8_t bootlog_diag_count;
static bool bootlog_bridge_ready;
static bool bootlog_bios_kbd_tested;
static bool bootlog_bios_kbd_ok;
static bool bootlog_usb_hid_ready;
static bool bootlog_usb_scanned;
static bool bootlog_have_boot_mbr;
static uint8_t bootlog_boot_mbr[512];

static const char *bootlog_reason_text(uint8_t reason) {
    switch (reason) {
    case BOOTLOG_REASON_NO_DRIVE:
        return "no drive from loader";
    case BOOTLOG_REASON_BRIDGE:
        return "realmode window busy";
    case BOOTLOG_REASON_NO_EXT:
        return "INT13 ext/CHS absent";
    case BOOTLOG_REASON_READ:
        return "INT13 read failed";
    case BOOTLOG_REASON_NO_VOLUME:
        return "no log volume";
    case BOOTLOG_REASON_ATA_DIFFERENT:
        return "ATA is not boot disk";
    case BOOTLOG_REASON_FIRMWARE_WRITE:
        return "firmware disk: not boot drive";
    case BOOTLOG_REASON_FIRMWARE_WINDOW:
        return "firmware writes paused";
    case BOOTLOG_REASON_WRITE:
        return "write failed";
    case BOOTLOG_REASON_READ_ONLY:
        return "read-only boot medium";
    case BOOTLOG_REASON_NONE:
        return "ok";
    default:
        return "probing";
    }
}

/*
 * Human-readable discovery/save state, rebuilt from the live fields.
 * Called by the boot menu on every repaint; release builds get "".
 */
static const char *serial_bootlog_status_text(void);

/* FAT geometry of the partition holding the log file, plus the resolved
 * cluster chain and the retained directory-entry and stamp sectors. */
static uint32_t bootlog_fat_fat_lba;
static uint32_t bootlog_fat_data_lba;
static uint16_t bootlog_fat_spc;
static bool bootlog_fat_is12;
static uint32_t bootlog_chain_lba[BOOTLOG_FAT_MAX_CLUSTERS];
static uint8_t bootlog_chain_clusters;
static uint32_t bootlog_capacity;          /* chain bytes (spc*512*clusters) */
static uint32_t bootlog_dir_lba;
static uint16_t bootlog_dir_off;
static uint8_t bootlog_dir_sector[512];    /* retained; the size field is patched */
static uint8_t bootlog_stamp[512];         /* hidden tail stamp, retained */
static uint32_t bootlog_stamp_lba;

/* Delta-sync bookkeeping: how much of the RAM mirror is already on the
 * medium. Only sectors past this point are written out. */
static uint32_t bootlog_region_written;
static bool bootlog_boot_counted;       /* stamp's boot_count bumped once */
static bool bootlog_autosave_armed;     /* one-time auto-save trace */
static uint32_t bootlog_last_sync_tick;

static const char *serial_bootlog_status_text(void) {
    size_t len = 0;
    size_t max = sizeof(bootlog_status_line);

    bootlog_status_line[0] = '\0';
    if (bootlog_target_kind == BOOTLOG_TARGET_NONE) {
        append_string(bootlog_status_line, &len, max, "LOG none raw ");
        if (boot_device_raw_valid) {
            append_hex32(bootlog_status_line, &len, max, boot_device_raw);
        } else {
            append_string(bootlog_status_line, &len, max, "0x????????");
        }
        append_string(bootlog_status_line, &len, max, " drv ");
        if (boot_drive_valid) {
            append_string(bootlog_status_line, &len, max, "0x");
            append_hex8(bootlog_status_line, &len, max, boot_drive_number);
        } else {
            append_string(bootlog_status_line, &len, max, "none");
        }
        append_string(bootlog_status_line, &len, max, ": ");
        append_string(bootlog_status_line, &len, max,
                      bootlog_reason_text(bootlog_reason));
        /* The generic reason identifies the failed layer; the compact
         * per-function status says whether the firmware declined the call
         * (AH) or the real-mode bridge caught a fault (F).  This is kept on
         * the VGA line because many current PCs expose neither COM1 nor a
         * usable USB-to-serial adapter during early boot. */
        if (bootlog_reason == BOOTLOG_REASON_NO_EXT) {
            append_string(bootlog_status_line, &len, max, " E41:");
            if (bootlog_int13_ext_faulted) {
                append_string(bootlog_status_line, &len, max, "F");
                append_hex8(bootlog_status_line, &len, max,
                            bootlog_int13_ext_fault_vector);
            } else {
                append_hex8(bootlog_status_line, &len, max,
                            bootlog_int13_ext_ah);
            }
            append_string(bootlog_status_line, &len, max, " C08:");
            if (bootlog_int13_chs_faulted) {
                append_string(bootlog_status_line, &len, max, "F");
                append_hex8(bootlog_status_line, &len, max,
                            bootlog_int13_chs_fault_vector);
            } else {
                append_hex8(bootlog_status_line, &len, max,
                            bootlog_int13_chs_ah);
            }
        }
        return bootlog_status_line;
    }

    append_string(bootlog_status_line, &len, max, "LOG ");
    if (bootlog_target_kind == BOOTLOG_TARGET_INT13) {
        append_string(bootlog_status_line, &len, max, "INT13 0x");
        append_hex8(bootlog_status_line, &len, max, bootlog_target_drive);
        if (bootlog_target_chs) {
            append_string(bootlog_status_line, &len, max, " chs");
        }
    } else {
        append_string(bootlog_status_line, &len, max, "ATA");
    }
    append_string(bootlog_status_line, &len, max, " raw ");
    if (boot_device_raw_valid) {
        append_hex32(bootlog_status_line, &len, max, boot_device_raw);
    } else {
        append_string(bootlog_status_line, &len, max, "0x????????");
    }
    append_string(bootlog_status_line, &len, max, " saved ");
    append_uint(bootlog_status_line, &len, max, bootlog_region_written);
    append_string(bootlog_status_line, &len, max, "B ");
    append_string(bootlog_status_line, &len, max,
                  bootlog_saved_ok ? "ok" : bootlog_reason_text(bootlog_reason));
    return bootlog_status_line;
}

/*
 * The diagnostics panel the boot menu draws. Built from the probe records
 * above (so a photograph of a hung machine carries the answer) and one PCI
 * scan that reports which USB host-controller class is present. This is a
 * diagnostic only: PCI enumeration does not establish whether firmware has
 * routed a USB keyboard through 8042 legacy emulation, and this kernel does
 * not yet contain a native USB HID input driver.
 */
static void bootlog_build_diagnostics(void) {
    static char usb_line[96];

    bootlog_diag_count = 0;
    for (uint8_t i = 0; i < bootlog_probe_count &&
                    bootlog_diag_count < (BOOTLOG_DIAG_LINES - 1u);
         ++i) {
        BootlogProbe *p = &bootlog_probes[i];
        char *line = bootlog_diag_lines[bootlog_diag_count];
        size_t len = 0;

        line[0] = '\0';
        append_string(line, &len, 96, "INT13 0x");
        append_hex8(line, &len, 96, p->drive);
        if (p->ext) {
            append_string(line, &len, 96, " ext ok");
        } else if (p->ext_fault != 0xFFu) {
            append_string(line, &len, 96, " ext F");
            append_hex8(line, &len, 96, p->ext_fault);
        } else {
            append_string(line, &len, 96, " ext AH");
            append_hex8(line, &len, 96, p->ext_ah);
        }
        if (!p->ext) {
            if (p->chs) {
                append_string(line, &len, 96, " chs ok");
            } else if (p->chs_fault != 0xFFu) {
                append_string(line, &len, 96, " chs F");
                append_hex8(line, &len, 96, p->chs_fault);
            } else {
                append_string(line, &len, 96, " chs AH");
                append_hex8(line, &len, 96, p->chs_ah);
            }
        }
        append_string(line, &len, 96, p->read ? " rd ok" : " rd no");
        append_string(line, &len, 96, p->ours ? " LOG YES" : " LOG no");
        ++bootlog_diag_count;
    }

    if (!bootlog_usb_scanned) {
        size_t len = 0;
        bool found = false;
        static const char *const names[4] = {"uhci", "ohci", "ehci", "xhci"};

        bootlog_usb_scanned = true;
        usb_line[0] = '\0';
        append_string(usb_line, &len, 96, "USB");
        /* PCI/PCIe systems can put host controllers behind bridges on bus
         * numbers above 7. The old 0..7-only scan falsely reported "none"
         * on such machines, which misled keyboard bring-up diagnostics. */
        for (uint16_t bus = 0; bus < 256u; ++bus) {
            for (uint8_t slot = 0; slot < 32; ++slot) {
                for (uint8_t fn = 0; fn < 8; ++fn) {
                    uint32_t id = pci_config_read32((uint8_t)bus, slot, fn, 0x00);
                    uint32_t cls;
                    uint8_t prog_if;
                    uint8_t kind;

                    if (id == 0xFFFFFFFFu) {
                        if (fn == 0) {
                            break;
                        }
                        continue;
                    }
                    cls = pci_config_read32((uint8_t)bus, slot, fn, 0x08);
                    if (((cls >> 24) & 0xFFu) != 0x0Cu ||
                        ((cls >> 16) & 0xFFu) != 0x03u) {
                        continue;
                    }
                    prog_if = (uint8_t)((cls >> 8) & 0xFFu);
                    kind = (uint8_t)(prog_if >> 4);
                    if (kind >= 4u) {
                        continue;
                    }
                    found = true;
                    append_string(usb_line, &len, 96, " ");
                    append_string(usb_line, &len, 96, names[kind]);
                    append_string(usb_line, &len, 96, " ");
                    append_hex8(usb_line, &len, 96, (uint8_t)bus);
                    append_string(usb_line, &len, 96, ":");
                    append_hex8(usb_line, &len, 96, slot);
                    append_string(usb_line, &len, 96, ".");
                    {
                        char fn_text[2];
                        fn_text[0] = (char)('0' + (fn & 7u));
                        fn_text[1] = '\0';
                        append_string(usb_line, &len, 96, fn_text);
                    }
                    append_string(usb_line, &len, 96,
                                  bootlog_usb_hid_ready ? " HID ready" :
                                                          " (no HID driver)");
                    goto usb_scan_done;
                }
            }
        }
usb_scan_done:
        if (!found) {
            append_string(usb_line, &len, 96, " none found in PCI[0-255]");
        }
    }
    if (bootlog_diag_count < BOOTLOG_DIAG_LINES) {
        size_t len = 0;
        char *line = bootlog_diag_lines[bootlog_diag_count];
        line[0] = '\0';
        append_string(line, &len, 96, usb_line);
        ++bootlog_diag_count;
    }
    if (bootlog_diag_count < BOOTLOG_DIAG_LINES) {
        size_t len = 0;
        char *line = bootlog_diag_lines[bootlog_diag_count];
        line[0] = '\0';
        append_string(line, &len, 96, "BRIDGE ");
        if (realmode_stack_bytes() != 0) {
            append_uint(line, &len, 96, realmode_stack_bytes() / 1024u);
            append_string(line, &len, 96, "K stack");
        } else {
            append_string(line, &len, 96, "not armed");
        }
        if (bootlog_ivt_ok) {
            append_string(line, &len, 96, " IVT ok");
        } else {
            append_string(line, &len, 96, " IVT CHG@");
            append_hex8(line, &len, 96, bootlog_ivt_bad_index);
        }
        if (bootlog_bios_kbd_tested) {
            append_string(line, &len, 96, bootlog_bios_kbd_ok ? " INT16 ok" : " INT16 F");
        }
        ++bootlog_diag_count;
    }
}

const char *serial_bootlog_diag_line(uint8_t index) {
    return index < bootlog_diag_count ? bootlog_diag_lines[index] : "";
}

/* Called by the native xHCI HID driver once it has armed a keyboard
 * endpoint.  The USB diagnostic line is cached after its PCI scan, so mark
 * it dirty and rebuild it; the boot menu then shows the live state instead
 * of the first-frame "(no HID driver)" text. */
void serial_bootlog_set_usb_hid_ready(bool ready) {
    if (bootlog_usb_hid_ready == ready) {
        return;
    }
    bootlog_usb_hid_ready = ready;
    bootlog_usb_scanned = false;
    bootlog_build_diagnostics();
    if (system_state == STATE_BOOT_MENU) {
        boot_menu_dirty = true;
    }
}

/* True only when the firmware's INT 16h service answered the probe.  The
 * native xHCI driver is installed only when neither this nor the 8042 path
 * is usable, so a machine with a working firmware keyboard is left alone. */
bool serial_bootlog_bios_keyboard_usable(void) {
    return bootlog_bios_kbd_tested && bootlog_bios_kbd_ok;
}

/*
 * Read one key from the firmware's own keyboard services (INT 16h).
 *
 * This is the fallback for a USB keyboard that the firmware injects through
 * its legacy path instead of through the 8042: when no scancode ever arrives
 * at port 0x60, the BIOS's keyboard buffer can still have the key.  It is
 * only consulted while the 8042 path has produced nothing (see
 * poll_input), so a machine whose PS/2 path works never double-reads.
 */
static uint32_t bootlog_bios_key_count;

/* Is the firmware's keyboard service callable at all?  Asked once, right
 * after the bridge is armed: a fault here is reported on the boot menu, and
 * it tells a machine whose INT 16h is unusable from one where the buffer is
 * merely empty. */
static void bootlog_bios_keyboard_probe(void) {
    if (bootlog_bios_kbd_tested || !bootlog_bridge_ready) {
        return;
    }
    bootlog_bios_kbd_tested = true;
    bootlog_bios_kbd_ok = bootlog_rm_call(0x16, 0x0100u, 0, 0, 0, 0, 0);
    if (bootlog_bios_kbd_ok) {
        serial_trace("INFO", "input: firmware INT 16h keyboard services present");
    } else {
        serial_trace("WARNING", "input: firmware INT 16h keyboard services unusable");
    }
}


bool serial_bootlog_bios_key_poll(uint8_t *ascii, uint8_t *scan) {
    uint32_t flags;
    uint32_t eax;

    if (!bootlog_bridge_ready || !bootlog_bios_kbd_ok) {
        return false;
    }
    /* AH=01h: is a key waiting?  ZF=1 (and CF on some BIOSes) means empty;
     * calling AH=00h then would block the whole machine. */
    if (!bootlog_rm_call(0x16, 0x0100u, 0, 0, 0, 0, 0)) {
        return false;
    }
    flags = bootlog_rm_get(RM_L_FLAGS);
    if ((flags & 0x40u) != 0 || (flags & 0x01u) != 0) {
        return false;
    }
    if (!bootlog_rm_call(0x16, 0x0000u, 0, 0, 0, 0, 0)) {
        return false;
    }
    eax = bootlog_rm_get(RM_L_EAX);
    if (ascii != NULL) {
        *ascii = (uint8_t)(eax & 0xFFu);
    }
    if (scan != NULL) {
        *scan = (uint8_t)((eax >> 8) & 0xFFu);
    }
    ++bootlog_bios_key_count;
    return true;
}

uint32_t serial_bootlog_bios_key_count(void) {
    return bootlog_bios_key_count;
}

static void bootlog_build_diagnostics(void);

/* A staged save changes the byte count under the boot menu; repaint it
 * so the line the user photographs is not a stale first-frame value. */
static void bootlog_status_changed(void) {
    bootlog_build_diagnostics();
    if (system_state == STATE_BOOT_MENU) {
        boot_menu_dirty = true;
    }
}


static bool bootlog_target_read_sector(uint32_t lba, uint8_t *out) {
    if (bootlog_target_kind == BOOTLOG_TARGET_ATA) {
        return ata_pio_read_sector(lba, out);
    }
    return bootlog_int13_read_sector(bootlog_target_drive, lba, out);
}

static bool bootlog_target_write_sector(uint32_t lba, const uint8_t *data) {
    if (bootlog_target_kind == BOOTLOG_TARGET_ATA) {
        return ata_pio_write_sector(lba, data);
    }
    /* Last line of defence: only the validated log volume on the loader's
     * own boot drive may be written through firmware. */
    if (bootlog_int13_write_unsafe) {
        return false;
    }
    return bootlog_int13_write_sector(bootlog_target_drive, lba, data);
}

/*
 * Make the firmware release its cached view of the drive.  INT 13h
 * writes can sit in a USB stick's/BIOS's write-back cache until the
 * next reset; on a real machine that is exactly the case where the
 * file looks complete on the machine that wrote it and blank when the
 * stick is moved to another one.  AH=00h resets the disk system.  ATA
 * writes already issue FLUSH CACHE per sector (ata.c).
 */
static void bootlog_target_reset(void) {
    /* Nothing to do: see bootlog_int13_reset.  The AH=00h reset cannot be
     * issued from the bridge, and the per-sector read-back in
     * bootlog_target_write_sector_verified already proves that each write
     * reached the medium.  ATA writes flush in ata.c. */
}

/*
 * Write one sector and read it back.  A BIOS can report success for a
 * write that never reaches the medium (a vanished USB stick, a write-
 * protected adapter, a cache that drops the sector); the read-back is
 * what turns that into a retry instead of a silently truncated log.
 * Three attempts, resetting the drive between them.
 */
static bool bootlog_target_write_sector_verified(uint32_t lba, const uint8_t *data) {
    uint8_t check[512];

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (bootlog_target_write_sector(lba, data)) {
            if (bootlog_target_read_sector(lba, check)) {
                bool same = true;
                for (int i = 0; i < 512; ++i) {
                    if (check[i] != data[i]) {
                        same = false;
                        break;
                    }
                }
                if (same) {
                    return true;
                }
            }
        }
        bootlog_target_reset();
    }
    return false;
}

/*
 * One 12-bit FAT entry. The 12-bit entries straddle byte boundaries, so
 * an odd entry at the very end of a cached sector needs one byte from
 * the next FAT sector.
 */
static bool bootlog_fat12_entry(uint16_t cluster, uint32_t *next) {
    uint32_t byte_off = (uint32_t)cluster + ((uint32_t)cluster >> 1);
    uint32_t fat_lba = bootlog_fat_fat_lba + byte_off / 512u;
    uint8_t sec[512];
    uint32_t in = byte_off % 512u;
    uint32_t b0;
    uint32_t b1;

    if (!bootlog_target_read_sector(fat_lba, sec)) {
        return false;
    }
    b0 = sec[in];
    if (in + 1u < 512u) {
        b1 = sec[in + 1u];
    } else {
        if (!bootlog_target_read_sector(fat_lba + 1u, sec)) {
            return false;
        }
        b1 = sec[0];
    }
    if ((cluster & 1u) != 0) {
        *next = ((b0 >> 4) | (b1 << 4)) & 0x0FFFu;
    } else {
        *next = (b0 | (b1 << 8)) & 0x0FFFu;
    }
    return true;
}

static bool bootlog_fat16_entry(uint16_t cluster, uint32_t *next) {
    uint32_t byte_off = (uint32_t)cluster * 2u;
    uint8_t sec[512];
    if (!bootlog_target_read_sector(bootlog_fat_fat_lba + byte_off / 512u, sec)) {
        return false;
    }
    *next = (uint32_t)sec[byte_off % 512u] |
            ((uint32_t)sec[(byte_off % 512u) + 1u] << 8);
    return true;
}

/*
 * Walk the file's cluster chain once at discovery and cache the absolute
 * LBA of every cluster's first sector; the sync path then never touches
 * the FAT again.
 */
static bool bootlog_fat_walk(uint16_t first_cluster) {
    uint32_t c = first_cluster;
    uint8_t n = 0;

    if (first_cluster < 2) {
        return false;
    }
    while (n < BOOTLOG_FAT_MAX_CLUSTERS) {
        uint32_t next;
        bootlog_chain_lba[n] = bootlog_fat_data_lba + (c - 2u) * bootlog_fat_spc;
        ++n;
        if (bootlog_fat_is12) {
            if (!bootlog_fat12_entry(c, &next)) {
                return false;
            }
            if (next >= 0xFF8u) {
                break;
            }
        } else {
            if (!bootlog_fat16_entry(c, &next)) {
                return false;
            }
            if (next >= 0xFFF8u) {
                break;
            }
        }
        if (next < 2 || next > 0xFFF0u) {
            return false;
        }
        c = next;
    }

    bootlog_chain_clusters = n;
    bootlog_capacity = (uint32_t)n * (uint32_t)bootlog_fat_spc * 512u;
    return bootlog_capacity >= 4096u + 512u;
}

/*
 * Find the *.LOG root-directory entry (the volume ships exactly one:
 * the bios-serial.log template; its long name lives in LFN entries the
 * short extension still ends in LOG). The whole entry's sector is
 * retained so the size field can be patched on every sync.
 */
static bool bootlog_fat_find_entry(const uint8_t *bpb_sector, uint32_t part_lba) {
    uint16_t bytes_per_sector = (uint16_t)bpb_sector[11] | ((uint16_t)bpb_sector[12] << 8);
    uint16_t reserved = (uint16_t)bpb_sector[14] | ((uint16_t)bpb_sector[15] << 8);
    uint8_t nfats = bpb_sector[16];
    uint16_t root_entries = (uint16_t)bpb_sector[17] | ((uint16_t)bpb_sector[18] << 8);
    uint16_t fat_size = (uint16_t)bpb_sector[22] | ((uint16_t)bpb_sector[23] << 8);
    uint16_t total16 = (uint16_t)bpb_sector[19] | ((uint16_t)bpb_sector[20] << 8);
    uint32_t total32 = bootlog_get32(bpb_sector + 32);
    uint32_t total_sectors = total16 != 0 ? total16 : total32;
    uint32_t root_lba;
    uint32_t root_sectors;
    uint32_t data_sectors;
    uint32_t clusters;
    uint8_t sec[512];

    /* Only FAT12/16: a zero 16-bit FAT size field at offset 22 is the
     * FAT32 signature (its FAT size lives at 36). These FAT32 volumes
     * are deliberately not touched. Note bytes 36.. are the drive
     * number / ext boot signature here, NOT a root-cluster field. */
    if (bytes_per_sector != 512u || fat_size == 0 ||
        nfats == 0 || root_entries == 0 || total_sectors == 0) {
        return false;
    }

    bootlog_fat_spc = bpb_sector[13];
    if (bootlog_fat_spc == 0 || (bootlog_fat_spc & (bootlog_fat_spc - 1u)) != 0) {
        return false;
    }

    bootlog_fat_fat_lba = part_lba + reserved;
    root_lba = bootlog_fat_fat_lba + (uint32_t)nfats * fat_size;
    root_sectors = ((uint32_t)root_entries * 32u + 511u) / 512u;
    bootlog_fat_data_lba = root_lba + root_sectors;

    data_sectors = total_sectors - reserved - (uint32_t)nfats * fat_size - root_sectors;
    clusters = data_sectors / bootlog_fat_spc;
    if (clusters < 2 || clusters > 65524u) {
        return false;
    }
    bootlog_fat_is12 = clusters < 4085u;

    for (uint32_t r = 0; r < root_sectors; ++r) {
        if (!bootlog_target_read_sector(root_lba + r, sec)) {
            return false;
        }
        for (uint32_t e = 0; e < 16; ++e) {
            const uint8_t *entry = sec + e * 32u;
            uint8_t attr = entry[11];
            uint16_t first_cluster;
            uint32_t size;

            if (entry[0] == 0x00) {
                return false; /* end of directory: no log file */
            }
            if (entry[0] == 0xE5) {
                continue; /* deleted */
            }
            if ((attr & 0x3Fu) == 0x0Fu) {
                continue; /* LFN entry */
            }
            if ((attr & 0x08u) != 0) {
                continue; /* volume label */
            }
            /* extension bytes 8..10: "LOG" (mtools uppercases) */
            if ((entry[8] != 'L' && entry[8] != 'l') ||
                (entry[9] != 'O' && entry[9] != 'o') ||
                (entry[10] != 'G' && entry[10] != 'g')) {
                continue;
            }
            first_cluster = (uint16_t)entry[26] | ((uint16_t)entry[27] << 8);
            size = bootlog_get32(entry + 28);
            if (size > (uint32_t)BOOTLOG_FAT_MAX_CLUSTERS * 128u * 512u) {
                continue;
            }

            bootlog_dir_lba = root_lba + r;
            bootlog_dir_off = (uint16_t)(e * 32u);
            for (int i = 0; i < 512; ++i) {
                bootlog_dir_sector[i] = sec[i];
            }
            return bootlog_fat_walk(first_cluster);
        }
    }
    return false;
}

/* Probe one MBR partition as the log volume: BPB -> root dir -> the
 * *.LOG entry -> cluster chain -> hidden tail stamp. Every level must
 * validate before the target is armed. */
static bool bootlog_fat_probe_partition(uint32_t part_lba) {
    uint8_t sec[512];
    uint32_t stamp_cluster;
    uint32_t stamp_in;

    if (part_lba == 0 || !bootlog_target_read_sector(part_lba, sec)) {
        return false;
    }
    if (((uint16_t)sec[510] | ((uint16_t)sec[511] << 8)) != 0xAA55u) {
        return false;
    }
    if (!bootlog_fat_find_entry(sec, part_lba)) {
        return false;
    }

    /* The hidden stamp: the LAST 512 bytes of the file's allocation. */
    stamp_cluster = (bootlog_capacity - 512u) /
                    ((uint32_t)bootlog_fat_spc * 512u);
    stamp_in = (bootlog_capacity - 512u) %
               ((uint32_t)bootlog_fat_spc * 512u);
    if (stamp_cluster >= bootlog_chain_clusters) {
        return false;
    }
    bootlog_stamp_lba = bootlog_chain_lba[stamp_cluster] + stamp_in / 512u;
    if (!bootlog_target_read_sector(bootlog_stamp_lba, bootlog_stamp) ||
        !bootlog_stamp_valid(bootlog_stamp, bootlog_capacity)) {
        return false;
    }
    return true;
}

/* Scan an MBR's primary entries and probe every non-extended candidate.
 * The type byte is advisory only (the disk image's own partition says
 * 0x83), so content decides: a partition without the validated chain
 * and stamp can never be armed. */
static bool bootlog_fat_probe_mbr(void) {
    uint8_t mbr[512];

    if (!bootlog_target_read_sector(0, mbr)) {
        return false;
    }
    if (((uint16_t)mbr[510] | ((uint16_t)mbr[511] << 8)) != 0xAA55u) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        const uint8_t *entry = mbr + 446u + (uint32_t)i * 16u;
        uint8_t type = entry[4];
        uint32_t start = bootlog_get32(entry + 8);
        uint32_t size = bootlog_get32(entry + 12);

        if (type == 0x00 || type == 0x05 || type == 0x0F ||
            type == 0xEE || type == 0xEF) {
            continue; /* empty / extended / GPT protective */
        }
        if (start == 0 || size == 0) {
            continue;
        }
        if (bootlog_fat_probe_partition(start)) {
            return true;
        }
    }
    return false;
}

static bool bootlog_mbr_equal(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 512; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

/*
 * Prefer the kernel's own polled ATA driver when the firmware boot disk is
 * demonstrably the same ATA disk.  That path is always safe and keeps
 * auto-saving in every state.  Everything else - a USB stick, a SCSI/SAS
 * disk behind a firmware driver, an AHCI disk this kernel does not drive -
 * has no ATA path and is settled by bootlog_settle_int13_target().
 */
static bool bootlog_prefer_ata_target(void) {
    uint8_t ata_mbr[512];

    if (!bootlog_have_boot_mbr) {
        return false;
    }
    if (!ata_pio_read_sector(0, ata_mbr) ||
        !bootlog_mbr_equal(bootlog_boot_mbr, ata_mbr)) {
        return false;
    }
    bootlog_target_kind = BOOTLOG_TARGET_ATA;
    if (bootlog_fat_probe_mbr()) {
        return true;
    }
    /* The MBR matched but the ATA path could not validate the log volume.
     * Keep the firmware target for diagnostics; settle decides whether its
     * write path is allowed. */
    bootlog_target_kind = BOOTLOG_TARGET_INT13;
    (void)bootlog_fat_probe_mbr();
    return false;
}

/*
 * The INT 13h path uses the same real-mode bridge as the VBE layer: the
 * register file, DAP and 16-bit stub live at 0x8000..0x9F00, and sectors are
 * staged through 0x20000..0x28000.  GRUB can leave its multiboot structure,
 * memory map, module list or whole EBDA in those ranges, and overwriting them
 * is a machine that dies with nothing on screen.  The VBE probe has always
 * checked this before its first call; the boot log now runs BEFORE the video
 * mode set, so it has to make the same check itself.  If either window is
 * occupied, refuse the firmware path and keep the boot intact.
 */
static bool bootlog_int13_low_memory_is_free(void) {
    if (!realmode_range_is_free(bootlog_boot_info,
                                rm_layout[RM_L_BASE],
                                rm_layout[RM_L_STACK]) ||
        !realmode_range_is_free(bootlog_boot_info,
                                BOOTLOG_XFER_LINEAR,
                                BOOTLOG_XFER_LINEAR + BOOTLOG_XFER_BYTES)) {
        if (!bootlog_bridge_warned) {
            bootlog_bridge_warned = true;
            serial_trace("WARNING", "boot log: low-memory bridge block overlaps live boot data; not using INT 13h");
        }
        return false;
    }
    /* Pick a real-mode stack large enough for a firmware USB stack, in a
     * window the ownership check above considers free.  The bridge is only
     * used after this, so the choice is final for the boot. */
    realmode_configure_stack(bootlog_boot_info);
    return true;
}

/* INT 13h AH=00h: reset the disk system.
 *
 * Deliberately never issued.  The real-mode bridge runs every firmware call
 * with interrupts disabled (realmode.asm clears IF before the mode switch and
 * the stub never sets it again), so a BIOS disk call that waits for the
 * controller's interrupt can never finish.  VMware's PhoenixBIOS does exactly
 * that in AH=00h: the call never returns, the boot menu keeps its last painted
 * frame, the main loop stops at the first auto-save and every key is ignored -
 * field reports read as "the keyboard died at the boot menu".  It cost one
 * boot on real hardware and one in VMware before the serial trace showed the
 * loop stopping right after the reset.
 *
 * The reset is only a cache-flush and stale-error hint.  Every write is read
 * back sector by sector (bootlog_target_write_sector_verified), so losing the
 * reset costs a flush after an unclean power cut, not correctness or, worse,
 * the whole machine.
 */
static uint8_t bootlog_int13_reset(uint8_t drive) {
    (void)drive;
    return 0xFFu; /* not attempted: firmware calls cannot wait for IRQs */
}

/*
 * Probe one firmware disk and say what happened:
 *   0  no INT 13h access at all
 *   1  INT 13h works, but this is not our log volume
 *   2  INT 13h works and the validated log file was found
 * The probe record is kept for the boot-menu panel (up to BOOTLOG_PROBE_MAX;
 * probes past that still run, they just are not displayed).
 */
static uint8_t bootlog_probe_drive(uint8_t drive) {
    BootlogProbe scratch;
    BootlogProbe *p = &scratch;
    uint8_t mbr[512];
    bool record = bootlog_probe_count < BOOTLOG_PROBE_MAX;

    for (uint32_t i = 0; i < sizeof(scratch); ++i) {
        ((uint8_t *)&scratch)[i] = 0;
    }
    p->drive = drive;
    p->ext_fault = 0xFFu;
    p->chs_fault = 0xFFu;
    if (record) {
        p = &bootlog_probes[bootlog_probe_count++];
        *p = scratch;
    }

    p->reset_ah = bootlog_int13_reset(drive);

    bootlog_int13_ext_ok = bootlog_int13_ext_present(drive);
    p->ext = bootlog_int13_ext_ok ? 1u : 0u;
    p->ext_ah = bootlog_int13_ext_ah;
    if (bootlog_int13_ext_faulted) {
        p->ext_fault = bootlog_int13_ext_fault_vector;
    }
    if (!p->ext) {
        p->chs = bootlog_int13_chs_geometry(drive) ? 1u : 0u;
        p->chs_ah = bootlog_int13_chs_ah;
        if (bootlog_int13_chs_faulted) {
            p->chs_fault = bootlog_int13_chs_fault_vector;
        }
    }
    if (!p->ext && !p->chs) {
        return 0;
    }

    bootlog_target_kind = BOOTLOG_TARGET_INT13;
    bootlog_target_drive = drive;
    bootlog_target_chs = !p->ext;
    p->read = bootlog_target_read_sector(0, mbr) ? 1u : 0u;
    if (!p->read) {
        return 1;
    }
    if (boot_drive_valid && drive == boot_drive_number) {
        for (int i = 0; i < 512; ++i) {
            bootlog_boot_mbr[i] = mbr[i];
        }
        bootlog_have_boot_mbr = true;
    }
    p->ours = bootlog_fat_probe_mbr() ? 1u : 0u;
    return p->ours ? 2u : 1u;
}

/* Scan every firmware hard disk, skipping the one already tried.  A USB
 * stick can be 0x81 or higher (an internal NVMe/SSD takes 0x80), and a
 * firmware can report a stale number for a USB 3.0 handoff, so "the drive
 * GRUB named" is a starting point, not the whole search. */
static uint8_t bootlog_scan_int13_disks(uint8_t skip_drive) {
    uint8_t any = 0;

    for (uint8_t drive = 0x80; drive <= 0x87; ++drive) {
        uint8_t result;
        if (drive == skip_drive) {
            continue;
        }
        result = bootlog_probe_drive(drive);
        if (result == 2u) {
            return 2u;
        }
        if (result != 0u) {
            any = 1u;
        }
    }
    return any;
}

/*
 * Is the CPU running under a hypervisor?  The distinction matters here:
 * VMware's PhoenixBIOS answers firmware disk calls from the text-mode boot
 * menu but can wait forever on the next one after its SVGA mode is set.
 * Bare-metal firmware does not show that failure in practice.
 */
static bool bootlog_is_virtualized(void) {
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    if (bootlog_hypervisor_state < 0) {
        bootlog_hypervisor_state = 0;
        if (cpuid_is_available()) {
            cpuid_query(1, &eax, &ebx, &ecx, &edx);
            if ((ecx & (1u << 31)) != 0) {
                bootlog_hypervisor_state = 1;
            }
        }
    }
    return bootlog_hypervisor_state == 1;
}

/* The only firmware disk this writer may write: the validated log volume on
 * the drive the loader booted from. */
static bool bootlog_int13_boot_drive_target(void) {
    return bootlog_target_kind == BOOTLOG_TARGET_INT13 &&
           !bootlog_int13_write_unsafe &&
           boot_drive_valid &&
           bootlog_target_drive == boot_drive_number;
}

/*
 * A firmware disk answered with the validated log volume.  If it is the
 * drive the loader booted from, it is the only write path a USB boot has:
 * arm it.  A firmware disk that is NOT the boot drive is left read-only, so
 * an internal disk carrying an old template can never be written by a USB
 * boot.
 *
 * The real-mode bridge masks interrupts for the call, and a hypervisor's
 * firmware USB stack can wait forever for a controller IRQ.  The write
 * policy in bootlog_firmware_disk_allowed() therefore keeps a virtualized
 * boot on the pre-graphics and text-mode path, while bare metal keeps
 * writing through the desktop and the final flush.
 */
static void bootlog_settle_int13_target(void) {
    if (bootlog_prefer_ata_target()) {
        bootlog_reason = BOOTLOG_REASON_NONE;
        bootlog_status_changed();
        return;
    }
    if (bootlog_have_boot_mbr && boot_drive_valid &&
        bootlog_target_drive == boot_drive_number) {
        bootlog_int13_write_unsafe = false;
        bootlog_target_kind = BOOTLOG_TARGET_INT13;
        bootlog_reason = BOOTLOG_REASON_NONE;
        bootlog_status_changed();
        serial_trace_hex_value("INFO",
                               "boot log: using INT13 writes on the boot drive",
                               bootlog_target_drive);
        if (bootlog_is_virtualized()) {
            serial_trace("WARNING",
                         "boot log: hypervisor detected; firmware writes stop after the graphics mode set");
        }
        return;
    }
    bootlog_int13_write_unsafe = true;
    bootlog_target_kind = BOOTLOG_TARGET_NONE;
    bootlog_reason = BOOTLOG_REASON_FIRMWARE_WRITE;
    bootlog_scan_attempts = BOOTLOG_SCAN_ATTEMPTS;
    bootlog_status_changed();
    serial_trace("WARNING",
                 "boot log: validated log volume is not on the boot drive; on-medium writes refused");
}

/*
 * Which firmware disk calls are allowed?
 *
 * The bridge masks interrupts for the whole call (realmode.asm), and a
 * hypervisor's PhoenixBIOS answers those calls from the text-mode boot menu
 * but never returns from the next one after the SVGA display is programmed.
 * A virtualized boot therefore stops issuing them once the graphics mode is
 * up; its early save and boot-menu checkpoints are already on the medium.
 *
 * Bare metal has no such limitation in practice, and a USB boot has no ATA
 * path, so the validated log volume on the boot drive keeps writing through
 * the desktop and the final flush.  A target on the kernel's own polled ATA
 * driver is always safe.
 */
static bool bootlog_firmware_disk_allowed(void) {
    if (bootlog_int13_boot_drive_target()) {
        if (bootlog_is_virtualized() && bootlog_graphics_ready) {
            return false;
        }
        return true;
    }
    return system_state == STATE_BOOT_MENU || system_state == STATE_BOOT_TERMINAL;
}

/*
 * Find the volume the machine actually booted from, never "the first
 * disk that happens to carry a template".  The old order probed the
 * internal ATA master first, so a USB boot on a machine whose internal
 * disk had been booted before wrote the log to that internal disk and
 * left the stick's file empty.
 *
 *   1. the boot drive GRUB reported, through INT 13h (works for USB,
 *      where the kernel has no driver), then every other firmware hard
 *      disk: the reported number can be stale for a USB 3.0 boot, and an
 *      internal NVMe/SSD can occupy 0x80 while the stick is 0x81+;
 *   2. ATA, only when there is no boot-drive number at all or when the
 *      ATA MBR is byte-identical to the boot drive's MBR (same disk,
 *      just reached through the other path);
 *   3. with no boot-drive number to go on, firmware disks 0x80..0x87.
 *
 * A failed pass is retried: a firmware USB stack can take seconds to answer
 * after the handoff, and the early sync runs before the video mode set.  The
 * retries stop after BOOTLOG_SCAN_ATTEMPTS so a CD boot does not keep poking
 * the bus forever; the boot-menu panel records what each probe saw.
 */
static void bootlog_discover(void) {
    uint8_t boot_drive = boot_drive_valid ? boot_drive_number : 0xFFu;
    uint8_t result;
    bool any_int13 = false;

    if (bootlog_detached) {
        return;
    }
    if (!bootlog_firmware_disk_allowed()) {
        /* Never issue a firmware disk call once the graphics mode is up: it
         * can wait for an interrupt the bridge masks forever.  Give up the
         * medium for this boot and say so once. */
        if (bootlog_scan_attempts != BOOTLOG_SCAN_ATTEMPTS) {
            bootlog_scan_attempts = BOOTLOG_SCAN_ATTEMPTS;
            serial_trace("WARNING",
                         "boot log: firmware disk probing stopped after the graphics mode set");
        }
        return;
    }
    if (bootlog_target_kind != BOOTLOG_TARGET_NONE) {
        return;
    }
    if (bootlog_scan_attempts >= BOOTLOG_SCAN_ATTEMPTS) {
        return;
    }
    if (bootlog_scan_attempts != 0 &&
        (uint32_t)(timer_ticks - bootlog_last_scan_tick) < (2u * TIMER_HZ)) {
        return;
    }
    ++bootlog_scan_attempts;
    bootlog_last_scan_tick = timer_ticks;
    bootlog_probe_count = 0;
    bootlog_have_boot_mbr = false;

    /* A CD/DVD is read-only by physics.  Probing it through INT 13h cannot
     * produce a writable LOG volume, and those firmware calls are exactly
     * what can disturb a USB-legacy keyboard on some machines; the ISO path
     * therefore stops at the first discovery attempt.  A writable stick or
     * disk still reports 0x80+ and keeps the normal probe.
     *
     * El Torito CDs are reported as 0x9F or 0xE0..0xE7, not just 0xE0/0xE1.
     * VMware's PhoenixBIOS never returns from an INT 13h call on the 0x9F
     * number (the call waits for an IRQ the real-mode bridge masks), which
     * froze the CD boot on its first frame before the menu appeared. */
    if (boot_drive == 0x9Fu ||
        (boot_drive >= 0xE0u && boot_drive <= 0xE7u)) {
        bootlog_reason = BOOTLOG_REASON_READ_ONLY;
        bootlog_scan_attempts = BOOTLOG_SCAN_ATTEMPTS;
        bootlog_status_changed();
        return;
    }

    if (boot_drive < 0x80u) {
        bootlog_reason = BOOTLOG_REASON_NO_DRIVE;
    }

    /* 1. The drive the firmware booted from, then the rest of the map.
     * Only real hard-disk numbers: a CD's 0x9F/0xE0+ slot, or a stale value
     * the loader copied, must never reach a firmware disk call. */
    if (boot_drive >= 0x80u && boot_drive <= 0x87u) {
        if (!bootlog_int13_low_memory_is_free()) {
            /* The trace says which window was busy; the status line
             * only has room for the short reason. */
            bootlog_reason = BOOTLOG_REASON_BRIDGE;
        } else {
            realmode_install_gdt();
            bootlog_bridge_ready = true;
            bootlog_bios_keyboard_probe();
            result = bootlog_probe_drive(boot_drive);
            if (result == 2u) {
                bootlog_settle_int13_target();
                return;
            }
            if (result != 0u) {
                any_int13 = true;
            }
            result = bootlog_scan_int13_disks(boot_drive);
            if (result == 2u) {
                bootlog_settle_int13_target();
                return;
            }
            if (result != 0u) {
                any_int13 = true;
            }
            if (!any_int13) {
                bootlog_reason = BOOTLOG_REASON_NO_EXT;
                serial_trace_hex_value("WARNING",
                                       "boot log: no BIOS drive answers INT13 (extensions and CHS both failed)",
                                       boot_drive);
            } else {
                bootlog_reason = bootlog_have_boot_mbr ? BOOTLOG_REASON_NO_VOLUME
                                                       : BOOTLOG_REASON_READ;
                serial_trace_hex_value("WARNING",
                                       "boot log: INT13 drives found but none carries the validated log volume",
                                       boot_drive);
            }
        }
    }

    /* 2. ATA is safe only when it is demonstrably the same disk. */
    if (boot_drive >= 0x80u) {
        if (bootlog_have_boot_mbr) {
            uint8_t ata_mbr[512];

            bootlog_target_kind = BOOTLOG_TARGET_ATA;
            if (bootlog_target_read_sector(0, ata_mbr) &&
                bootlog_mbr_equal(bootlog_boot_mbr, ata_mbr)) {
                if (bootlog_fat_probe_mbr()) {
                    bootlog_reason = BOOTLOG_REASON_NONE;
                    bootlog_status_changed();
                    return;
                }
                bootlog_reason = BOOTLOG_REASON_NO_VOLUME;
            } else {
                /* Keep the INT13 failure when there was one: "ATA is not
                 * the boot disk" is expected on every machine with an
                 * internal drive and would hide why the boot medium was
                 * rejected. */
                if (bootlog_reason == BOOTLOG_REASON_NO_DRIVE ||
                    bootlog_reason == BOOTLOG_REASON_PROBING) {
                    bootlog_reason = BOOTLOG_REASON_ATA_DIFFERENT;
                }
                serial_trace("WARNING", "boot log: ATA disk is not the boot drive; not writing there");
            }
        }
    } else if (boot_drive == 0xFFu) {
        /* No boot-drive number from the loader: fall back to the disk
         * image layout this writer has always supported. */
        bootlog_target_kind = BOOTLOG_TARGET_ATA;
        if (bootlog_fat_probe_mbr()) {
            bootlog_reason = BOOTLOG_REASON_NONE;
            bootlog_status_changed();
            return;
        }
        bootlog_reason = BOOTLOG_REASON_NO_VOLUME;

        /* 3. Last resort: firmware hard disks 0x80..0x8F.  Some BIOSes
         * put a USB stick well above 0x83. */
        if (bootlog_int13_low_memory_is_free()) {
            realmode_install_gdt();
            bootlog_bridge_ready = true;
            bootlog_bios_keyboard_probe();
            result = bootlog_scan_int13_disks(0xFFu);
            if (result == 2u) {
                bootlog_settle_int13_target();
                return;
            }
            if (result != 0u) {
                any_int13 = true;
            }
            if (!any_int13) {
                bootlog_reason = BOOTLOG_REASON_NO_EXT;
            }
        } else {
            bootlog_reason = BOOTLOG_REASON_BRIDGE;
        }
    }

    bootlog_target_kind = BOOTLOG_TARGET_NONE;
    bootlog_status_changed();
    if (bootlog_scan_attempts == BOOTLOG_SCAN_ATTEMPTS) {
        serial_trace("WARNING", "boot log: no bios-serial.log target found on the boot medium");
        serial_trace("WARNING", "boot log: if the stick was flashed by ISO-extraction mode, re-flash with the DD Image mode option (or dd/balenaEtcher) - DD mode puts the log file on the stick");
    }
}

/*
 * Stream the NEW sectors of the RAM mirror out through the cached
 * cluster map, grow the directory entry's size field, and refresh the
 * hidden tail stamp. On any write failure the delta bookkeeping is
 * reset so the next sync rewrites the whole span - the medium may have
 * gone away, and a bounded full retry every auto-save window is cheaper
 * than a silently inconsistent file.
 */
static bool bootlog_sync_region(void) {
    const char *data = serial_log_data();
    uint32_t len = serial_log_length();
    uint32_t usable = bootlog_capacity - 512u; /* the stamp owns the tail */
    uint32_t persist_len = len < usable ? len : usable;
    uint32_t written = bootlog_region_written < persist_len ? bootlog_region_written : persist_len;
    uint32_t cluster_bytes = (uint32_t)bootlog_fat_spc * 512u;
    uint32_t first_sector = written / 512u;
    uint32_t need_sectors = (persist_len + 511u) / 512u;
    uint8_t sector[512];

    for (uint32_t s = first_sector; s < need_sectors; ++s) {
        uint32_t cl = (s * 512u) / cluster_bytes;
        uint32_t in = (s * 512u) % cluster_bytes;

        if (cl >= bootlog_chain_clusters) {
            bootlog_region_written = 0;
            return false;
        }
        for (uint32_t j = 0; j < 512u; ++j) {
            sector[j] = (s * 512u + j < persist_len) ? (uint8_t)data[s * 512u + j] : 0;
        }
        if (!bootlog_target_write_sector_verified(bootlog_chain_lba[cl] + in / 512u, sector)) {
            bootlog_region_written = 0;
            return false;
        }
    }

    /* Directory entry: the file size the host sees. */
    bootlog_put32(bootlog_dir_sector + bootlog_dir_off + 28, persist_len);
    if (!bootlog_target_write_sector_verified(bootlog_dir_lba, bootlog_dir_sector)) {
        bootlog_region_written = 0;
        return false;
    }

    /* Hidden tail stamp. */
    bootlog_put32(bootlog_stamp + BS_WRITE_BYTES, persist_len);
    if (!bootlog_boot_counted) {
        bootlog_boot_counted = true;
        bootlog_put32(bootlog_stamp + BS_BOOT_COUNT,
                      bootlog_get32(bootlog_stamp + BS_BOOT_COUNT) + 1u);
    }
    uint32_t x = 0;
    for (uint32_t off = 0; off < BS_CHECKSUM; off += 4) {
        x ^= bootlog_get32(bootlog_stamp + off);
    }
    bootlog_put32(bootlog_stamp + BS_CHECKSUM, x);
    if (!bootlog_target_write_sector_verified(bootlog_stamp_lba, bootlog_stamp)) {
        bootlog_region_written = 0;
        return false;
    }

    bootlog_region_written = persist_len;
    bootlog_last_sync_tick = timer_ticks;
    return true;
}

/*
 * Push everything the firmware may still hold in a write-back cache to
 * the medium, then read the directory entry back.  The final flush and
 * the early sync call this after their last sync; without it a USB
 * stick can look written on the machine that booted it and blank on the
 * machine it is plugged into next.
 */
static bool bootlog_finalize_medium(void) {
    uint8_t dir[512];

    bootlog_target_reset();
    if (!bootlog_target_read_sector(bootlog_dir_lba, dir)) {
        return false;
    }
    return bootlog_get32(dir + bootlog_dir_off + 28) == bootlog_region_written;
}

/*
 * Sync, flush the firmware cache, and verify the directory entry.  A
 * reset that loses the last sectors is retried once with the whole span
 * rewritten (the write path verifies every sector individually already).
 */
static bool bootlog_sync_and_finalize(void) {
    if (bootlog_target_kind == BOOTLOG_TARGET_INT13 &&
        (bootlog_int13_write_unsafe || !bootlog_firmware_disk_allowed())) {
        /* Every sync entry point goes through here; never let a stale
         * target turn into an INT 13h write outside the window the
         * platform allows. */
        bool firmware_window = !bootlog_int13_write_unsafe &&
                               !bootlog_firmware_disk_allowed();

        bootlog_target_kind = BOOTLOG_TARGET_NONE;
        bootlog_saved_ok = false;
        bootlog_reason = firmware_window ? BOOTLOG_REASON_FIRMWARE_WINDOW
                                         : BOOTLOG_REASON_FIRMWARE_WRITE;
        bootlog_scan_attempts = BOOTLOG_SCAN_ATTEMPTS;
        bootlog_status_changed();
        serial_trace("WARNING", "boot log: INT13 write stopped outside the safe firmware window");
        return false;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (bootlog_sync_region() && bootlog_finalize_medium()) {
            bootlog_saved_ok = true;
            bootlog_reason = BOOTLOG_REASON_NONE;
            bootlog_status_changed();
            return true;
        }
        bootlog_region_written = 0;
        bootlog_saved_ok = false;
        bootlog_reason = BOOTLOG_REASON_WRITE;
        bootlog_status_changed();
    }
    return false;
}

/*
 * Auto-save, called from the main loop in every interactive state: the
 * boot menu, the login screen and the desktop. Streams the new trace
 * sectors out whenever roughly a KiB has accumulated, or a couple of
 * seconds have passed with anything pending - so an accidental power
 * cut costs at most that last window. Silent by design: a trace line
 * per sync would feed itself.
 */
static void serial_bootlog_autosave_tick(void) {
    uint32_t len;
    uint32_t pending;

    if (system_state == STATE_SHUTDOWN) {
        return; /* the poweroff attempt does the final flush */
    }
    if (!bootlog_firmware_disk_allowed() &&
        bootlog_target_kind != BOOTLOG_TARGET_ATA) {
        return; /* see bootlog_firmware_disk_allowed() */
    }

    /* Keep looking for the medium even when the trace has not grown.
     * A firmware USB stack can start answering INT 13h only seconds after
     * the handoff; discovery is rate-limited internally and gives up after
     * BOOTLOG_SCAN_ATTEMPTS, so this cannot poke the bus forever. */
    if (bootlog_target_kind == BOOTLOG_TARGET_NONE) {
        bootlog_discover();
    }

    len = serial_log_length();
    pending = len > bootlog_region_written ? len - bootlog_region_written : 0;
    if (pending == 0) {
        return;
    }
    if (pending < 1024u &&
        (uint32_t)(timer_ticks - bootlog_last_sync_tick) < (2u * TIMER_HZ)) {
        return;
    }

    bootlog_discover();
    if (bootlog_target_kind == BOOTLOG_TARGET_NONE || !bootlog_sync_region()) {
        if (bootlog_target_kind != BOOTLOG_TARGET_NONE) {
            bootlog_saved_ok = false;
            bootlog_reason = BOOTLOG_REASON_WRITE;
            bootlog_status_changed();
        }
        return;
    }
    bootlog_saved_ok = true;
    bootlog_reason = BOOTLOG_REASON_NONE;
    bootlog_status_changed();

    if (!bootlog_autosave_armed) {
        bootlog_autosave_armed = true;
        if (bootlog_target_kind == BOOTLOG_TARGET_ATA) {
            serial_trace("INFO", "boot log: auto-saving to bios-serial.log on the hard disk");
        } else {
            serial_trace_hex_value("INFO", "boot log: auto-saving to bios-serial.log on BIOS drive",
                                   bootlog_target_drive);
        }
    }
}

/* Called once the video mode is programmed.  A virtualized boot uses this
 * boundary to stop firmware disk calls before the SVGA path can make them
 * wait forever for a masked interrupt. */
void serial_bootlog_mark_graphics_ready(void);

void serial_bootlog_mark_graphics_ready(void) {
    bootlog_graphics_ready = true;
}

/*
 * Early sync: called once from kernel_main just before the graphics
 * mode set.  The video BIOS is the most likely place for a real machine
 * to hang, and until this point the whole trace exists only in RAM - a
 * hang there used to leave bios-serial.log zero bytes even though the
 * kernel had started.  This writes the boot story so far and flushes
 * the firmware cache, so a later hang still leaves a usable file.
 */
void serial_bootlog_early_sync(const MultibootInfo *mbi);

void serial_bootlog_early_sync(const MultibootInfo *mbi) {
    /* Keep the multiboot block for the real-mode ownership checks: the
     * early sync is the first caller, and the flush/auto-save paths need
     * the same guard long after kernel_main has moved on. */
    bootlog_boot_info = mbi;
    serial_trace("INFO", "boot log: early save before graphics init");
    bootlog_discover();
    if (bootlog_target_kind == BOOTLOG_TARGET_NONE) {
        return;
    }
    if (!bootlog_sync_and_finalize()) {
        serial_trace("WARNING", "boot log: early save failed; will retry");
        return;
    }
    serial_trace("INFO", "boot log: early boot trace saved to the boot medium");
}

/*
 * Checkpoint: persist the trace at a named boot milestone.  The early
 * sync only proves the machine reached the video mode set; if a real
 * machine hangs later, these staged saves say whether graphics
 * initialised, whether the first frame was presented, and whether the
 * main loop was entered.  Called from kernel_main only, in debug builds.
 */
void serial_bootlog_checkpoint(const char *stage);

void serial_bootlog_checkpoint(const char *stage) {
    serial_trace_concat("INFO", "boot log stage: ",
                        stage != NULL ? stage : "unknown");
    bootlog_discover();
    if (bootlog_target_kind == BOOTLOG_TARGET_NONE) {
        return;
    }
    if (!bootlog_sync_and_finalize()) {
        serial_trace("WARNING", "boot log: checkpoint write failed; will retry");
    }
}

/*
 * Final flush: append the reason line (it is the last thing that lands
 * in the file) and sync to the very end. Called at power off, restart,
 * halt and from the crash handler - the dump is already in the RAM
 * mirror, so it ships with the same write.
 */
void serial_bootlog_flush(const char *reason);

void serial_bootlog_flush(const char *reason) {
    serial_trace_concat("INFO", "boot log flush: ",
                        reason != NULL ? reason : "unknown");

    bootlog_discover();
    if (bootlog_target_kind == BOOTLOG_TARGET_NONE) {
        serial_trace("WARNING", "boot log: no bios-serial.log target to flush to on this medium");
    } else if (!bootlog_sync_and_finalize()) {
        serial_trace("WARNING", "boot log: bios-serial.log write failed");
    } else if (bootlog_target_kind == BOOTLOG_TARGET_ATA) {
        serial_trace("INFO", "boot log: flushed to bios-serial.log on the hard disk");
    } else {
        serial_trace_hex_value("INFO", "boot log: flushed to bios-serial.log on BIOS drive",
                               bootlog_target_drive);
    }
    serial_trace("INFO", "boot log flush complete");
}

/*
 * Give up the boot medium for the rest of this boot.  The caller has
 * already flushed everything it wants kept; from here the firmware's
 * disk path may be gone (the xHCI controller is about to be reset or
 * handed back to the firmware), so discovery and the auto-save tick
 * must never touch INT 13h again.  The serial mirror keeps growing so a
 * capture on COM1 still sees the rest of the boot.
 */
void serial_bootlog_detach_medium(void);

void serial_bootlog_detach_medium(void) {
    if (bootlog_detached) {
        return;
    }
    bootlog_detached = true;
    bootlog_target_kind = BOOTLOG_TARGET_NONE;
    bootlog_status_changed();
    serial_trace("INFO", "boot log: medium detached; further trace is COM1/RAM only");
}

#else /* !HALOXOS_CONFIG_DEBUG */

/* Release builds: no RAM mirror, no log file, no auto-save. */

static void serial_bootlog_autosave_tick(void) {
}

void serial_bootlog_mark_graphics_ready(void) {
}

void serial_bootlog_early_sync(const MultibootInfo *mbi) {
    (void)mbi;
}

void serial_bootlog_checkpoint(const char *stage) {
    (void)stage;
}

void serial_bootlog_flush(const char *reason);

void serial_bootlog_flush(const char *reason) {
    (void)reason;
}

void serial_bootlog_detach_medium(void) {
}

static const char *serial_bootlog_status_text(void) {
    return "";
}

const char *serial_bootlog_diag_line(uint8_t index) {
    (void)index;
    return "";
}

void serial_bootlog_set_usb_hid_ready(bool ready) {
    (void)ready;
}

bool serial_bootlog_bios_keyboard_usable(void) {
    /* Release builds have no real-mode INT 16h probe.  Assume the firmware
     * path works so the native driver never disturbs it; XHCI_HID_FORCE=1
     * overrides this at the call site. */
    return true;
}

bool serial_bootlog_bios_key_poll(uint8_t *ascii, uint8_t *scan) {
    (void)ascii;
    (void)scan;
    return false;
}

uint32_t serial_bootlog_bios_key_count(void) {
    return 0;
}

#endif

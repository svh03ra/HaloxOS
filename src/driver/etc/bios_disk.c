// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: bios_disk.c, firmware INT 13h disk transport.
//
// This repository is licensed under the GNU General Public License.

/*
 * A USB stick is a hard disk to the firmware, but the kernel's polled ATA
 * driver cannot see it: the stick lives behind the firmware's USB stack.
 * GRUB booted through INT 13h, so the same firmware path is the one
 * transport guaranteed to reach the boot stick on real hardware.
 *
 * This backend is deliberately read-only.  Stress Test's disk mode only
 * needs reads, and refusing writes here means a wrong drive number can
 * never modify a disk.  It uses the same real-mode bridge as the VBE and
 * boot-log paths (realmode.asm), including the 32 KiB transfer window at
 * linear 0x20000.  INT 13h AH=00h (reset) is never issued: the bridge runs
 * with interrupts disabled, and a firmware reset that waits for a
 * controller interrupt can hang the machine forever (see bootlog.c).
 *
 * Discovery order:
 *   1. AH=41h: extended disk services present?
 *   2. AH=48h: EDD geometry / total 512-byte sector count.
 *   3. AH=08h: legacy CHS geometry, used as a fallback and as a capacity
 *      source when a CSM answers AH=41h but not AH=48h.
 *   4. Read sector 0 through the selected path before declaring the drive
 *      usable.
 */

extern const uint32_t rm_layout[];
extern int realmode_call(void);
extern void realmode_install_gdt(void);

/* Linear 0x20000, 32 KiB.  Clear of the bridge block (0x8000), the
 * firmware's real-mode stack, the boot sector and the EBDA.  The boot log
 * validates this window before its first firmware call; this backend only
 * runs later, from the desktop, so it reuses the same proven window. */
#define BIOS_DISK_XFER_LINEAR  0x20000u
#define BIOS_DISK_XFER_BYTES   0x8000u
#define BIOS_DISK_XFER_SEG     0x2000u
#define BIOS_DISK_MAX_SECTORS  64u

typedef struct {
    uint8_t drive;
    bool ext;          /* AH=41h/42h extended services available */
    bool chs;          /* AH=08h geometry available */
    bool use_chs;      /* the last successful transfer used CHS */
    bool ready;
    uint16_t heads;
    uint16_t spt;
    uint16_t max_cyl;
    uint32_t sectors;  /* total 512-byte sectors, 0 when unknown */
} BiosDisk;

static bool bios_disk_last_faulted;
static uint8_t bios_disk_last_ah;

static uint8_t *bios_disk_regs(void) {
    return (uint8_t *)(uintptr_t)rm_layout[RM_L_REGS];
}

static void bios_disk_reg_set(int field, uint32_t value) {
    volatile uint32_t *slot =
        (volatile uint32_t *)(void *)(bios_disk_regs() + rm_layout[field]);
    *slot = value;
}

static uint32_t bios_disk_reg_get(int field) {
    const volatile uint32_t *slot =
        (const volatile uint32_t *)(const void *)(bios_disk_regs() + rm_layout[field]);
    return *slot;
}

/*
 * One firmware call through the bridge.  The register file is cleared first
 * (which also zeroes the INT selector word), then the caller's registers and
 * the interrupt vector go in.  Returns false when the call itself faulted in
 * real mode; the caller must not read the register file in that case.
 */
static bool bios_disk_rm_call(uint8_t vector, uint32_t eax, uint32_t ebx,
                              uint32_t ecx, uint32_t edx, uint32_t esi,
                              uint32_t es) {
    volatile uint32_t *regs = (volatile uint32_t *)(void *)bios_disk_regs();
    volatile uint16_t *vector_slot =
        (volatile uint16_t *)(void *)(bios_disk_regs() + rm_layout[RM_L_INT]);

    bios_disk_last_faulted = false;
    bios_disk_last_ah = 0;

    for (int i = 0; i < 10; ++i) {
        regs[i] = 0; /* 0x00..0x27: EAX..ES plus the INT selector word */
    }
    bios_disk_reg_set(RM_L_EAX, eax);
    bios_disk_reg_set(RM_L_EBX, ebx);
    bios_disk_reg_set(RM_L_ECX, ecx);
    bios_disk_reg_set(RM_L_EDX, edx);
    bios_disk_reg_set(RM_L_ESI, esi);
    bios_disk_reg_set(RM_L_ES, es);
    *vector_slot = vector;
    if (realmode_call() != 0) {
        bios_disk_last_faulted = true;
        return false;
    }
    bios_disk_last_ah = (uint8_t)((bios_disk_reg_get(RM_L_EAX) >> 8) & 0xFFu);
    return true;
}

static bool bios_disk_flags_carry(void) {
    return (bios_disk_reg_get(RM_L_FLAGS) & 1u) != 0;
}

/* AH=41h: does this BIOS drive support the extended services?  DS:SI must
 * point at a writable 30-byte parameter buffer; the bridge's RM_BUF is used
 * so the real-mode IVT is never touched. */
static bool bios_disk_ext_present(uint8_t drive) {
    volatile uint8_t *param = (volatile uint8_t *)(uintptr_t)rm_layout[RM_L_BUF];

    for (int i = 0; i < 32; ++i) {
        param[i] = 0;
    }
    if (!bios_disk_rm_call(0x13, 0x4100u, 0x55AAu, 0, drive,
                           rm_layout[RM_L_BUF], 0)) {
        return false;
    }
    if (bios_disk_flags_carry()) {
        return false;
    }
    return (bios_disk_reg_get(RM_L_EBX) & 0xFFFFu) == 0xAA55u;
}

static uint32_t bios_disk_get32(const volatile uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* AH=48h: EDD device parameters.  The total sector count is the real
 * capacity a USB stick reports; it is what keeps stress reads inside the
 * medium instead of wrapping at an arbitrary 4 MiB marker. */
static bool bios_disk_edd_params(uint8_t drive, BiosDisk *disk) {
    volatile uint8_t *buf = (volatile uint8_t *)(uintptr_t)rm_layout[RM_L_BUF];
    uint32_t total_lo;
    uint32_t total_hi;
    uint16_t bytes_per_sector;

    for (int i = 0; i < 66; ++i) {
        buf[i] = 0;
    }
    buf[0] = 30; /* EDD 1.1 size; 3.0 BIOSes accept the smaller block too */
    if (!bios_disk_rm_call(0x13, 0x4800u, 0, 0, drive,
                           rm_layout[RM_L_BUF], 0)) {
        return false;
    }
    if (bios_disk_flags_carry() || buf[0] < 26u) {
        return false;
    }
    bytes_per_sector = (uint16_t)buf[24] | ((uint16_t)buf[25] << 8);
    if (bytes_per_sector != 512u) {
        return false; /* this backend transfers 512-byte units */
    }
    total_lo = bios_disk_get32(buf + 16);
    total_hi = bios_disk_get32(buf + 20);
    if (total_hi != 0u) {
        total_lo = 0xFFFFFFFFu; /* 32-bit LBA limit of this backend */
    }
    if (total_lo == 0u) {
        return false;
    }
    disk->sectors = total_lo;
    return true;
}

/* AH=08h: the drive's CHS geometry.  A CSM that boots USB in legacy mode
 * can service AH=02h even when AH=41h/42h are absent, and the geometry also
 * gives a capacity estimate when AH=48h is missing. */
static bool bios_disk_chs_geometry(uint8_t drive, BiosDisk *disk) {
    uint32_t ecx;
    uint32_t edx;
    uint32_t max_cyl;
    uint32_t spt;
    uint32_t heads;
    uint32_t total;

    if (!bios_disk_rm_call(0x13, 0x0800u, 0, 0, drive, 0, 0)) {
        return false;
    }
    if (bios_disk_flags_carry()) {
        return false;
    }
    ecx = bios_disk_reg_get(RM_L_ECX);
    edx = bios_disk_reg_get(RM_L_EDX);
    spt = ecx & 0x3Fu;                         /* CL bits 0-5 */
    max_cyl = ((ecx & 0xC0u) << 2) | ((ecx >> 8) & 0xFFu);
    heads = ((edx >> 8) & 0xFFu) + 1u;         /* DH is the max head index */

    if (spt == 0u || spt > 63u || heads == 0u || heads > 256u) {
        return false;
    }
    if (max_cyl > 1023u) {
        max_cyl = 1023u;                       /* CHS cannot name more */
    }
    disk->spt = (uint16_t)spt;
    disk->heads = (uint16_t)heads;
    disk->max_cyl = (uint16_t)max_cyl;
    disk->chs = true;

    if (disk->sectors == 0u) {
        total = (max_cyl + 1u) * heads * spt;
        if (total == 0u) {
            return false;
        }
        disk->sectors = total;
    }
    return true;
}

/* AH=42h: one extended read into the bounce window. */
static bool bios_disk_ext_read(const BiosDisk *disk, uint32_t lba,
                               uint32_t sectors) {
    volatile uint8_t *dap = (volatile uint8_t *)(uintptr_t)rm_layout[RM_L_BUF];

    if (sectors == 0u || sectors > BIOS_DISK_MAX_SECTORS) {
        return false;
    }
    dap[0] = 0x10; /* DAP size */
    dap[1] = 0;
    dap[2] = (uint8_t)sectors;
    dap[3] = 0;
    dap[4] = 0; /* buffer offset 0 */
    dap[5] = 0;
    dap[6] = (uint8_t)BIOS_DISK_XFER_SEG;
    dap[7] = (uint8_t)(BIOS_DISK_XFER_SEG >> 8);
    for (int i = 0; i < 8; ++i) {
        dap[8 + i] = (uint8_t)(lba >> (8 * i));
    }
    /* AH=42h read, AL=0 (no verify); DS:SI -> DAP */
    if (!bios_disk_rm_call(0x13, 0x4200u, 0, 0, disk->drive,
                           rm_layout[RM_L_BUF], 0)) {
        return false;
    }
    return !bios_disk_flags_carry();
}

/* AH=02h: one sector at a time through CHS.  The caller has already
 * bounded the LBA to the geometry's last cylinder. */
static bool bios_disk_chs_read(const BiosDisk *disk, uint32_t lba) {
    uint32_t per_cylinder;
    uint32_t cylinder;
    uint32_t rest;
    uint32_t head;
    uint32_t sector;
    uint16_t cx;
    uint16_t dx;

    if (!disk->chs || disk->heads == 0u || disk->spt == 0u) {
        return false;
    }
    per_cylinder = (uint32_t)disk->heads * (uint32_t)disk->spt;
    cylinder = lba / per_cylinder;
    rest = lba % per_cylinder;
    if (cylinder > disk->max_cyl || cylinder > 1023u) {
        return false;
    }
    head = rest / disk->spt;
    sector = rest % disk->spt + 1u;

    /* CH = cylinder bits 0-7, CL bits 6-7 = cylinder bits 8-9,
     * CL bits 0-5 = sector (1-based). */
    cx = (uint16_t)(sector | ((cylinder & 0x300u) >> 2) |
                    ((cylinder & 0xFFu) << 8));
    dx = (uint16_t)((head << 8) | disk->drive);

    if (!bios_disk_rm_call(0x13, 0x0201u, 0, cx, dx, 0,
                           BIOS_DISK_XFER_SEG)) {
        return false;
    }
    return !bios_disk_flags_carry();
}

static void bios_disk_copy_out(uint8_t *dst, uint32_t bytes) {
    const uint8_t *src = (const uint8_t *)(uintptr_t)BIOS_DISK_XFER_LINEAR;

    memcpy_local(dst, src, (size_t)bytes);
}

/*
 * Read sectors into a caller buffer.  Extended reads move up to 64 sectors
 * per firmware call; CHS reads move one sector at a time.  If an extended
 * read fails mid-transfer and a geometry is available, the transfer falls
 * back to CHS and retries the same sector rather than aborting the stress
 * run on a firmware quirk.
 */
static bool bios_disk_read_sectors(BiosDisk *disk, uint32_t lba,
                                   uint8_t *dst, uint32_t sectors) {
    while (sectors > 0u) {
        if (disk->use_chs || !disk->ext) {
            if (!bios_disk_chs_read(disk, lba)) {
                return false;
            }
            bios_disk_copy_out(dst, 512u);
            disk_io_add_bytes(512u);
            ++lba;
            dst += 512;
            --sectors;
        } else {
            uint32_t chunk = sectors > BIOS_DISK_MAX_SECTORS
                                 ? BIOS_DISK_MAX_SECTORS : sectors;

            if (bios_disk_ext_read(disk, lba, chunk)) {
                uint32_t bytes = chunk * 512u;

                bios_disk_copy_out(dst, bytes);
                disk_io_add_bytes(bytes);
                lba += chunk;
                dst += bytes;
                sectors -= chunk;
            } else if (disk->chs) {
                disk->use_chs = true;
            } else {
                return false;
            }
        }
    }
    return true;
}

/*
 * Probe one firmware hard disk.  Only drives in the 0x80..0x8F range are
 * accepted: those are the firmware's hard-disk numbers (USB sticks included)
 * and never a floppy or CD-ROM emulation.
 */
static bool bios_disk_probe(uint8_t drive, BiosDisk *disk) {
    uint8_t *xfer = (uint8_t *)(uintptr_t)BIOS_DISK_XFER_LINEAR;
    bool ext_ok;
    bool edd_ok;
    bool chs_ok;

    for (uint32_t i = 0; i < sizeof(*disk); ++i) {
        ((uint8_t *)disk)[i] = 0;
    }
    disk->drive = drive;
    if (drive < 0x80u || drive > 0x8Fu) {
        return false;
    }

    /* The video/boot-log path has already installed the bridge GDT; doing
     * it again is idempotent and keeps this backend usable on its own. */
    realmode_install_gdt();

    ext_ok = bios_disk_ext_present(drive);
    edd_ok = ext_ok && bios_disk_edd_params(drive, disk);
    chs_ok = bios_disk_chs_geometry(drive, disk);
    if (!ext_ok && !chs_ok) {
        return false;
    }
    if (!edd_ok && !chs_ok) {
        return false; /* no trustworthy capacity */
    }
    disk->ext = ext_ok;
    disk->use_chs = !ext_ok;

    /* A successful sector-0 read is the final gate: AH=41h alone only says
     * the firmware knows the call, not that this drive answers it. */
    if (!bios_disk_read_sectors(disk, 0u, xfer, 1u)) {
        return false;
    }
    disk->ready = true;
    return true;
}

static bool bios_disk_read(BiosDisk *disk, uint32_t lba,
                           uint8_t *dst, uint32_t sectors) {
    if (!disk->ready || dst == NULL || sectors == 0u) {
        return false;
    }
    return bios_disk_read_sectors(disk, lba, dst, sectors);
}

static uint32_t bios_disk_capacity(const BiosDisk *disk) {
    return disk->ready ? disk->sectors : 0u;
}

/* VMware's PhoenixBIOS is known to hang on the first firmware disk call
 * after its SVGA mode is programmed (the bridge masks interrupts for the
 * whole call).  Stress runs after the desktop is up, so refuse this
 * transport there; QEMU, other hypervisors and bare metal are unaffected. */
static bool bios_disk_runtime_allowed(void) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    if (!cpuid_is_available()) {
        return true;
    }
    cpuid_query(0x40000000u, &eax, &ebx, &ecx, &edx);
    if (ebx == 0x61774D56u && ecx == 0x4D566572u && edx == 0x65726177u) {
        return false; /* "VMwareVMware" */
    }
    return true;
}

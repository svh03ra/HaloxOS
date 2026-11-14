// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: serial_log.c, debug-only COM1 capture on the boot medium.

// This repository is licensed under the GNU General Public License.

/*
 * Debug builds mirror every COM1 character into a reserved slot on the medium
 * the kernel was booted from, so a machine with no serial cable can still hand
 * its boot trace over: pull the USB stick and run tools/readseriallog.py,
 * which writes the captured text out as serial.log.
 *
 * The write path is the firmware's INT 13h, not the ATA driver.  The machines
 * this exists for boot from a USB stick, and a USB stick is not on the ATA bus
 * - this kernel has no USB mass-storage driver - so the only code that can
 * reach it is the BIOS.  The real-mode bridge in realmode.asm runs the call;
 * realmode_call_int13() is the INT 13h side of it, and the bridge owns the low-
 * memory scratch (DAP + one sector) the call reads and writes through.
 *
 * Slot layout, in 512-byte sectors:
 *
 *   [header][text sector][text sector]...
 *
 * The header carries the magic 'HXSL'.  The kernel refuses to touch a slot
 * that does not already have it, so a stale or foreign medium can never be
 * written into.  The build reserves the slot in two shapes and the kernel
 * probes for both:
 *
 *   disk/floppy  a real SERIAL.LOG file in the FAT partition (FAT16 on a
 *                disk, FAT12 on a floppy), whose clusters the build pins to
 *                HALOXOS_SERIAL_LOG_LBA / HALOXOS_SERIAL_LOG_FLOPPY_LBA (see
 *                tools/mkseriallog.py).  Windows shows the file when the
 *                stick is plugged in; its size stays the full slot height.
 *                Both fixed positions are probed on every 512-byte-block
 *                medium, because a floppy image written to a USB stick is
 *                reported by the firmware as a hard disk, not a floppy.
 *   ISO          a raw region right after the ISO volume - which the kernel
 *                finds through the PVD's volume-space size - and before the
 *                DOOM WAD tail.  A pressed CD is read-only, so the kernel says
 *                so instead of writing; the region pays off when the ISO was
 *                dd'd to a USB stick.
 *
 * Characters are buffered into one sector and that sector is rewritten as soon
 * as a line ends, so a hang costs at most the line in flight - which is the
 * whole point of the feature - and the text still lands on the medium
 * contiguously, with no holes between lines.  The sector only advances once it
 * is full.  Two sources are replayed into the slot before the live capture
 * starts, in order: the zstd loader's own COM1 output - mirrored into the
 * reserved handoff block while the kernel was still a compressed module, so a
 * boot that dies before the kernel even exists still explains itself - and the
 * kernel's earliest characters, buffered in a small RAM ring since entry.
 * serial.log therefore opens with the loader's [LDR]: lines and continues into
 * "initialize kernel" instead of starting there.
 */

#ifndef HALOXOS_SERIAL_LOG
#define HALOXOS_SERIAL_LOG 0
#endif

#ifndef HALOXOS_SERIAL_LOG_LBA
#define HALOXOS_SERIAL_LOG_LBA 65536u
#endif

#ifndef HALOXOS_SERIAL_LOG_SECTORS
#define HALOXOS_SERIAL_LOG_SECTORS 1024u
#endif

/* The floppy target carries a much smaller slot; see the Makefile. */
#ifndef HALOXOS_SERIAL_LOG_FLOPPY_LBA
#define HALOXOS_SERIAL_LOG_FLOPPY_LBA 1024u
#endif

/*
 * The zstd loader's pre-kernel COM1 trace (loader/main.c mirrors every byte
 * it clocks out into this block).  The text there is the only record of the
 * decompression phase: the wire has it, but a machine with no serial port
 * would otherwise lose exactly the lines that explain a boot that never
 * reached the kernel.  BIOS and GRUB text is not here, because nothing before
 * the loader writes memory the kernel would read.
 *
 * The block sits inside the 64 KiB the RAM gate reserves for the loader
 * handoff above the kernel image; the address mirrors init.c's own switch
 * for the embedded-WAD build, which moves the handoff block.
 */
#include "ram_requirement.h"

#define SERIAL_LOG_LOADER_TRACE_MAGIC  0x4C525458u /* 'XTRL' little endian */
#define SERIAL_LOG_LOADER_TRACE_OFFSET 0x1000u
#define SERIAL_LOG_LOADER_TRACE_HEADER 8u /* magic, byte count; text follows */
#define SERIAL_LOG_LOADER_TRACE_MAX    0x4000u
#ifdef HALOXOS_KERNEL_IMAGE_SPAN
#define SERIAL_LOG_LOADER_INFO_ADDR 0x1500000u
#else
#define SERIAL_LOG_LOADER_INFO_ADDR HALOXOS_LOADER_INFO_ADDR
#endif
#define SERIAL_LOG_LOADER_TRACE_ADDR \
    (SERIAL_LOG_LOADER_INFO_ADDR + SERIAL_LOG_LOADER_TRACE_OFFSET)

/*
 * A UEFI boot's kernel half: the COM1 trace kept in RAM.
 *
 * On UEFI the kernel has no way to reach the boot medium - no BIOS disk
 * services, no USB mass-storage driver, and runtime services that are
 * 64-bit code on the machines that matter - so the old capture design just
 * stopped, and every kernel print after "initialize kernel" (input, video,
 * screen state, app opens, the crash handler) was lost on a machine with no
 * serial port.  The trace is therefore mirrored into a fixed-address RAM
 * ring, and the NEXT boot's EFI loader writes it into SERIAL.LOG while its
 * own firmware services still work.
 *
 * RAM survives a warm reset - the reset button, Ctrl-Alt-Del, and the triple
 * fault a hard crash ends in - which is exactly the moment a boot trace is
 * wanted.  A power cycle still loses it; that is the price of needing no
 * disk driver at all, and the EFI loader's own half of the file still says
 * what this boot chain did either way.
 *
 * WHERE the ring lives is the part the address has to get right.  A ring
 * low in memory, next to the loader handoff block, is found by the next
 * boot's firmware and reused before the EFI loader runs at all: measured,
 * OVMF puts DXE code on the 11 MiB handoff ring within one second of the
 * reset (the harvested header read back as an instruction stream).  The
 * primary ring is therefore high, at 768 MiB - RAM no firmware reclaims
 * that early on any machine with a gigabyte or more, and this kernel can
 * only enable it once the memory map proves the address is real memory.
 * The low ring stays as the fallback for the small machines where 768 MiB
 * does not exist, and the loader harvests whichever it finds.
 *
 * Layout of the ring, at either address:
 *   +0x00 magic 'XTRK'   +0x04 total bytes written (monotonic)
 *   +0x08 capacity       +0x0C boot stage breadcrumb
 *   +0x1000..            text, wrapping at SERIAL_LOG_RAM_TEXT_BYTES
 * The byte lands before the count that names it, so a crash mid-character
 * costs at most that character, never a gap.
 */
#define SERIAL_LOG_RAM_RING_MAGIC  0x4B525458u /* 'XTRK' little endian */
#define SERIAL_LOG_RAM_RING_HIGH   0x30000000u /* 768 MiB: firmware never lands here early */
#define SERIAL_LOG_RAM_RING_OFFSET 0x8000u
#define SERIAL_LOG_RAM_RING_BYTES  0x8000u
#define SERIAL_LOG_RAM_TEXT_OFFSET 0x1000u
#define SERIAL_LOG_RAM_TEXT_BYTES  0x7000u
#define SERIAL_LOG_RAM_RING_LOW \
    (SERIAL_LOG_LOADER_INFO_ADDR + SERIAL_LOG_RAM_RING_OFFSET)

#define SERIAL_LOG_MAGIC 0x4C535848u /* 'HXSL' little endian */
#define SERIAL_LOG_VERSION 1u
#define SERIAL_LOG_SECTOR_BYTES 512u
#define SERIAL_LOG_RING_BYTES 8192u

/*
 * Header layout, by byte offset: eight dwords (magic, version, header bytes,
 * region sectors, boot drive, date, time, flags), then the location of the
 * slot's own 32-byte FAT directory entry - the absolute sector that holds it
 * and the byte offset inside that sector.  The kernel raises the file's
 * recorded size there as the text grows, so Windows shows the log instead of
 * the whole reserved slot; a zero sector means the slot is not a file (the
 * ISO's raw region) and nothing is updated.
 *
 * The rest of the sector is the diagnostic half of the feature, and it is
 * what makes a capture that stopped early explain itself on a machine with
 * no serial port.  Six more dwords follow the directory-entry location: the
 * boot stage the kernel reached, the bytes actually stored, the firmware
 * error the last failed write came back with, how many writes have failed
 * in a row, and the capture state.  Bytes 64.. are a one-line human summary
 * of those numbers, so opening SERIAL.LOG in Notepad answers "how far did
 * the boot get and why did the log stop" without the reader knowing the
 * dword layout; bytes 224.. carry the banner.
 *
 * The header is rewritten whenever one of those values changes, which is
 * cheap (one sector) and always leaves the medium holding the newest state
 * even if the very next write is the one that fails.
 */
#define SERIAL_LOG_HDR_DIRENT_LBA    32u
#define SERIAL_LOG_HDR_DIRENT_OFFSET 36u
#define SERIAL_LOG_HDR_STAGE         40u /* boot breadcrumb the kernel reached */
#define SERIAL_LOG_HDR_STORED_BYTES  44u /* text bytes actually on the medium */
#define SERIAL_LOG_HDR_ERROR         48u /* firmware status of the last failure */
#define SERIAL_LOG_HDR_FAILURES      52u /* consecutive failed writes */
#define SERIAL_LOG_HDR_STATE         56u /* SerialLogHeaderState below */
#define SERIAL_LOG_HDR_RESERVED      60u
#define SERIAL_LOG_STATUS_OFFSET     64u
#define SERIAL_LOG_STATUS_MAX        160u
#define SERIAL_LOG_BANNER_OFFSET     224u
#define SERIAL_LOG_DIRENT_SIZE_OFFSET 28u /* size field inside the entry */

/* What the header's state dword says the capture is doing.  A medium pulled
 * out of a machine that stopped booting early is diagnosed from these, not
 * from the text: the text says what happened, this says where it stopped. */
typedef enum {
    SERIAL_LOG_STATE_OFF = 0u,      /* release build or an unknown medium */
    SERIAL_LOG_STATE_EARLY = 1u,    /* medium not identified yet */
    SERIAL_LOG_STATE_ACTIVE = 2u,   /* mirroring the trace */
    SERIAL_LOG_STATE_UEFI = 3u,     /* UEFI boot: no BIOS disk services */
    SERIAL_LOG_STATE_NOSLOT = 4u,   /* no reserved slot on the boot medium */
    SERIAL_LOG_STATE_READONLY = 5u, /* pressed CD or write-protected medium */
    SERIAL_LOG_STATE_WRITEFAIL = 6u,/* the firmware refuses writes now */
    SERIAL_LOG_STATE_FULL = 7u      /* the reserved slot is exhausted */
} SerialLogHeaderState;

/*
 * How often the sector still being filled is put on the medium without a line
 * ending to trigger it.  The capture used to advance only at a newline, so a
 * boot that died mid-line - or a desktop that prints nothing for minutes -
 * left the medium holding the previous line and the RAM holding the rest.
 * Rewriting the in-flight sector every few frames keeps the file within one
 * interval of the screen, which is what makes a power cut cost a sentence
 * rather than a whole boot stage.  At TIMER_HZ=60 four ticks is about 15 Hz,
 * fast enough to be live on a screen photograph and slow enough that a USB
 * stick behind a legacy BIOS - where each write is a real millisecond or
 * more - is not being hammered once per frame.
 */
#ifndef SERIAL_LOG_PARTIAL_TICKS
#define SERIAL_LOG_PARTIAL_TICKS 4u
#endif

/* The diagnostic header is refreshed on the same cadence as the text is
 * stored, one second apart, plus immediately whenever the boot stage or the
 * capture state changes (which is when the answer matters). */
#ifndef SERIAL_LOG_HEADER_TICKS
#define SERIAL_LOG_HEADER_TICKS 60u
#endif

/*
 * Consecutive failed writes tolerated before the capture goes quiet.  Every
 * attempt first resets the disk system and retries once (see
 * serial_log_write_sector_at), which is what gets past the single failure a
 * real BIOS hands out after its own logic or another driver has touched the
 * drive; only a firmware disk path that is gone for good - the kernel's USB
 * driver taking the controller the firmware booted from - fails this many
 * times in a row.
 */
#ifndef SERIAL_LOG_MAX_FAILURES
#define SERIAL_LOG_MAX_FAILURES 3u
#endif

#if HALOXOS_SERIAL_LOG

/* Scratch owned by the real-mode bridge (realmode.asm). */
extern const uint32_t rm_serial_log_dap;
extern const uint32_t rm_serial_log_buf;
extern int realmode_call_int13(void);

/* The rm_layout table and the RM_L_* indices are declared by vbe.c, which
 * kernel.c includes before this file.  The register accessors below mirror
 * vbe's own helpers because the INT 13h call fills the same register file. */

/* The BIOS's EDD disk-address packet, built at rm_serial_log_dap. */
typedef struct __attribute__((packed)) {
    uint8_t size;     /* 0x10 */
    uint8_t reserved; /* 0 */
    uint16_t count;   /* 512-byte blocks */
    uint16_t offset;  /* transfer buffer, segment:offset */
    uint16_t segment;
    uint32_t lba;
    uint32_t lba_high;
} SerialLogDap;

typedef enum {
    SERIAL_LOG_EARLY = 0, /* medium not identified yet: RAM ring only */
    SERIAL_LOG_ACTIVE,    /* mirroring the trace onto the medium */
    SERIAL_LOG_RAM,       /* UEFI boot: mirroring into the fixed RAM ring */
    SERIAL_LOG_OFF
} SerialLogState;

static uint8_t serial_log_ring[SERIAL_LOG_RING_BYTES];
static uint32_t serial_log_ring_head;
static uint8_t serial_log_sector[SERIAL_LOG_SECTOR_BYTES];
/* The header has its own sector buffer: it is rewritten while the text sector
 * above is being filled, and rebuilding it in place would wipe the line in
 * flight. */
static uint8_t serial_log_header_sector[SERIAL_LOG_SECTOR_BYTES];
static uint32_t serial_log_fill;
static uint32_t serial_log_next_lba;
static uint32_t serial_log_end_lba;
static uint32_t serial_log_start_lba;
static uint8_t serial_log_drive;
static uint16_t serial_log_block_bytes = 512u; /* the BIOS's block size for it */
/* The slot's height comes from the pre-seeded header, so each medium can
 * reserve its own size (megabytes on the ISO and disk images, 64 KiB on a
 * floppy); the Makefile's SERIAL_LOG_SECTORS is only the fallback. */
static uint32_t serial_log_region_sectors = (uint32_t)HALOXOS_SERIAL_LOG_SECTORS;
/* The slot's FAT directory entry, cached at claim time so the recorded size
 * can follow the text without a BIOS read per update. */
static uint8_t serial_log_dirent[SERIAL_LOG_SECTOR_BYTES];
static uint32_t serial_log_dirent_lba;    /* 0 = the slot is not a file */
static uint32_t serial_log_dirent_offset;
static uint32_t serial_log_dirent_bytes;  /* size currently on the medium */
static bool serial_log_dirent_failed;     /* last size write did not land */
static bool serial_log_slot_read_only;
static uint32_t serial_log_write_failures;
static SerialLogState serial_log_state = SERIAL_LOG_EARLY;
static bool serial_log_busy; /* a flush is inside the BIOS; drop re-entrant chars */

/* --- diagnostics published in the slot header (see the layout above) --- */
static uint32_t serial_log_header_state = (uint32_t)SERIAL_LOG_STATE_OFF;
static uint32_t serial_log_boot_stage;     /* last breadcrumb serial_log_stage() saw */
static uint32_t serial_log_stored_bytes;   /* text bytes the medium really holds */
static uint32_t serial_log_last_error;     /* firmware status of the last failure */
static uint32_t serial_log_last_ah;        /* AH as the last INT 13h call returned it */
static bool serial_log_header_dirty = true;
static uint32_t serial_log_header_tick;
/* Rate limiting for the in-flight-sector rewrite; see SERIAL_LOG_PARTIAL_TICKS. */
static uint32_t serial_log_partial_tick;
static uint32_t serial_log_partial_fill;

static void serial_log_char(char ch);
static void serial_log_init(const MultibootInfo *mbi);
static uint32_t serial_log_find_slot(void);
static uint32_t serial_log_probe_slot(void);
static void serial_log_append(char ch);
static bool serial_log_flush_sector(void);
static bool serial_log_write_sector_at(uint32_t lba);
static void serial_log_header_update(void);
static void serial_log_replay_ring(void);
static void serial_log_stage(uint32_t step);
static void serial_log_frame(void);
static void serial_log_sync(void);
static void serial_log_ram_char(char ch);
static void serial_log_ram_begin(const MultibootInfo *mbi);

static void serial_log_reg_write(int field, uint32_t value) {
    volatile uint32_t *slot =
        (volatile uint32_t *)(void *)(rm_layout[RM_L_REGS] + rm_layout[field]);

    *slot = value;
}

static uint32_t serial_log_reg_read(int field) {
    const volatile uint32_t *slot =
        (const volatile uint32_t *)(const void *)(rm_layout[RM_L_REGS] + rm_layout[field]);

    return *slot;
}

/*
 * One INT 13h EDD call through the bridge.  `write` picks extended write
 * (AH=43h) over extended read (AH=42h); both move 512-byte blocks between the
 * medium and the scratch sector at rm_serial_log_buf.  False means the BIOS
 * refused it or the call faulted in firmware.  Fixed disks only: floppies go
 * through the CHS path below, because EDD is a fixed-disk interface.
 */
static bool serial_log_edd_call(uint32_t lba, uint16_t count, bool write) {
    SerialLogDap *dap = (SerialLogDap *)(uintptr_t)rm_serial_log_dap;

    dap->size = 0x10u;
    dap->reserved = 0u;
    dap->count = count;
    dap->offset = (uint16_t)(rm_serial_log_buf & 0xFu);
    dap->segment = (uint16_t)(rm_serial_log_buf >> 4);
    dap->lba = lba;
    dap->lba_high = 0u;

    serial_log_reg_write(RM_L_EAX, write ? 0x4300u : 0x4200u);
    serial_log_reg_write(RM_L_EBX, 0u);
    serial_log_reg_write(RM_L_ECX, 0u);
    serial_log_reg_write(RM_L_EDX, serial_log_drive);
    serial_log_reg_write(RM_L_ESI, rm_serial_log_dap);
    serial_log_reg_write(RM_L_EDI, 0u);
    serial_log_reg_write(RM_L_EBP, 0u);
    serial_log_reg_write(RM_L_ES, 0u);

    if (realmode_call_int13() != 0) {
        serial_log_last_ah = 0xFFu; /* the bridge itself never came back */
        return false;
    }
    /* Carry set or a nonzero AH is the BIOS saying no; AL may hold the block
     * count on success and is not part of the answer. */
    {
        uint32_t flags = serial_log_reg_read(RM_L_FLAGS);
        uint32_t eax = serial_log_reg_read(RM_L_EAX);

        serial_log_last_ah = (eax >> 8) & 0xFFu;
        if ((flags & 1u) != 0u && serial_log_last_ah == 0u) {
            serial_log_last_ah = 0x100u; /* carry with no code: the call faulted */
        }
        return (flags & 1u) == 0u && (eax & 0xFF00u) == 0u;
    }
}

/* Floppy geometry, read once per boot from INT 13h AH=08h. */
static uint8_t serial_log_chs_sectors;
static uint8_t serial_log_chs_heads;

static bool serial_log_chs_geometry(void) {
    uint32_t flags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;

    if (serial_log_chs_sectors != 0u) {
        return true;
    }
    serial_log_reg_write(RM_L_EAX, 0x0800u); /* get drive parameters */
    serial_log_reg_write(RM_L_EBX, 0u);
    serial_log_reg_write(RM_L_ECX, 0u);
    serial_log_reg_write(RM_L_EDX, serial_log_drive);
    serial_log_reg_write(RM_L_ESI, 0u);
    /* A floppy answers AH=08h by copying its parameter table to ES:DI, so DI
     * must point at scratch (0:0 would write into the interrupt table). */
    serial_log_reg_write(RM_L_EDI, 0x400u);
    serial_log_reg_write(RM_L_EBP, 0u);
    serial_log_reg_write(RM_L_ES, (uint32_t)(rm_serial_log_buf >> 4));
    if (realmode_call_int13() != 0) {
        return false;
    }
    flags = serial_log_reg_read(RM_L_FLAGS);
    eax = serial_log_reg_read(RM_L_EAX);
    ecx = serial_log_reg_read(RM_L_ECX);
    edx = serial_log_reg_read(RM_L_EDX);
    if ((flags & 1u) != 0u || (eax & 0xFF00u) != 0u) {
        return false;
    }
    serial_log_chs_sectors = (uint8_t)(ecx & 0x3Fu);
    serial_log_chs_heads = (uint8_t)(((edx >> 8) & 0xFFu) + 1u);
    return serial_log_chs_sectors != 0u && serial_log_chs_heads != 0u;
}

/*
 * INT 13h AH=00h: reset the disk system and recalibrate the drive.  Both the
 * floppy CHS path and the hard-disk EDD path land in the same firmware entry
 * point here - a BIOS that refused a write because its own cached geometry or
 * media state went stale accepts it again after this, which on real hardware
 * is the difference between a capture that dies on its first hiccup and one
 * that carries on.  DL selects the drive in both cases.
 */
static bool serial_log_disk_reset(void) {
    uint32_t flags;
    uint32_t eax;

    serial_log_reg_write(RM_L_EAX, 0x0000u);
    serial_log_reg_write(RM_L_EBX, 0u);
    serial_log_reg_write(RM_L_ECX, 0u);
    serial_log_reg_write(RM_L_EDX, serial_log_drive);
    serial_log_reg_write(RM_L_ESI, 0u);
    serial_log_reg_write(RM_L_EDI, 0u);
    serial_log_reg_write(RM_L_EBP, 0u);
    serial_log_reg_write(RM_L_ES, 0u);

    if (realmode_call_int13() != 0) {
        return false;
    }
    flags = serial_log_reg_read(RM_L_FLAGS);
    eax = serial_log_reg_read(RM_L_EAX);
    serial_log_last_ah = (eax >> 8) & 0xFFu;
    return (flags & 1u) == 0u && (eax & 0xFF00u) == 0u;
}

/*
 * One INT 13h CHS call: AH=03h write or AH=02h read with the geometry the
 * firmware reported.  A floppy has no EDD entry, so this is the only way to
 * reach one.  The buffer is addressed through ES:BX, which the bridge loads
 * from the register file like every other BIOS call.
 *
 * The first access after a boot often comes back with a media or controller
 * status: the firmware left the drive idle while the bootloader ran, and a
 * floppy reports that as a failure until the disk system is reset.  The
 * classic remedy is one AH=00h reset, then the same call again.
 */
static bool serial_log_chs_call(uint32_t lba, uint16_t count, bool write) {
    uint32_t track_sectors;
    uint32_t cylinder;
    uint32_t remainder;
    uint32_t head;
    uint32_t sector;

    if (count == 0u || lba > 0x00FFFFFFu || !serial_log_chs_geometry()) {
        return false;
    }
    track_sectors = (uint32_t)serial_log_chs_sectors * (uint32_t)serial_log_chs_heads;
    cylinder = lba / track_sectors;
    remainder = lba % track_sectors;
    head = remainder / serial_log_chs_sectors;
    sector = remainder % serial_log_chs_sectors + 1u;
    if (cylinder > 1023u) {
        return false;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        uint32_t flags;
        uint32_t eax;

        serial_log_reg_write(RM_L_EAX, ((write ? 3u : 2u) << 8) | (uint32_t)count);
        serial_log_reg_write(RM_L_EBX, (uint32_t)(rm_serial_log_buf & 0xFu));
        serial_log_reg_write(RM_L_ECX, ((cylinder & 0xFFu) << 8) |
                                           ((cylinder >> 8) << 6) |
                                           (sector & 0x3Fu));
        serial_log_reg_write(RM_L_EDX, (head << 8) | serial_log_drive);
        serial_log_reg_write(RM_L_ESI, 0u);
        serial_log_reg_write(RM_L_EDI, 0u);
        serial_log_reg_write(RM_L_EBP, 0u);
        serial_log_reg_write(RM_L_ES, (uint32_t)(rm_serial_log_buf >> 4));

        if (realmode_call_int13() != 0) {
            return false;
        }
        flags = serial_log_reg_read(RM_L_FLAGS);
        eax = serial_log_reg_read(RM_L_EAX);
        if ((flags & 1u) == 0u && (eax & 0xFF00u) == 0u) {
            return true;
        }
        if (attempt == 0 && !serial_log_disk_reset()) {
            return false;
        }
    }
    return false;
}

/* Drive numbers below 0x80 are floppies: they get the CHS path. */
static bool serial_log_bios_call(uint32_t lba, uint16_t count, bool write) {
    if (serial_log_drive < 0x80u) {
        return serial_log_chs_call(lba, count, write);
    }
    return serial_log_edd_call(lba, count, write);
}

static void serial_log_put32(uint8_t *dest, uint32_t value) {
    dest[0] = (uint8_t)value;
    dest[1] = (uint8_t)(value >> 8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

/* Small formatters for the status line below; the kernel's tracing helpers
 * write to COM1 and cannot be used to build a buffer. */
static uint32_t serial_log_status_str(uint8_t *dest, uint32_t offset, uint32_t limit,
                                      const char *text) {
    for (uint32_t i = 0u; text[i] != '\0' && offset < limit; ++i) {
        dest[offset++] = (uint8_t)text[i];
    }
    return offset;
}

static uint32_t serial_log_status_uint(uint8_t *dest, uint32_t offset, uint32_t limit,
                                       uint32_t value) {
    uint8_t digits[10];
    uint32_t count = 0u;

    if (value == 0u) {
        if (offset < limit) {
            dest[offset++] = '0';
        }
        return offset;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (uint8_t)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u && offset < limit) {
        dest[offset++] = digits[--count];
    }
    return offset;
}

static uint32_t serial_log_status_hex(uint8_t *dest, uint32_t offset, uint32_t limit,
                                      uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";

    for (int shift = 28; shift >= 0 && offset < limit; shift -= 4) {
        dest[offset++] = (uint8_t)hex[(value >> shift) & 0x0Fu];
    }
    return offset;
}

/* The state dword's name, for the status line. */
static const char *serial_log_state_name(uint32_t state) {
    switch (state) {
    case (uint32_t)SERIAL_LOG_STATE_ACTIVE:   return "ACTIVE";
    case (uint32_t)SERIAL_LOG_STATE_EARLY:    return "EARLY";
    case (uint32_t)SERIAL_LOG_STATE_UEFI:     return "UEFI";
    case (uint32_t)SERIAL_LOG_STATE_NOSLOT:   return "NO-SLOT";
    case (uint32_t)SERIAL_LOG_STATE_READONLY: return "READ-ONLY";
    case (uint32_t)SERIAL_LOG_STATE_WRITEFAIL:return "WRITE-FAILED";
    case (uint32_t)SERIAL_LOG_STATE_FULL:     return "SLOT-FULL";
    default:                                  return "OFF";
    }
}

/*
 * Lay the human-readable half of the header out: one line naming the capture
 * state and where it lives, one line of counters.  It is the part of the file
 * a Notepad user sees first, and on a capture that stopped early it is the
 * answer to "where did this stop and why".
 */
static void serial_log_build_status(uint8_t *dest) {
    uint32_t offset = SERIAL_LOG_STATUS_OFFSET;
    const uint32_t limit = SERIAL_LOG_STATUS_OFFSET + SERIAL_LOG_STATUS_MAX;

    offset = serial_log_status_str(dest, offset, limit, "capture=");
    offset = serial_log_status_str(dest, offset, limit,
                                   serial_log_state_name(serial_log_header_state));
    offset = serial_log_status_str(dest, offset, limit, " slot_lba=0x");
    offset = serial_log_status_hex(dest, offset, limit, serial_log_start_lba);
    offset = serial_log_status_str(dest, offset, limit, " drive=0x");
    offset = serial_log_status_hex(dest, offset, limit, (uint32_t)serial_log_drive);
    offset = serial_log_status_str(dest, offset, limit, " block=");
    offset = serial_log_status_uint(dest, offset, limit, (uint32_t)serial_log_block_bytes);
    offset = serial_log_status_str(dest, offset, limit, "\r\n");
    offset = serial_log_status_str(dest, offset, limit, "stored_bytes=");
    offset = serial_log_status_uint(dest, offset, limit, serial_log_stored_bytes);
    offset = serial_log_status_str(dest, offset, limit, " boot_stage=");
    offset = serial_log_status_uint(dest, offset, limit, serial_log_boot_stage);
    offset = serial_log_status_str(dest, offset, limit, " failed_writes=");
    offset = serial_log_status_uint(dest, offset, limit, serial_log_write_failures);
    offset = serial_log_status_str(dest, offset, limit, " last_error=0x");
    offset = serial_log_status_hex(dest, offset, limit, serial_log_last_error);
    if (offset < limit) {
        dest[offset] = '\0';
    }
}

/* Fill the header sector with a fresh copy, dated from the RTC. */
static void serial_log_build_header(void) {
    uint32_t year = 2000u + (uint32_t)from_bcd(cmos_read(0x09));
    uint32_t month = (uint32_t)from_bcd(cmos_read(0x08));
    uint32_t day = (uint32_t)from_bcd(cmos_read(0x07));
    uint32_t hour = (uint32_t)from_bcd(cmos_read(0x04));
    uint32_t minute = (uint32_t)from_bcd(cmos_read(0x02));
    uint32_t second = (uint32_t)from_bcd(cmos_read(0x00));

    for (uint32_t i = 0; i < SERIAL_LOG_SECTOR_BYTES; ++i) {
        serial_log_header_sector[i] = 0u;
    }
    serial_log_put32(serial_log_header_sector + 0u, SERIAL_LOG_MAGIC);
    serial_log_put32(serial_log_header_sector + 4u, SERIAL_LOG_VERSION);
    serial_log_put32(serial_log_header_sector + 8u, SERIAL_LOG_SECTOR_BYTES);
    serial_log_put32(serial_log_header_sector + 12u, serial_log_region_sectors);
    serial_log_put32(serial_log_header_sector + 16u, (uint32_t)serial_log_drive);
    serial_log_put32(serial_log_header_sector + 20u, year * 10000u + month * 100u + day);
    serial_log_put32(serial_log_header_sector + 24u, hour * 10000u + minute * 100u + second);
    serial_log_put32(serial_log_header_sector + 28u, 0u); /* flags, reserved */
    serial_log_put32(serial_log_header_sector + SERIAL_LOG_HDR_DIRENT_LBA,
                     serial_log_dirent_lba);
    serial_log_put32(serial_log_header_sector + SERIAL_LOG_HDR_DIRENT_OFFSET,
                     serial_log_dirent_offset);
    serial_log_put32(serial_log_header_sector + SERIAL_LOG_HDR_STAGE,
                     serial_log_boot_stage);
    serial_log_put32(serial_log_header_sector + SERIAL_LOG_HDR_STORED_BYTES,
                     serial_log_stored_bytes);
    serial_log_put32(serial_log_header_sector + SERIAL_LOG_HDR_ERROR,
                     serial_log_last_error);
    serial_log_put32(serial_log_header_sector + SERIAL_LOG_HDR_FAILURES,
                     serial_log_write_failures);
    serial_log_put32(serial_log_header_sector + SERIAL_LOG_HDR_STATE,
                     serial_log_header_state);

    /* Fields above are what the kernel and tools/readseriallog.py read; the
     * text is there so a Notepad view of SERIAL.LOG explains itself instead
     * of showing nothing but the raw header. */
    serial_log_build_status(serial_log_header_sector);
    {
        static const char banner[] =
            "HaloxOS debug serial.log - COM1 trace captured on the boot medium.\r\n"
            "This 512-byte sector is the slot header, not trace text; the trace\r\n"
            "starts in the next sector and tools/readseriallog.py strips it.\r\n";

        for (uint32_t i = 0u; i + 1u < sizeof(banner); ++i) {
            if (SERIAL_LOG_BANNER_OFFSET + i < SERIAL_LOG_SECTOR_BYTES) {
                serial_log_header_sector[SERIAL_LOG_BANNER_OFFSET + i] = (uint8_t)banner[i];
            }
        }
    }
}

/*
 * Put the current diagnostics on the medium.  The slot header is one sector,
 * so this is the cheapest way to make a capture that is about to die say how
 * far it got: it runs on every stage change and state change, and once a
 * second while the capture is live to keep the byte count fresh.
 *
 * A write failure here is deliberately swallowed.  The header is a diagnosis
 * of the capture, not the capture, and a machine whose firmware refuses the
 * header sector is about to say so through the text path's own failure
 * accounting.
 */
static void serial_log_header_update(void) {
    if (serial_log_state == SERIAL_LOG_EARLY || serial_log_start_lba == 0u ||
        serial_log_busy) {
        return;
    }
    serial_log_header_dirty = false;
    serial_log_header_tick = timer_ticks;
    serial_log_build_header();
    serial_log_busy = true;
    memcpy_local((void *)(uintptr_t)rm_serial_log_buf, serial_log_header_sector,
                 SERIAL_LOG_SECTOR_BYTES);
    (void)serial_log_bios_call(serial_log_start_lba, 1u, true);
    serial_log_busy = false;
}

/*
 * Record how far the kernel got.  Called from the boot-progress milestones in
 * init.c, so a capture that stops carries the last milestone it saw, and a
 * machine whose screen froze can be told apart from one whose medium did.
 * The milestone is kept even when there is no slot to write it to (a UEFI
 * boot, or a medium without a reservation), because the number is still the
 * right answer once a capture does start.
 */
static void serial_log_stage(uint32_t step) {
    if (step == serial_log_boot_stage) {
        return;
    }
    serial_log_boot_stage = step;
    serial_log_header_dirty = true;
    serial_log_header_update();
}

/* Does `lba` start a reserved slot?  The pre-seeded header is the proof. */
static bool serial_log_slot_present(uint32_t lba) {
    if (!serial_log_bios_call(lba, 1u, false)) {
        return false;
    }
    return *(const volatile uint32_t *)(uintptr_t)rm_serial_log_buf == SERIAL_LOG_MAGIC;
}

/*
 * The BIOS's block size for the current drive, from INT 13h AH=48h.  A disk
 * or USB stick answers 512; an optical drive answers 2048, and the same
 * medium is then addressed in 2048-byte blocks - which is why the ISO's
 * volume descriptor and its slot have to be looked for at two different
 * places.  Anything unexpected falls back to 512.
 */
static uint16_t serial_log_drive_block_bytes(void) {
    volatile uint16_t *result = (volatile uint16_t *)(uintptr_t)rm_serial_log_buf;
    uint16_t bytes;

    if (serial_log_drive < 0x80u) {
        return 512u; /* a floppy's INT 13h block is one 512-byte sector */
    }

    result[0] = 30u; /* the size of the buffer the BIOS may fill in */
    serial_log_reg_write(RM_L_EAX, 0x4800u);
    serial_log_reg_write(RM_L_EBX, 0u);
    serial_log_reg_write(RM_L_ECX, 0u);
    serial_log_reg_write(RM_L_EDX, serial_log_drive);
    serial_log_reg_write(RM_L_ESI, rm_serial_log_buf);
    serial_log_reg_write(RM_L_EDI, 0u);
    serial_log_reg_write(RM_L_EBP, 0u);
    serial_log_reg_write(RM_L_ES, 0u);

    if (realmode_call_int13() != 0 ||
        (serial_log_reg_read(RM_L_FLAGS) & 1u) != 0u ||
        (serial_log_reg_read(RM_L_EAX) & 0xFF00u) != 0u) {
        return 512u;
    }
    bytes = result[12]; /* offset 24 of the result block */
    return bytes == 2048u ? 2048u : 512u;
}

/*
 * The ISO slot: read the volume descriptor at ISO sector 16 and take the
 * volume-space size as the end of the image.  The region follows it.  Returns
 * 0 when the medium is not an ISO or the descriptor cannot be read.
 *
 * The descriptor is read in the drive's own blocks, so a 512-byte-block
 * medium carries it at LBA 64 and the region begins at four 512-byte sectors
 * per volume sector; a 2048-byte-block medium (a CD) carries it at LBA 16 and
 * the region begins at the volume number itself.
 */
static uint32_t serial_log_find_iso_slot(void) {
    bool blocks_2048 = serial_log_block_bytes == 2048u;
    const volatile uint8_t *pvd;
    uint32_t volume;

    if (!serial_log_bios_call(blocks_2048 ? 16u : 64u, 1u, false)) {
        return 0u;
    }
    pvd = (const volatile uint8_t *)(uintptr_t)rm_serial_log_buf;
    if (pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1') {
        return 0u;
    }
    volume = (uint32_t)pvd[80] | ((uint32_t)pvd[81] << 8) |
             ((uint32_t)pvd[82] << 16) | ((uint32_t)pvd[83] << 24);
    if (volume == 0u || volume > 0x400000u) { /* 8 GiB is already absurd */
        return 0u;
    }
    return blocks_2048 ? volume : volume * 4u;
}

/*
 * The EFI loader always names the platform on the command line.  A UEFI boot
 * has no real-mode BIOS behind it, so INT 13h must not be issued at all - the
 * reset with nothing on screen is exactly the failure vbe.c's own probe was
 * made lazy to avoid.
 */
static bool serial_log_cmdline_is_efi(const MultibootInfo *mbi) {
    static const char key[] = "haloxos_platform=efi";
    const char *cursor;

    if (mbi == NULL || (mbi->flags & (1u << 2)) == 0u || mbi->cmdline == 0u) {
        return false;
    }
    cursor = (const char *)(uintptr_t)mbi->cmdline;
    while (*cursor != '\0') {
        size_t length = 0u;
        bool match = true;

        while (cursor[length] != '\0' && cursor[length] != ' ') {
            ++length;
        }
        if (length == sizeof(key) - 1u) {
            for (size_t i = 0u; i < length; ++i) {
                if (cursor[i] != key[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        cursor += length;
        while (*cursor == ' ') {
            ++cursor;
        }
    }
    return false;
}

/* The IVT entry for INT 13h, held in a volatile object so the reads through
 * it are not folded into a fixed address the compiler can argue about (vbe.c
 * reads the INT 10h entry the same way).  A plausible handler lives in ROM
 * (F segment) or in an Extended BIOS Data Area thunk at 0x9FC0-0x9FFF -
 * which is exactly where CSM-era AMI firmware hooks its USB and ATAPI
 * emulation layers, and where the original ROM-only test silently refused
 * this machine's perfectly working BIOS, leaving bios-serial.log in its
 * seeded state while the boot's trace went nowhere.  On a UEFI boot the
 * table is not the BIOS's, so this stays false alongside the command-line
 * check above. */
static volatile uint32_t serial_log_int13_vector_address = 0x004Cu;

static bool serial_log_bios_disk_services(void) {
    const volatile uint8_t *vector =
        (const volatile uint8_t *)(uintptr_t)serial_log_int13_vector_address;
    uint16_t offset = (uint16_t)(vector[0] | ((uint16_t)vector[1] << 8));
    uint16_t segment = (uint16_t)(vector[2] | ((uint16_t)vector[3] << 8));

    return offset != 0u && segment >= 0x8000u;
}

/*
 * Claim the slot: write a fresh header, read it back.  The read-back is what
 * separates a writable medium from a pressed CD or a write-protected stick -
 * both fail the write, and from then on COM1 keeps working with the capture
 * switched off.
 */
static bool serial_log_claim_slot(uint32_t lba) {
    uint32_t region;
    uint32_t dirent_lba;
    uint32_t dirent_offset;

    /* The pre-seeded header is the reservation, and it also names the slot's
     * height and its FAT directory entry.  The kernel must trust it, not the
     * compile-time default, or a floppy boot would run past its slot into
     * live FAT data. */
    if (!serial_log_slot_present(lba)) {
        return false;
    }
    region = *(const volatile uint32_t *)(uintptr_t)(rm_serial_log_buf + 12u);
    if (region < 2u || region > 0x20000u) {
        return false;
    }
    serial_log_region_sectors = region;

    /* Role flags (dword 7, byte offset 28): bit 1 marks the UEFI half's
     * slot, the file the EFI loader writes with the previous boot's
     * recovered kernel trace.  The live capture never touches it - the two
     * boot modes each own their file - while an older medium's single
     * unflagged slot stays the shared home it always was. */
    if ((*(const volatile uint32_t *)(uintptr_t)(rm_serial_log_buf + 28u) &
         0x2u) != 0u) {
        serial_trace("INFO", "serial.log: the UEFI half's slot, left alone");
        return false;
    }

    /* Read the entry's location before the fresh header is written over the
     * sector it came from, and cache the entry's own sector while the update
     * path still has no read in it.  A zero sector means the slot is not a
     * file (the ISO's raw region), and then the size walk is skipped. */
    dirent_lba = *(const volatile uint32_t *)(uintptr_t)
                     (rm_serial_log_buf + SERIAL_LOG_HDR_DIRENT_LBA);
    dirent_offset = *(const volatile uint32_t *)(uintptr_t)
                        (rm_serial_log_buf + SERIAL_LOG_HDR_DIRENT_OFFSET);
    serial_log_dirent_lba = 0u;
    serial_log_dirent_offset = 0u;
    serial_log_dirent_bytes = 0u;
    serial_log_dirent_failed = false;
    if (dirent_lba != 0u &&
        dirent_offset + 32u <= SERIAL_LOG_SECTOR_BYTES &&
        serial_log_bios_call(dirent_lba, 1u, false)) {
        memcpy_local(serial_log_dirent, (const void *)(uintptr_t)rm_serial_log_buf,
                     SERIAL_LOG_SECTOR_BYTES);
        serial_log_dirent_lba = dirent_lba;
        serial_log_dirent_offset = dirent_offset;
        serial_log_dirent_bytes =
            (uint32_t)serial_log_dirent[dirent_offset + SERIAL_LOG_DIRENT_SIZE_OFFSET] |
            ((uint32_t)serial_log_dirent[dirent_offset + SERIAL_LOG_DIRENT_SIZE_OFFSET + 1u] << 8) |
            ((uint32_t)serial_log_dirent[dirent_offset + SERIAL_LOG_DIRENT_SIZE_OFFSET + 2u] << 16) |
            ((uint32_t)serial_log_dirent[dirent_offset + SERIAL_LOG_DIRENT_SIZE_OFFSET + 3u] << 24);
    }

    serial_log_build_header();
    memcpy_local((void *)(uintptr_t)rm_serial_log_buf, serial_log_header_sector,
                 SERIAL_LOG_SECTOR_BYTES);
    if (!serial_log_bios_call(lba, 1u, true) || !serial_log_slot_present(lba)) {
        return false;
    }
    serial_log_start_lba = lba;
    serial_log_next_lba = lba + 1u;
    serial_log_end_lba = lba + region;
    serial_log_fill = 0u;
    serial_log_partial_fill = 0u;
    serial_log_partial_tick = timer_ticks;
    serial_log_write_failures = 0u;
    serial_log_last_error = 0u;
    serial_log_stored_bytes = 0u;
    serial_log_header_state = (uint32_t)SERIAL_LOG_STATE_ACTIVE;
    serial_log_header_dirty = true;
    return true;
}

/*
 * Which drive carries the slot?  The zstd loader forwards GRUB's multiboot
 * info unchanged, and GRUB does not always know the drive it came from: a
 * BIOS boot can report 0xFF, which is not a drive at all.  The slot is the
 * authority instead - a medium with the 'HXSL' header at a slot position is
 * the right medium - so the drives the firmware could have booted from are
 * probed in order: the reported drive first when it is a disk, then the
 * usual BIOS numbers (0x80..0x87 hard disks and USB sticks, 0x9F and
 * 0xE0..0xE7 the CD-ROM slots an El Torito boot uses).  Only reads happen
 * here; a drive that does not answer, or carries no header, is skipped.
 *
 * This also makes the capture work when the HaloxOS medium is not the first
 * disk in the machine, which is the normal real-hardware case.
 */
static uint32_t serial_log_find_slot(void) {
    static const uint8_t candidate_drives[] = {
        0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u,
        0x9Fu, 0xE0u, 0xE1u, 0xE2u, 0xE3u, 0xE4u, 0xE5u, 0xE6u, 0xE7u,
        0x00u /* the floppy target's drive, tried last on a disk boot */
    };
    bool have_reported = boot_drive_valid &&
                         (boot_drive_number < 0x02u || boot_drive_number >= 0x80u);

    serial_log_slot_read_only = false;
    if (have_reported) {
        uint32_t slot;

        serial_log_drive = boot_drive_number;
        serial_log_block_bytes = serial_log_drive_block_bytes();
        slot = serial_log_probe_slot();
        if (slot != 0u) {
            return slot;
        }
    }
    for (size_t i = 0u; i < sizeof(candidate_drives); ++i) {
        uint32_t slot;

        if (have_reported && candidate_drives[i] == boot_drive_number) {
            continue;
        }
        serial_log_drive = candidate_drives[i];
        serial_log_block_bytes = serial_log_drive_block_bytes();
        slot = serial_log_probe_slot();
        if (slot != 0u) {
            return slot;
        }
    }
    return 0u;
}

/*
 * The two fixed slot shapes, in the order the drive number suggests.  Both
 * are tried on every 512-byte-block medium: the floppy image is normally
 * written to a USB stick, where the firmware reports a hard-disk number
 * (0x80+) even though the build pinned its FAT12 file at the floppy LBA.
 * Picking one LBA from the drive number would look only at the disk slot
 * there and miss the floppy slot entirely - which is exactly how a floppy
 * build written to a stick lost its capture.  The 'HXSL' header decides
 * either way; a read that fails, or a sector without the header, moves on
 * to the other shape.  A real floppy's second probe is free: the disk LBA is
 * past cylinder 1023, so the CHS path rejects it before any BIOS call.
 */
static uint32_t serial_log_probe_fixed_slot(void) {
    uint32_t floppy_lba = (uint32_t)HALOXOS_SERIAL_LOG_FLOPPY_LBA;
    uint32_t disk_lba = (uint32_t)HALOXOS_SERIAL_LOG_LBA;
    uint32_t first = serial_log_drive < 0x80u ? floppy_lba : disk_lba;
    uint32_t second = serial_log_drive < 0x80u ? disk_lba : floppy_lba;

    if (serial_log_slot_present(first)) {
        return first;
    }
    if (serial_log_slot_present(second)) {
        return second;
    }
    return 0u;
}

/*
 * One drive's possible slot shapes.  An ISO answers first even when it was
 * dd'd to a USB stick: its PVD names the end of the volume, and the slot sits
 * right behind it.  A plain disk image has no such descriptor and falls
 * through to the fixed LBAs the FAT files were pinned to.
 */
static uint32_t serial_log_probe_slot(void) {
    uint32_t iso_lba = serial_log_find_iso_slot();

    if (iso_lba != 0u) {
        /* One line on an ISO boot, none on a plain disk image: the PVD was
         * readable, so this is where the ISO's slot has to be. */
        serial_trace_hex_value("INFO", "serial.log: ISO volume end LBA", iso_lba);
        if (serial_log_slot_present(iso_lba)) {
            /* A 2048-byte-block medium is an optical drive: the slot is real,
             * but the flush writes 512-byte blocks and a pressed CD cannot be
             * written at all - init reports it instead of trying. */
            serial_log_slot_read_only = serial_log_block_bytes == 2048u;
            return iso_lba;
        }
    }
    if (serial_log_block_bytes == 512u) {
        return serial_log_probe_fixed_slot();
    }
    return 0u;
}

/*
 * Replay the loader's pre-kernel trace into the slot first, so serial.log
 * opens with the lines the zstd loader printed while it decompressed - the
 * kernel's first byte is already too late to explain a boot that died in the
 * loader.  The magic is the proof the block is this boot's trace and not
 * stale RAM; a loader too old to leave one simply makes this a no-op.
 */
static void serial_log_emit_loader_trace(void) {
    const volatile uint32_t *header =
        (const volatile uint32_t *)(uintptr_t)SERIAL_LOG_LOADER_TRACE_ADDR;
    const volatile char *text =
        (const volatile char *)(uintptr_t)(SERIAL_LOG_LOADER_TRACE_ADDR +
                                           SERIAL_LOG_LOADER_TRACE_HEADER);
    uint32_t bytes;

    if (header[0] != SERIAL_LOG_LOADER_TRACE_MAGIC) {
        return;
    }
    bytes = header[1];
    if (bytes > (uint32_t)SERIAL_LOG_LOADER_TRACE_MAX) {
        bytes = (uint32_t)SERIAL_LOG_LOADER_TRACE_MAX;
    }
    for (uint32_t i = 0u; i < bytes; ++i) {
        serial_log_append(text[i]);
    }
}

static void serial_log_replay_ring(void) {
    uint32_t first = serial_log_ring_head > SERIAL_LOG_RING_BYTES
                         ? serial_log_ring_head - SERIAL_LOG_RING_BYTES
                         : 0u;

    for (uint32_t i = first; i < serial_log_ring_head; ++i) {
        serial_log_append((char)serial_log_ring[i % SERIAL_LOG_RING_BYTES]);
    }
}

/*
 * The RAM-ring half of the capture (UEFI boots).  The byte is stored before
 * the counter that publishes it, so a crash mid-character costs at most that
 * character; the stage is refreshed on every character because it changes
 * rarely and the loader that harvests this ring has no other way to learn it.
 */
static uint32_t serial_log_ram_ring_addr;   /* ring base chosen this boot */

static void serial_log_ram_char(char ch) {
    volatile uint32_t *header =
        (volatile uint32_t *)(uintptr_t)serial_log_ram_ring_addr;
    volatile uint8_t *text =
        (volatile uint8_t *)(uintptr_t)(serial_log_ram_ring_addr +
                                        SERIAL_LOG_RAM_TEXT_OFFSET);
    uint32_t total = header[1];

    text[total % SERIAL_LOG_RAM_TEXT_BYTES] = (uint8_t)ch;
    header[3] = serial_log_boot_stage;
    header[1] = total + 1u;
}

/* Is `base` real, usable memory according to the map the bootloader left?
 * A machine with less than 768 MiB simply leaves the address unmapped, and
 * writing into it would be writing into whatever the chipset answers there. */
static bool serial_log_ram_addr_is_memory(uint32_t base, const MultibootInfo *mbi) {
    uintptr_t cursor;
    uintptr_t end;

    if (mbi == NULL || (mbi->flags & (1u << 6)) == 0 ||
        mbi->mmap_addr == 0 || mbi->mmap_length == 0) {
        return false;
    }
    cursor = (uintptr_t)mbi->mmap_addr;
    end = cursor + mbi->mmap_length;
    while (cursor + sizeof(uint32_t) <= end) {
        const MultibootMmapEntry *entry = (const MultibootMmapEntry *)cursor;
        uint64_t limit;

        if (cursor + entry->size + sizeof(uint32_t) > end) {
            break;
        }
        cursor += entry->size + sizeof(uint32_t);
        if (entry->type != 1u) {
            continue;
        }
        limit = entry->base_addr + entry->length;
        if (entry->base_addr <= (uint64_t)base &&
            limit >= (uint64_t)base + SERIAL_LOG_RAM_RING_BYTES) {
            return true;
        }
    }
    return false;
}

/*
 * Publish a fresh ring header and replay the early RAM ring into it, so the
 * first characters of the boot - everything printed before the command line
 * was even read - are part of the recovered trace too.  The magic and the
 * capacity go down before the count: a header with a stale or zero count is
 * a dead ring rather than half a log.
 *
 * The high ring is used when the memory map proves it exists, because the
 * next boot's firmware reuses low memory long before its EFI loader runs;
 * the low ring next to the handoff block remains for the machines where
 * 768 MiB is not there at all.
 */
static void serial_log_ram_begin(const MultibootInfo *mbi) {
    uint32_t first = serial_log_ring_head > SERIAL_LOG_RING_BYTES
                         ? serial_log_ring_head - SERIAL_LOG_RING_BYTES
                         : 0u;
    volatile uint32_t *header;

    serial_log_ram_ring_addr = serial_log_ram_addr_is_memory(
                                   SERIAL_LOG_RAM_RING_HIGH, mbi)
                                   ? SERIAL_LOG_RAM_RING_HIGH
                                   : SERIAL_LOG_RAM_RING_LOW;
    header = (volatile uint32_t *)(uintptr_t)serial_log_ram_ring_addr;
    header[0] = SERIAL_LOG_RAM_RING_MAGIC;
    header[2] = SERIAL_LOG_RAM_TEXT_BYTES;
    header[3] = serial_log_boot_stage;
    header[1] = 0u;
    for (uint32_t i = first; i < serial_log_ring_head; ++i) {
        serial_log_ram_char((char)serial_log_ring[i % SERIAL_LOG_RING_BYTES]);
    }
    serial_trace_hex_value("INFO", "serial.log: RAM trace ring at",
                           serial_log_ram_ring_addr);
}

static void serial_log_init(const MultibootInfo *mbi) {
    uint32_t slot;

    if (serial_log_state != SERIAL_LOG_EARLY) {
        return;
    }
    if (serial_log_cmdline_is_efi(mbi)) {
        /* A UEFI boot has no real-mode disk services and no way to reach the
         * medium, so the kernel's half of the trace is kept in the fixed RAM
         * ring instead (see the note on SERIAL_LOG_RAM_RING_ADDR): the next
         * boot's EFI loader harvests it into SERIAL.LOG before its own
         * firmware services end.  The capture itself keeps running - every
         * kernel print from here on is recoverable after a warm reset. */
        serial_log_state = SERIAL_LOG_RAM;
        serial_log_header_state = (uint32_t)SERIAL_LOG_STATE_UEFI;
        serial_log_ram_begin(mbi);
        serial_trace("WARNING",
                     "serial.log: UEFI boot, trace kept in RAM; the next "
                     "boot's EFI loader writes it into SERIAL.LOG");
        return;
    }
    if (!serial_log_bios_disk_services()) {
        serial_log_state = SERIAL_LOG_OFF;
        serial_log_header_state = (uint32_t)SERIAL_LOG_STATE_UEFI;
        serial_trace_hex_value("WARNING",
                               "serial.log: INT13 vector rejected (cs:ip)",
                               *(const volatile uint32_t *)(uintptr_t)0x4Cu);
        serial_trace("WARNING", "serial.log: no BIOS disk services");
        return;
    }

    slot = serial_log_find_slot();
    if (slot == 0u) {
        serial_log_state = SERIAL_LOG_OFF;
        serial_log_header_state = (uint32_t)SERIAL_LOG_STATE_NOSLOT;
        serial_trace("WARNING", "serial.log: no reserved slot on the boot medium");
        return;
    }
    if (serial_log_slot_read_only) {
        serial_log_state = SERIAL_LOG_OFF;
        serial_log_header_state = (uint32_t)SERIAL_LOG_STATE_READONLY;
        serial_trace("WARNING", "serial.log: CD-ROM medium is read-only, capture off");
        return;
    }

    if (!serial_log_claim_slot(slot)) {
        serial_log_state = SERIAL_LOG_OFF;
        serial_log_header_state = (uint32_t)SERIAL_LOG_STATE_READONLY;
        serial_trace("WARNING", "serial.log: slot is read-only or write-protected");
        return;
    }

    serial_log_state = SERIAL_LOG_ACTIVE;
    serial_log_emit_loader_trace();
    serial_log_replay_ring();
    if (serial_log_state != SERIAL_LOG_ACTIVE) {
        return; /* a flush failed while replaying; the warning went to COM1 */
    }
    serial_trace_hex_value("INFO", "serial.log capture slot LBA", slot);
    serial_trace_hex_value("INFO", "serial.log capture drive", serial_log_drive);
}

static void serial_log_char(char ch) {
    if (serial_log_state == SERIAL_LOG_EARLY) {
        serial_log_ring[serial_log_ring_head % SERIAL_LOG_RING_BYTES] = (uint8_t)ch;
        serial_log_ring_head++;
        return;
    }
    if (serial_log_state == SERIAL_LOG_RAM) {
        serial_log_ram_char(ch);
        return;
    }
    if (serial_log_state != SERIAL_LOG_ACTIVE || serial_log_busy) {
        return;
    }
    serial_log_append(ch);
}

static void serial_log_append(char ch) {
    serial_log_sector[serial_log_fill++] = (uint8_t)ch;
    if (serial_log_fill >= SERIAL_LOG_SECTOR_BYTES) {
        if (serial_log_flush_sector()) {
            serial_log_next_lba++;
        }
        /* On a failed write the sector at this LBA keeps whatever the failed
         * call left there; starting a fresh one means the next successful
         * write replaces it, so only this sector is lost. */
        serial_log_fill = 0u;
        /* A fresh sector is not "already stored": the frame loop may put it
         * down as soon as it has something in it. */
        serial_log_partial_fill = 0u;
        serial_log_partial_tick = timer_ticks;
        return;
    }
    if (ch == '\n') {
        (void)serial_log_flush_sector();
        /* The line just landed; nothing has been appended since, so the frame
         * loop has no reason to repeat the write. */
        serial_log_partial_fill = serial_log_fill;
        serial_log_partial_tick = timer_ticks;
    }
}

/*
 * Keep the FAT directory entry's recorded size in step with the text, so
 * Explorer and Notepad show the log itself instead of a multi-megabyte blank
 * file: without this the entry keeps the whole reserved slot as its size and
 * Notepad spends the file scrolling through zeros.
 *
 * The size covers the sector being filled as well, so the tail of a hang is
 * always inside the file, and the value only changes when a sector boundary
 * is crossed - one small directory write per 512 bytes of log rather than one
 * per line, which also keeps the root directory (the boot files' own entries
 * share that sector) out of the write path as much as possible.  A failed
 * update is ignored: the text on the medium is what matters, and the reader
 * walks sectors, not the entry.
 */
static void serial_log_dirent_update(void) {
    uint32_t sectors;
    uint32_t bytes;
    uint8_t *entry;

    if (serial_log_dirent_lba == 0u) {
        return;
    }
    sectors = serial_log_next_lba - serial_log_start_lba + 1u;
    if (sectors > serial_log_region_sectors) {
        sectors = serial_log_region_sectors;
    }
    bytes = sectors * SERIAL_LOG_SECTOR_BYTES;
    /* A failed write is retried on the next flush instead of waiting for the
     * next sector boundary: the failures arrive in bursts (the firmware losing
     * the USB controller), and a size the text already earned should still be
     * published when an attempt works again. */
    if (bytes == serial_log_dirent_bytes && !serial_log_dirent_failed) {
        return;
    }
    entry = serial_log_dirent + serial_log_dirent_offset;
    serial_log_put32(entry + SERIAL_LOG_DIRENT_SIZE_OFFSET, bytes);
    memcpy_local((void *)(uintptr_t)rm_serial_log_buf, serial_log_dirent,
                 SERIAL_LOG_SECTOR_BYTES);
    if (serial_log_bios_call(serial_log_dirent_lba, 1u, true)) {
        serial_log_dirent_bytes = bytes;
        serial_log_dirent_failed = false;
    } else {
        serial_log_dirent_failed = true;
    }
}

/*
 * One attempt at putting the sector being filled on the medium, with the disk
 * reset a real firmware often needs first.  `lba` is explicit because the
 * in-flight-sector rewrite in serial_log_flush_sector and the per-frame
 * refresh in serial_log_frame both write the same sector without moving the
 * slot forward.
 *
 * The retry is the part that makes this survive real hardware: a BIOS that
 * has just had its drive touched by another driver (or that answered its own
 * boot a second ago) fails exactly one write and then works, and the classic
 * remedy - AH=00h, then the same call again - turns that into a stored line
 * instead of a three-strike stop.
 */
static bool serial_log_write_sector_at(uint32_t lba) {
    bool written;

    for (uint32_t i = serial_log_fill; i < SERIAL_LOG_SECTOR_BYTES; ++i) {
        serial_log_sector[i] = 0u;
    }
    serial_log_busy = true;
    memcpy_local((void *)(uintptr_t)rm_serial_log_buf, serial_log_sector,
                 SERIAL_LOG_SECTOR_BYTES);
    written = serial_log_bios_call(lba, 1u, true);
    serial_log_busy = false;
    if (written) {
        serial_log_last_error = 0u;
        return true;
    }
    serial_log_last_error = (serial_log_last_ah & 0xFFu) != 0u
                                ? (serial_log_last_ah & 0xFFu)
                                : 0x100u;
    if (serial_log_disk_reset()) {
        /* The sector bytes were not touched by the failed call, so the same
         * buffer goes straight back into the bridge. */
        serial_log_busy = true;
        memcpy_local((void *)(uintptr_t)rm_serial_log_buf, serial_log_sector,
                     SERIAL_LOG_SECTOR_BYTES);
        written = serial_log_bios_call(lba, 1u, true);
        serial_log_busy = false;
        if (written) {
            serial_log_last_error = 0u;
            return true;
        }
        serial_log_last_error = (serial_log_last_ah & 0xFFu) != 0u
                                    ? (serial_log_last_ah & 0xFFu)
                                    : 0x100u;
    }
    return false;
}

/*
 * Rewrite the sector being filled.  The characters after the write position
 * are still zero, so the medium always holds the text up to the last line;
 * the LBA only moves when the sector has actually filled, which keeps the
 * stored text contiguous.  Called both when a line ends and, from the frame
 * loop, when nothing has ended a line for a few frames - the second caller is
 * what keeps the file live instead of one line behind the screen.
 */
static bool serial_log_flush_sector(void) {
    if (serial_log_state != SERIAL_LOG_ACTIVE) {
        return false;
    }
    if (serial_log_next_lba >= serial_log_end_lba) {
        /* Even the extended slot is far more boot trace than this kernel has
         * ever printed; a full slot means something is looping.  Stop. */
        serial_log_state = SERIAL_LOG_OFF;
        serial_log_header_state = (uint32_t)SERIAL_LOG_STATE_FULL;
        /* Last chance to publish the size the text earned, in case the most
         * recent directory write was part of a failure burst. */
        serial_log_dirent_update();
        serial_log_header_update();
        serial_trace("WARNING", "serial.log: slot full, capture stopped");
        return false;
    }
    if (!serial_log_write_sector_at(serial_log_next_lba)) {
        /* A BIOS disk call can fail once and work again: the kernel's own USB
         * driver takes the controller the firmware was using for the boot
         * stick, and the firmware's next writes can land mid-reset.  Always
         * a few failures, never forever - on a firmware whose USB path is
         * gone for good each failed call can burn a timeout, and the boot
         * must not stall on the log.  The failure count, the firmware status
         * and the boot stage are all in the header by the time this gives
         * up, so the medium itself says where and why. */
        serial_log_write_failures++;
        serial_log_header_dirty = true;
        if (serial_log_write_failures >= (uint32_t)SERIAL_LOG_MAX_FAILURES) {
            serial_log_state = SERIAL_LOG_OFF;
            serial_log_header_state = (uint32_t)SERIAL_LOG_STATE_WRITEFAIL;
            /* The text that did land keeps its own sector, so publish the size
             * it earned before the capture goes quiet. */
            serial_log_dirent_update();
            serial_log_header_update();
            serial_trace("WARNING", "serial.log: sector write failed, capture stopped");
        } else {
            serial_log_header_update();
        }
        return false;
    }
    serial_log_write_failures = 0u;
    serial_log_stored_bytes = (serial_log_next_lba - serial_log_start_lba) *
                              SERIAL_LOG_SECTOR_BYTES + serial_log_fill;
    serial_log_dirent_update();
    if (serial_log_header_dirty) {
        serial_log_header_update();
    }
    return true;
}

/*
 * Per-frame housekeeping, called from the kernel's main loop right after the
 * frame is presented: with the screen just changed, the medium should not be
 * a whole line behind it.  The write is rate limited (SERIAL_LOG_PARTIAL_TICKS)
 * and skipped entirely when nothing new has been appended since the last one,
 * so an idle desktop writes nothing and a busy one still writes at a bounded
 * rate instead of once per frame.
 *
 * This is also what keeps the diagnostic header fresh once per second, so a
 * medium pulled from a machine that never got a line ending still carries a
 * current byte count and boot stage.
 */
static void serial_log_frame(void) {
    if (serial_log_state != SERIAL_LOG_ACTIVE) {
        return;
    }
    if (serial_log_fill != 0u && serial_log_fill != serial_log_partial_fill &&
        timer_ticks - serial_log_partial_tick >= (uint32_t)SERIAL_LOG_PARTIAL_TICKS) {
        serial_log_partial_tick = timer_ticks;
        serial_log_partial_fill = serial_log_fill;
        (void)serial_log_flush_sector();
        return;
    }
    if (serial_log_header_dirty ||
        timer_ticks - serial_log_header_tick >= (uint32_t)SERIAL_LOG_HEADER_TICKS) {
        serial_log_header_update();
    }
}

/*
 * Force everything that is still only in RAM onto the medium.  Called where
 * the kernel knows it is about to stop making progress - the shutdown path,
 * just before the machine is asked to power off - so a clean exit loses
 * nothing, and the header on the medium carries the final state instead of
 * whatever it held up to a second earlier.
 */
static void serial_log_sync(void) {
    if (serial_log_state != SERIAL_LOG_ACTIVE) {
        return;
    }
    serial_log_partial_fill = serial_log_fill; /* clear the "no new bytes" gate */
    (void)serial_log_flush_sector();
    serial_log_header_dirty = true;
    serial_log_header_update();
}

#else

/* Release builds keep the call sites but have nothing to do. */
static void serial_log_char(char ch) {
    (void)ch;
}

static void serial_log_init(const MultibootInfo *mbi) {
    (void)mbi;
}

static void serial_log_stage(uint32_t step) {
    (void)step;
}

static void serial_log_frame(void) {
}

static void serial_log_sync(void) {
}

#endif

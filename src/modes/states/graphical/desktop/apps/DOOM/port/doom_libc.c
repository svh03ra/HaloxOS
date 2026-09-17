// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: doom_libc.c, DOOM engine port: libc shim + WAD disk reader.
//
// This includes the DOOM 1997 source code, written by id Software Inc. Please check the license in the current folder.
// This repository is licensed under the GNU General Public License.
//
// The DOOM engine source expects a POSIX libc (stdio/stdlib/string) and
// POSIX file I/O. HaloxOS is freestanding, so this file provides the
// subset the engine actually uses, plus a WAD reader that streams the
// IWAD from raw disk sectors via the kernel ATA driver (there is no
// filesystem on the boot media the kernel can read yet).

/* WAD layout on the boot disk (build stamps it, see Makefile):
 * The DOOM1.WAD lives past the end of the FAT partition, as a raw
 * contiguous blob with a small info header:
 *   DOOM_WAD_INFO_LBA: 16-byte info block
 *     u32 magic 'DWAD' | u32 total_bytes | u32 start_sector | u32 zero
 *   DOOM_WAD_DATA_LBA: the WAD bytes, contiguous to the end.
 * When the block is absent (WAD not shipped for legal reasons) the DOOM
 * app shows the "DOOM1.WAD is missing" error window instead. */

#include "doom_os.h"

#ifndef DOOM_LIBC_INCLUDED
#define DOOM_LIBC_INCLUDED

/* ================= libc subset ================= */

int doom_strcmp(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && a[i] == b[i]) {
        ++i;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

int doom_strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        }
        if (a[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

int doom_strcasecmp(const char *a, const char *b) {
    for (size_t i = 0; ; ++i) {
        int ca = doom_toupper((int)(unsigned char)a[i]);
        int cb = doom_toupper((int)(unsigned char)b[i]);
        if (ca != cb) {
            return ca - cb;
        }
        if (ca == 0) {
            return 0;
        }
    }
}

char *doom_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++) != '\0') {
    }
    return dest;
}

char *doom_strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    for (; i < n; ++i) {
        dest[i] = '\0';
    }
    return dest;
}

char *doom_strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) {
        ++d;
    }
    while ((*d++ = *src++) != '\0') {
    }
    return dest;
}

size_t doom_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

void *doom_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void *doom_memset(void *dest, int value, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < n; ++i) {
        d[i] = (uint8_t)value;
    }
    return dest;
}

int doom_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}

int doom_toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    return c;
}

int doom_atoi(const char *s) {
    int value = 0;
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        ++s;
    } else if (*s == '+') {
        ++s;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        ++s;
    }
    return neg ? -value : value;
}

/* ---- sprintf: %s %d %i %c %x %X %u, with optional width (0N / N)
 * and precision (.N) as used by the engine (%03d lump names, %.3d). */
int doom_vsprintf(char *dest, const char *fmt, va_list ap) {
    char *out = dest;

    while (*fmt) {
        if (*fmt != '%') {
            *out++ = *fmt++;
            continue;
        }
        ++fmt;
        /* flags */
        int zero_pad = 0;
        int left_align = 0;
        for (;; ++fmt) {
            if (*fmt == '0' && fmt[-1] == '%') { zero_pad = 1; continue; }
            if (*fmt == '-') { left_align = 1; continue; }
            break;
        }
        /* width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            ++fmt;
        }
        /* precision: used as zero-padded minimum digit count */
        int precision = -1;
        if (*fmt == '.') {
            ++fmt;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt - '0');
                ++fmt;
            }
        }
        char spec = *fmt++;
        char numbuf[16];
        int min_digits = precision >= 0 ? precision : (zero_pad ? width : 0);

        switch (spec) {
            case '%':
                *out++ = '%';
                break;
            case 'c': {
                int ch = va_arg(ap, int);
                *out++ = (char)ch;
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                while (*s) {
                    *out++ = *s++;
                }
                break;
            }
            case 'd':
            case 'i': {
                int v = va_arg(ap, int);
                unsigned int u = (unsigned int)v;
                if (v < 0) {
                    *out++ = '-';
                    u = (unsigned int)(-v);
                }
                int pos = 0;
                do {
                    numbuf[pos++] = (char)('0' + u % 10u);
                    u /= 10u;
                } while (u && pos < 12);
                while (pos < min_digits && pos < 12) {
                    numbuf[pos++] = '0';
                }
                while (pos < width && pos < 12 && !zero_pad && !left_align) {
                    numbuf[pos++] = ' ';
                }
                while (pos) {
                    *out++ = numbuf[--pos];
                }
                break;
            }
            case 'x':
            case 'X': {
                unsigned int u = va_arg(ap, unsigned int);
                int pos = 0;
                do {
                    int digit = (int)(u & 0xFu);
                    numbuf[pos++] = (char)(digit < 10 ? '0' + digit : ((spec == 'x') ? 'a' : 'A') + digit - 10);
                    u >>= 4;
                } while (u && pos < 8);
                while (pos < min_digits && pos < 8) {
                    numbuf[pos++] = '0';
                }
                while (pos) {
                    *out++ = numbuf[--pos];
                }
                break;
            }
            case 'u': {
                unsigned int u = va_arg(ap, unsigned int);
                int pos = 0;
                do {
                    numbuf[pos++] = (char)('0' + u % 10u);
                    u /= 10u;
                } while (u && pos < 12);
                while (pos) {
                    *out++ = numbuf[--pos];
                }
                break;
            }
            default:
                /* unknown spec: emit verbatim */
                *out++ = '%';
                *out++ = spec;
                break;
        }
    }
    *out = '\0';
    return (int)(out - dest);
}

int doom_sprintf(char *dest, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = doom_vsprintf(dest, fmt, ap);
    va_end(ap);
    return n;
}

/* ================= malloc arena ================= */

/* Engine-side malloc users (d_main title/wadfile buffers, w_wad's
 * lumpinfo growth, i_net tables) are small and long-lived; a bump
 * arena with per-allocation size headers is plenty (the zone itself
 * is allocated separately at boot). 256 KB covers W_Init's growth
 * chain for any IWAD up to ~12000 lumps. */
#define DOOM_MALLOC_ARENA (256 * 1024)
static uint8_t doom_malloc_pool[DOOM_MALLOC_ARENA] __attribute__((aligned(16)));
static size_t doom_malloc_used = 0;

typedef struct {
    size_t size; /* payload bytes, 16-byte aligned */
} doom_alloc_hdr_t;

void *doom_malloc(size_t size) {
    if (size == 0) {
        size = 1;
    }
    size = (size + 15u) & ~(size_t)15u;
    size_t need = sizeof(doom_alloc_hdr_t) + size;
    if (doom_malloc_used + need > DOOM_MALLOC_ARENA) {
        /* engine treats this like a fatal error */
        extern void doom_fatal(const char *message);
        doom_fatal("doom_malloc: arena exhausted");
    }
    doom_alloc_hdr_t *hdr = (doom_alloc_hdr_t *)&doom_malloc_pool[doom_malloc_used];
    hdr->size = size;
    doom_malloc_used += need;
    return (void *)(hdr + 1);
}

void doom_free(void *ptr) {
    (void)ptr; /* bump arena: frees are no-ops (all uses are startup-only) */
}

/* Startup metadata has process lifetime in the original executable.  The
 * hosted port starts a fresh logical process for each reopen, so reclaim the
 * whole arena before that startup sequence instead of leaking it per run. */
void doom_malloc_reset(void) {
    doom_malloc_used = 0;
}

/* realloc: the engine grows lumpinfo/lumpcache arrays with it. Copy
 * the OLD payload size into a fresh bump allocation; old space leaks
 * inside the arena (the arrays only grow a handful of times during
 * W_Init). */
void *doom_realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return doom_malloc(size);
    }
    if (size == 0) {
        return NULL;
    }
    doom_alloc_hdr_t *hdr = (doom_alloc_hdr_t *)ptr - 1;
    size_t old_size = hdr->size;
    void *fresh = doom_malloc(size);
    doom_memcpy(fresh, ptr, old_size < size ? old_size : size);
    return fresh;
}

/* ================= WAD medium discovery + reader ================= */

#define DOOM_WAD_INFO_LBA 2048u           /* compact raw layout, before FAT */
#define DOOM_WAD_DATA_LBA 2052u           /* +4 sectors slack for the info */
#define DOOM_WAD_LEGACY_INFO_LBA 131200u  /* old layout: past 64M FAT */
#define DOOM_WAD_LEGACY_DATA_LBA 131204u

typedef struct {
    uint32_t magic;        /* 'DWAD' = 0x44415744 */
    uint32_t total_bytes;
    uint32_t start_sector; /* ATA: 512B LBA; ATAPI: 2048B CD sector */
    uint32_t reserved;
} DoomWadInfo;

typedef enum {
    DOOM_MEDIUM_NONE = 0,
    DOOM_MEDIUM_MODULE,  /* multiboot WAD module relocated to RAM by the loader */
    DOOM_MEDIUM_ATA,
    DOOM_MEDIUM_ATAPI
} DoomMediumKind;

typedef struct {
    DoomMediumKind kind;
    uint16_t io_base;
    uint8_t devsel;
    int pos;               /* index into doom_ata_positions */
    uint32_t start_sector;
    uint32_t total_bytes;
} DoomWadMedium;

static DoomWadMedium doom_wad_medium;
static bool doom_wad_present = false;

/* Cached first sectors: WAD header (12 bytes) + the whole lump directory
 * live there; DOOM1.WAD's directory is ~2200 lumps * 16 bytes = ~35 KB,
 * so up to 72 sectors of cache covers any IWAD up to 4600 lumps (ATA
 * path only: CD reads stream per request). */
#define DOOM_WAD_DIR_SECTORS 72
static uint8_t doom_wad_dir_cache[DOOM_WAD_DIR_SECTORS * 512];
static bool doom_wad_dir_cached = false;

/* Scratch sectors for unaligned WAD reads. */
static uint8_t doom_wad_sector[512];
static uint8_t doom_wad_cd_sector[2048];

/* Driver entry points (ata.c, defined earlier in this amalgamation TU).
 * Per-device variants let the WAD live on ANY channel/drive: 86Box and
 * real hardware attach disks and CD-ROMs to different positions, and
 * the old primary-master-only read missed them all. */
bool ata_pio_read_sector(uint32_t lba, uint8_t *buffer);
bool ata_pio_read_sector_dev(uint16_t io_base, uint8_t devsel_head, uint32_t lba, uint8_t *buffer);
bool ata_pio_read_sectors_dev(uint16_t io_base, uint8_t devsel_head, uint32_t lba,
                              uint8_t *buffer, uint8_t sectors);
bool ata_packet_capacity_dev(uint16_t io_base, uint8_t devsel_head, uint32_t *last_lba, uint32_t *block_bytes);
bool ata_packet_read_dev(uint16_t io_base, uint8_t devsel_head, uint32_t lba, uint16_t blocks, uint8_t *buffer);

typedef struct {
    uint16_t io_base;
    uint8_t devsel;
    const char *name;
} DoomAtaPos;

static const DoomAtaPos doom_ata_positions[4] = {
    { ATA_PRIMARY_IO,   ATA_DEV_MASTER, "primary master" },
    { ATA_PRIMARY_IO,   ATA_DEV_SLAVE,  "primary slave" },
    { ATA_SECONDARY_IO, ATA_DEV_MASTER, "secondary master" },
    { ATA_SECONDARY_IO, ATA_DEV_SLAVE,  "secondary slave" }
};

/* WAD shipped as a multiboot module: GRUB loaded /boot/doom.wadmodule
 * via BIOS so the game data travels in RAM on ANY boot media - USB/
 * Ventoy, CD, or disk. The loader relocates it above the kernel image
 * (0xB00000) before decompressing, then kernel_main records the module
 * range in kernel.c (init.c fills these; same translation unit). The
 * ATA/ATAPI probes below are only fallbacks for machines where the
 * module could not be relocated (under 14 MB RAM). */
extern uint32_t doom_wad_module_addr;
extern uint32_t doom_wad_module_bytes;

/* WAD embedded in the kernel image (doom_wad_data.c, generated by the
 * Makefile). Separate TU; the glue hook in doom_wad_data_hosted.c
 * (included just after this file) hands it over. */
const uint8_t *doom_wad_embed_get(uint32_t *size);
static const uint8_t *doom_wad_embed_ptr = NULL;
static uint32_t doom_wad_embed_size_cache = 0xFFFFFFFFu;

/* Internal hooks for the fd-style API in doom_wad_api.c. */
bool doom_wad_probe_internal(void);
int doom_wad_read_at(uint32_t offset, void *dest, int count);
int doom_wad_size_internal(void);
const char *doom_wad_medium_desc(void);

static bool doom_wad_probe(void) {
    if (doom_wad_present) {
        return true;
    }

    /* Kernel-embedded WAD first: always present on ANY boot media, no
     * bus I/O at all. The image is part of the kernel binary itself. */
    {
        uint32_t size = 0;
        const uint8_t *image = doom_wad_embed_get(&size);
        if (image != NULL && size >= 12u && size <= 0x2000000u) {
            doom_wad_medium.kind = DOOM_MEDIUM_MODULE;
            doom_wad_medium.io_base = 0;
            doom_wad_medium.devsel = 0;
            doom_wad_medium.pos = 0;
            doom_wad_medium.start_sector = 0;
            doom_wad_medium.total_bytes = size;
            doom_wad_embed_ptr = image;
            doom_wad_embed_size_cache = size;
            doom_wad_present = true;
            serial_trace("INFO", "DOOM WAD medium: kernel-embedded image");
            return true;
        }
        if (doom_wad_embed_size_cache == 0xFFFFFFFFu) {
            doom_wad_embed_size_cache = 0;
        }
    }

    /* RAM module next: the loader-relocated multiboot WAD (machines
     * with a safe RAM relocation slot). No bus I/O, works on every boot media.
     * Module layout: 'DWAD' + u32 wad bytes + 8 pad, then the raw WAD. */
    if (doom_wad_module_addr != 0 && doom_wad_module_bytes >= 20u &&
        doom_wad_module_bytes <= 0x2000000u + 16u) {
        const volatile uint8_t *m = (const volatile uint8_t *)(uintptr_t)doom_wad_module_addr;
        if (m[0] == 'D' && m[1] == 'W' && m[2] == 'A' && m[3] == 'D') {
            uint32_t total = (uint32_t)m[4] | ((uint32_t)m[5] << 8) |
                             ((uint32_t)m[6] << 16) | ((uint32_t)m[7] << 24);
            if (total != 0 && total <= 0x2000000u && total + 16u == doom_wad_module_bytes) {
                doom_wad_medium.kind = DOOM_MEDIUM_MODULE;
                doom_wad_medium.io_base = 0;
                doom_wad_medium.devsel = 0;
                doom_wad_medium.pos = 0;
                doom_wad_medium.start_sector = 0;
                doom_wad_medium.total_bytes = total;
                doom_wad_present = true;
                serial_trace("INFO", "DOOM WAD medium: multiboot module (RAM)");
                return true;
            }
            serial_trace("WARNING", "DOOM probe: WAD module magic ok but size mismatch - falling back");
        } else {
            serial_trace("WARNING", "DOOM probe: WAD module magic bad - falling back to media probe");
        }
    }

    /* Hard disks: the raw DWAD stamp at a fixed LBA on ANY channel or
     * drive position (the ATA driver only sends READ SECTORS to drives
     * it identified as disks, so a CD-only box never gets stuck). */
    /* Probe the current compact layout first, then accept the old raw-disk
     * layout so existing HaloxOS disk images remain bootable. The probe is
     * intentionally device-agnostic: the WAD disk may be primary slave
     * while an ISO/CD device occupies primary master. */
    static const uint32_t wad_stamp_lbas[][2] = {
        { DOOM_WAD_INFO_LBA,        DOOM_WAD_DATA_LBA },
        { DOOM_WAD_LEGACY_INFO_LBA, DOOM_WAD_LEGACY_DATA_LBA }
    };
    for (int i = 0; i < 4; ++i) {
        for (size_t layout = 0; layout < sizeof(wad_stamp_lbas) / sizeof(wad_stamp_lbas[0]); ++layout) {
            uint32_t info_lba = wad_stamp_lbas[layout][0];
            uint32_t data_lba = wad_stamp_lbas[layout][1];
            if (!ata_pio_read_sector_dev(doom_ata_positions[i].io_base,
                                         doom_ata_positions[i].devsel,
                                         info_lba, doom_wad_sector)) {
                continue;
            }
            DoomWadInfo *info = (DoomWadInfo *)(void *)doom_wad_sector;
            if (info->magic != 0x44415744u /* 'DWAD' little endian */ ||
                info->total_bytes == 0 || info->total_bytes > 0x2000000u /* 32 MB sanity */ ||
                info->start_sector != data_lba) {
                continue;
            }
            doom_wad_medium.kind = DOOM_MEDIUM_ATA;
            doom_wad_medium.io_base = doom_ata_positions[i].io_base;
            doom_wad_medium.devsel = doom_ata_positions[i].devsel;
            doom_wad_medium.pos = i;
            doom_wad_medium.start_sector = info->start_sector;
            doom_wad_medium.total_bytes = info->total_bytes;
            doom_wad_present = true;
            serial_trace_uint_value("INFO", "DOOM WAD ATA start LBA", info->start_sector);
            return true;
        }
    }

    /* CD-ROMs: the ISO ships the WAD blob as a raw 2048-byte-sector
     * region right after its last volume sector. */
    for (int i = 0; i < 4; ++i) {
        uint32_t last_lba = 0;
        uint32_t block_bytes = 0;
        bool cap_ok = ata_packet_capacity_dev(doom_ata_positions[i].io_base,
                                              doom_ata_positions[i].devsel,
                                              &last_lba, &block_bytes);
        {
            char buf[80];
            doom_sprintf(buf, "DOOM probe: ATAPI %s capacity=%s lba=%d blk=%d",
                         doom_ata_positions[i].name, cap_ok ? "ok" : "fail",
                         last_lba, block_bytes);
            serial_trace("INFO", buf);
        }
        if (!cap_ok) {
            continue;
        }
        if (block_bytes != 2048u) {
            continue;
        }
        if (!ata_packet_read_dev(doom_ata_positions[i].io_base,
                                 doom_ata_positions[i].devsel,
                                 last_lba, 1, doom_wad_cd_sector)) {
            continue;
        }
        DoomWadInfo *info = (DoomWadInfo *)(void *)doom_wad_cd_sector;
        uint32_t wadsec = (info->total_bytes + 2047u) / 2048u;
        /* Layout: [ISO ...][WAD (wadsec sectors)][info in the LAST
         * sector]. The info records the WAD's start CD sector, and the
         * WAD region must end right before the info sector. */
        if (info->magic != 0x44415744u ||
            info->total_bytes == 0 || info->total_bytes > 0x2000000u ||
            info->start_sector == 0 || info->start_sector + wadsec != last_lba) {
            continue;
        }
        doom_wad_medium.kind = DOOM_MEDIUM_ATAPI;
        doom_wad_medium.io_base = doom_ata_positions[i].io_base;
        doom_wad_medium.devsel = doom_ata_positions[i].devsel;
        doom_wad_medium.pos = i;
        doom_wad_medium.start_sector = info->start_sector;
        doom_wad_medium.total_bytes = info->total_bytes;
        doom_wad_present = true;
        return true;
    }

    return false;
}

/* Read `count` bytes at WAD file offset `offset` into dest.
 * Returns bytes read (short read at EOF). */
static int doom_wad_pread(uint32_t offset, void *dest, int count) {
    uint8_t *d = (uint8_t *)dest;
    int done = 0;

    if (!doom_wad_probe() || offset >= doom_wad_medium.total_bytes) {
        return 0;
    }
    if ((uint64_t)offset + (uint64_t)count > (uint64_t)doom_wad_medium.total_bytes) {
        count = (int)(doom_wad_medium.total_bytes - offset);
    }

    if (doom_wad_medium.kind == DOOM_MEDIUM_MODULE) {
        if (doom_wad_embed_ptr != NULL &&
            doom_wad_embed_size_cache == doom_wad_medium.total_bytes) {
            /* Kernel-embedded image: straight copy. */
            doom_memcpy(dest, doom_wad_embed_ptr + offset, (size_t)count);
            return count;
        }
        /* Straight RAM copy from the relocated module: the module holds
         * the raw WAD at offset 16. */
        doom_memcpy(dest, (const void *)(uintptr_t)(doom_wad_module_addr + 16u + offset),
                    (size_t)count);
        return count;
    }

    if (doom_wad_medium.kind == DOOM_MEDIUM_ATA) {
        while (done < count) {
            uint32_t sec = offset / 512u;
            uint32_t in_sec = offset % 512u;

            /* Most WAD lumps arrive as contiguous full sectors.  Read them
             * in one PIO command (up to the ATA LBA28 limit of 128 here)
             * instead of selecting and polling the IDE device once per
             * sector.  This is especially important on slow 86Box PATA. */
            if (sec >= DOOM_WAD_DIR_SECTORS && in_sec == 0 &&
                count - done >= 512) {
                uint32_t full_sectors = (uint32_t)(count - done) / 512u;
                uint8_t sectors = full_sectors > 128u ? 128u : (uint8_t)full_sectors;
                if (!ata_pio_read_sectors_dev(doom_wad_medium.io_base,
                                              doom_wad_medium.devsel,
                                              doom_wad_medium.start_sector + sec,
                                              d + done, sectors)) {
                    return done;
                }
                {
                    uint32_t bytes = (uint32_t)sectors * 512u;
                    done += (int)bytes;
                    offset += bytes;
                }
                continue;
            }
            int chunk = 512 - (int)in_sec;
            if (chunk > count - done) {
                chunk = count - done;
            }

            /* Directory window? serve from cache (the W_Init path hammers
             * the header+directory; caching keeps it off the ATA bus). */
            if (sec < DOOM_WAD_DIR_SECTORS) {
                if (!doom_wad_dir_cached) {
                    if (!ata_pio_read_sectors_dev(doom_wad_medium.io_base,
                                                  doom_wad_medium.devsel,
                                                  doom_wad_medium.start_sector,
                                                  doom_wad_dir_cache,
                                                  DOOM_WAD_DIR_SECTORS)) {
                        return done;
                    }
                    doom_wad_dir_cached = true;
                }
                doom_memcpy(d + done, &doom_wad_dir_cache[sec * 512 + in_sec], (size_t)chunk);
            } else {
                if (!ata_pio_read_sector_dev(doom_wad_medium.io_base,
                                             doom_wad_medium.devsel,
                                             doom_wad_medium.start_sector + sec,
                                             doom_wad_sector)) {
                    return done;
                }
                doom_memcpy(d + done, &doom_wad_sector[in_sec], (size_t)chunk);
            }
            done += chunk;
            offset += (uint32_t)chunk;
        }
    } else {
        /* ATAPI: 2048-byte CD sectors via READ(12) packets. */
        while (done < count) {
            uint32_t sec = offset / 2048u;
            uint32_t in_sec = offset % 2048u;
            int chunk = 2048 - (int)in_sec;
            if (chunk > count - done) {
                chunk = count - done;
            }
            if (!ata_packet_read_dev(doom_wad_medium.io_base,
                                     doom_wad_medium.devsel,
                                     doom_wad_medium.start_sector + sec, 1,
                                     doom_wad_cd_sector)) {
                return done;
            }
            doom_memcpy(d + done, &doom_wad_cd_sector[in_sec], (size_t)chunk);
            done += chunk;
            offset += (uint32_t)chunk;
        }
    }
    return done;
}

static int doom_wad_size(void) {
    return doom_wad_probe() ? (int)doom_wad_medium.total_bytes : 0;
}

bool doom_wad_probe_internal(void) {
    return doom_wad_probe();
}

int doom_wad_read_at(uint32_t offset, void *dest, int count) {
    return doom_wad_pread(offset, dest, count);
}

int doom_wad_size_internal(void) {
    return doom_wad_size();
}

/* Where the WAD was found, for the COM1 self-test / debug trace. */
const char *doom_wad_medium_desc(void) {
    if (!doom_wad_present) {
        return "none found";
    }
    static char buffer[64];
    static const char *const kinds[] = { "none", "RAM image", "ATA disk", "ATAPI CD-ROM" };
    if (doom_wad_medium.kind == DOOM_MEDIUM_MODULE) {
        doom_sprintf(buffer, "%s (RAM)", kinds[doom_wad_medium.kind]);
    } else {
        doom_sprintf(buffer, "%s (%s)", kinds[doom_wad_medium.kind],
                     doom_ata_positions[doom_wad_medium.pos].name);
    }
    return buffer;
}

/* ================= printf -> COM1 serial trace ================= */

int doom_printf(const char *fmt, ...) {
    char buffer[160];
    va_list ap;

    va_start(ap, fmt);
    doom_vsprintf(buffer, fmt, ap);
    va_end(ap);

    /* Mirror the engine's console chatter to the COM1 debug trace so
     * 'make serial' shows the full DOOM startup banner, WAD lump
     * counts and game details, exactly like the DOS/Linux versions. */
    serial_trace_concat("DOOM", " ", buffer);
    return (int)doom_strlen(buffer);
}

#endif /* DOOM_LIBC_INCLUDED */

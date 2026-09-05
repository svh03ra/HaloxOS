// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: main.c, zstd boot loader main entry.
//
// This repository is licensed under the GNU General Public License.

/*
 * HaloxOS zstd boot loader.
 *
 * GRUB cannot decompress zstd, so this small multiboot kernel is what GRUB
 * actually boots. It receives the zstd-compressed HaloxOS kernel as a
 * multiboot module, decompresses it into place with the vendored zstd
 * 1.5.7 decompressor, and jumps to the kernel's real entry point with the
 * original multiboot registers restored.
 *
 * Memory layout (must stay under 8 MB):
 *   0x100000  loader itself (ELF loaded by GRUB)
 *   ~0x120000 kernel.bin.zst module (placed by GRUB, page aligned)
 *   0x200000  kernel decompression target (kernel links at 2M)
 *   0x700000  loader info struct handed to the kernel (debug trace data)
 *   0x710000  zstd DCtx scratch (256K, above kernel bss end)
 */

#define LOADER_DEBUG 1

/* Test build hook (make noram): pretend the machine has too little RAM so
 * the BOOT ERROR screen is forced at boot, even on real hardware with
 * plenty of memory. Lets the error screen be verified on any PC. */
#ifndef HALOXOS_FORCE_RAM_ERROR
#define HALOXOS_FORCE_RAM_ERROR 0
#endif

#define SERIAL_PORT 0x3F8

#define KERNEL_LOAD_ADDR   0x200000u
#define KERNEL_SCRATCH     0x710000u
#define KERNEL_SCRATCH_SIZE 0x40000u
/* Above the kernel bss end (0x70A2C4) and the zstd scratch: the kernel's
 * boot code zeroes its whole .bss, so this block must live outside it. */
#define LOADER_INFO_ADDR   0x750000u

#define MB_MAGIC_EXPECTED 0x2BADB002u
#define MB_FLAG_MODS       (1u << 3)
#define MB_FLAG_MMAP       (1u << 6)
#define MB_FLAG_MEM        (1u << 0)
#define MB_FLAG_FB         (1u << 12)

#define HALOXOS_MODULE_MAGIC 0x484C585Au

/* HaloxOS policy floor: require a full 8 MiB of installed/advertised RAM.
 * The loader's internal addresses fit below this, but the OS intentionally
 * refuses smaller configurations. */
#define MIN_RAM_BYTES 0x800000u

/* VGA text mode screen geometry. */
#define VGA_TEXT_BUFFER 0xB8000u
#define VGA_TEXT_COLS   80
#define VGA_TEXT_ROWS   25

typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef unsigned long size_t_;
typedef unsigned long long uint64_t;

typedef struct {
    uint32_t flags;               /* 0 */
    uint32_t mem_lower;           /* 4 */
    uint32_t mem_upper;           /* 8 */
    uint32_t boot_device;         /* 12 */
    uint32_t cmdline;             /* 16 */
    uint32_t mods_count;          /* 20 */
    uint32_t mods_addr;           /* 24 */
    uint32_t syms[4];             /* 28 */
    uint32_t mmap_length;         /* 44 */
    uint32_t mmap_addr;           /* 48 */
    uint32_t drives_length;       /* 52 */
    uint32_t drives_addr;         /* 56 */
    uint32_t config_table;        /* 60 */
    uint32_t boot_loader_name;    /* 64 */
    uint32_t apm_table;           /* 68 */
    uint32_t vbe_control_info;    /* 72 */
    uint32_t vbe_mode_info;       /* 76 */
    uint16_t vbe_mode;            /* 80 */
    uint16_t vbe_interface_seg;   /* 82 */
    uint16_t vbe_interface_off;   /* 84 */
    uint16_t vbe_interface_len;   /* 86 */
    uint64_t framebuffer_addr;    /* 88 */
    uint32_t framebuffer_pitch;   /* 96 */
    uint32_t framebuffer_width;   /* 100 */
    uint32_t framebuffer_height; /* 104 */
    uint8_t framebuffer_bpp;      /* 108 */
    uint8_t framebuffer_type;     /* 109 */
    uint8_t framebuffer_red_position;   /* 110 (RGB type only) */
    uint8_t framebuffer_red_mask_size;  /* 111 */
    uint8_t framebuffer_green_position; /* 112 */
    uint8_t framebuffer_green_mask_size;/* 113 */
    uint8_t framebuffer_blue_position;  /* 114 */
    uint8_t framebuffer_blue_mask_size; /* 115 */
} __attribute__((packed)) MbInfo;

__attribute__((unused)) static uint64_t mbi_fb_addr(const MbInfo *mbi) {
    return mbi->framebuffer_addr;
}

__attribute__((unused)) static uint32_t mbi_fb_width(const MbInfo *mbi) {
    return mbi->framebuffer_width;
}

__attribute__((unused)) static uint32_t mbi_fb_height(const MbInfo *mbi) {
    return mbi->framebuffer_height;
}

__attribute__((unused)) static uint32_t mbi_fb_bpp(const MbInfo *mbi) {
    return mbi->framebuffer_bpp;
}

__attribute__((unused)) static uint32_t mbi_fb_pitch(const MbInfo *mbi) {
    return mbi->framebuffer_pitch;
}

typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t mod_string;
    uint32_t reserved;
} MbModule;

/*
 * Compressed module header, emitted by the Makefile right before the zstd
 * frame so the loader can validate the blob and know the exact original
 * kernel byte size before decompressing.
 */
typedef struct {
    uint32_t magic;            /* HALOXOS_MODULE_MAGIC */
    uint32_t kernel_entry;      /* entry point inside the decompressed image */
    uint32_t image_size;        /* total decompressed byte count */
    uint32_t frame_size;        /* zstd frame bytes that follow this header */
    uint32_t crc;               /* simple checksum over the header (reserved) */
} ModuleHeader;

/* Fixed handoff block the kernel reads for its loader trace lines. */
typedef struct {
    uint32_t magic;             /* HALOXOS_MODULE_MAGIC when valid */
    uint32_t module_start;
    uint32_t module_end;
    uint32_t compressed_size;   /* frame_size, header not counted */
    uint32_t original_size;     /* image_size */
    uint32_t ratio_percent_x10; /* (compressed * 1000) / original */
    uint32_t decompress_ticks;  /* PIT-tick style count: incremented per loop */
    uint32_t dctx_size;         /* ZSTD_estimateDCtxSize() value */
    uint32_t kernel_entry;
    uint32_t loader_flags;
} LoaderInfo;

extern uint32_t saved_magic;
extern uint32_t saved_mbi;
extern void jump_to_kernel(uint32_t entry, uint32_t magic, uint32_t mbi);

static void outb(uint16_t port, uint8_t value);
static uint8_t inb(uint16_t port);
static void serial_init(void);
static void serial_tx(char ch);
static void serial_puts(const char *text);
static void serial_put_hex32(uint32_t value);
static void serial_put_uint(uint32_t value);
static void serial_line(const char *text);
static void serial_line_hex(const char *label, uint32_t value);
static void serial_line_uint(const char *label, uint32_t value);
static void loader_memset(void *dest, uint8_t value, size_t_ n);
static void loader_hang(const char *reason, const MbInfo *mbi);

/* ===== VGA text mode error screen (red background, white text) ===== */

static void text_screen_write(int row, int col, const char *text, uint8_t attr);
static void text_screen_fill(uint8_t attr);
static void text_force_mode3(void);
static void text_load_font(void);
static void err_begin(const MbInfo *mbi);
static void err_write(int row, int col, const char *text);

static void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void loader_hang(const char *reason, const MbInfo *mbi) {
    serial_puts("[LDR]: FATAL ");
    serial_puts(reason);
    serial_puts("\n");

    /* Never die invisibly: show the reason on a red screen too, for
     * machines where no serial port is connected. Uses the linear
     * framebuffer when GRUB provided one (modern GPUs), else classic
     * VGA text mode. */
    err_begin(mbi);
    err_write(1, 8, "***** BOOT ERROR!!! *****");
    err_write(3, 4, "The HaloxOS boot loader could not continue:");
    err_write(5, 8, reason);
    err_write(8, 4, "Check the serial port (COM1) trace for full details.");
    err_write(10, 4, "Power off, fix the issue above, and try again.");

    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ===== VGA text mode error screen (red background, white text) ===== */

static void text_screen_write(int row, int col, const char *text, uint8_t attr) {
    volatile uint16_t *screen = (volatile uint16_t *)VGA_TEXT_BUFFER;

    if (row < 0 || row >= VGA_TEXT_ROWS) {
        return;
    }
    for (int i = 0; text[i] != '\0'; ++i) {
        if (col < 0 || col >= VGA_TEXT_COLS) {
            break;
        }
        screen[row * VGA_TEXT_COLS + col] = (uint16_t)(((uint16_t)attr << 8) | (uint8_t)text[i]);
        ++col;
    }
}

static void text_screen_fill(uint8_t attr) {
    volatile uint16_t *screen = (volatile uint16_t *)VGA_TEXT_BUFFER;

    for (int i = 0; i < VGA_TEXT_COLS * VGA_TEXT_ROWS; ++i) {
        screen[i] = (uint16_t)(((uint16_t)attr << 8) | ' ');
    }
}

/* Write a string centered within the classic 80-column DOS text screen.
 * Unlike fixed columns, this keeps the warning/header visually centered even
 * when the string length changes. Strings wider than the screen are clipped
 * safely by text_screen_write(). */
static void text_screen_write_center(int row, const char *text, uint8_t attr) {
    int len = 0;
    while (text[len] != '\0') {
        ++len;
    }

    int col = (VGA_TEXT_COLS - len) / 2;
    if (col < 0) {
        col = 0;
    }
    text_screen_write(row, col, text, attr);
}

/* ===== Linear framebuffer error screen (modern real hardware) =====
 *
 * When GRUB hands over a VBE linear framebuffer, the GPU is in a mode no
 * direct VGA register programming can reliably undo on modern hardware
 * (NVIDIA/AMD/Intel dismiss legacy mode switches, producing garbage or
 * half-screens). Instead of touching any VGA register, draw the error
 * text directly into the existing framebuffer with the 8x8 font. This is
 * fully GPU-agnostic: any LFB pitch/bpp/masking GRUB reports is honoured.
 */

/* Color-info fields live at mbi+110..115 as raw bytes (multiboot spec).
 * Read them through a byte pointer so struct layout can never shift them:
 * a 2-byte shift turns red into green and the screen renders wrong. */
static void fb_read_color_info(const MbInfo *mbi,
                               uint8_t *red_pos, uint8_t *red_size,
                               uint8_t *green_pos, uint8_t *green_size,
                               uint8_t *blue_pos, uint8_t *blue_size) {
    const uint8_t *raw = (const uint8_t *)mbi;

    *red_pos   = raw[110];
    *red_size  = raw[111];
    *green_pos = raw[112];
    *green_size= raw[113];
    *blue_pos  = raw[114];
    *blue_size = raw[115];
}

static uint32_t fb_make_color(uint8_t r, uint8_t g, uint8_t b, const MbInfo *mbi) {
    uint32_t color = 0;
    uint8_t rp, rs, gp, gs, bp, bs;

    fb_read_color_info(mbi, &rp, &rs, &gp, &gs, &bp, &bs);
    if (mbi->framebuffer_type == 1 && rs != 0 && gs != 0 && bs != 0 &&
        rs <= 8 && gs <= 8 && bs <= 8 &&
        rp < 32 && gp < 32 && bp < 32) {
        /* Direct RGB with the exact Multiboot field positions/mask sizes. */
        color |= ((uint32_t)(r >> (8 - rs)) << rp);
        color |= ((uint32_t)(g >> (8 - gs)) << gp);
        color |= ((uint32_t)(b >> (8 - bs)) << bp);
        return color;
    }

    /* Graceful fallback for old firmware that reports a direct-RGB mode but
     * omits its mask fields.  These are the conventional VBE formats. */
    if (mbi->framebuffer_bpp == 15) {
        return (((uint32_t)r >> 3) << 10) |
               (((uint32_t)g >> 3) << 5) |
               ((uint32_t)b >> 3);
    }
    if (mbi->framebuffer_bpp == 16) {
        return (((uint32_t)r >> 3) << 11) |
               (((uint32_t)g >> 2) << 5) |
               ((uint32_t)b >> 3);
    }
    if (mbi->framebuffer_bpp == 24 || mbi->framebuffer_bpp == 32) {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    /* This function is never selected for indexed/legacy error screens, but
     * keep a deterministic fallback for defensive callers. */
    return ((uint32_t)(r & 0xE0u)) |
           (((uint32_t)g >> 3) & 0x1Cu) |
           (((uint32_t)b >> 6) & 0x03u);
}

static uint32_t fb_red(const MbInfo *mbi) {
    /* error screen background: dark red */
    if (mbi->framebuffer_type == 2) {
        return 4;               /* EGA index for red */
    }
    return fb_make_color(168, 0, 0, mbi);
}

static uint32_t fb_white(const MbInfo *mbi) {
    if (mbi->framebuffer_type == 2) {
        return 15;              /* EGA index for white */
    }
    return fb_make_color(255, 255, 255, mbi);
}

/* Put one pixel at (x,y). Handles 8/15/16/24/32 bpp RGB and indexed. */
static void fb_put_pixel(const MbInfo *mbi, uint32_t x, uint32_t y, uint32_t color) {
    volatile uint8_t *fb = (volatile uint8_t *)(size_t_)(uint32_t)mbi->framebuffer_addr;
    uint32_t pitch = mbi->framebuffer_pitch;
    uint32_t bpp = mbi->framebuffer_bpp;
    size_t_ offset;

    if (x >= mbi->framebuffer_width || y >= mbi->framebuffer_height) {
        return;
    }
    if (mbi->framebuffer_addr > 0xFFFFFFFFull) {
        return;                 /* beyond 32-bit reach: cannot draw */
    }

    switch ((bpp + 7) / 8) {
    case 1:
        offset = (size_t_)y * pitch + x;
        fb[offset] = (uint8_t)color;
        break;
    case 2:
        offset = (size_t_)y * pitch + x * 2u;
        *(volatile uint16_t *)(fb + offset) = (uint16_t)color;
        break;
    case 3:
        offset = (size_t_)y * pitch + x * 3u;
        fb[offset] = (uint8_t)(color & 0xFFu);
        fb[offset + 1] = (uint8_t)((color >> 8) & 0xFFu);
        fb[offset + 2] = (uint8_t)((color >> 16) & 0xFFu);
        break;
    case 4:
        offset = (size_t_)y * pitch + x * 4u;
        *(volatile uint32_t *)(fb + offset) = color;
        break;
    default:
        break;
    }
}

/* Fill the whole screen with one color. */
static void fb_fill(const MbInfo *mbi, uint32_t color) {
    for (uint32_t y = 0; y < mbi->framebuffer_height; ++y) {
        for (uint32_t x = 0; x < mbi->framebuffer_width; ++x) {
            fb_put_pixel(mbi, x, y, color);
        }
    }
}

/* 8x8 ASCII 32..127 font (table defined below, shared by both backends) */
static const uint8_t text_font8x8[96][8];

/* Draw one 8x8 glyph at pixel (px,py); bits follow the text_font8x8 table. */
static void fb_glyph(const MbInfo *mbi, uint32_t px, uint32_t py,
                     const uint8_t *rows, uint32_t fg, uint32_t bg) {
    for (int r = 0; r < 8; ++r) {
        uint8_t bits = rows[r];

        for (int c = 0; c < 8; ++c) {
            /* font table is LSB-first (leftmost pixel in bit 0), the same
             * convention text_load_font bit-reverses for VGA plane fonts */
            fb_put_pixel(mbi, px + (uint32_t)c, py + (uint32_t)r,
                         (bits & 0x01u) ? fg : bg);
            bits >>= 1;
        }
    }
}

/*
 * Write a string into the framebuffer at character cell (row, col) of an
 * 80x25-equivalent grid scaled to the screen, same layout as the VGA text
 * screen so the two error-screen paths stay visually identical.
 */
static void fb_text(const MbInfo *mbi, int row, int col, const char *text,
                    uint32_t fg, uint32_t bg) {
    if (mbi->framebuffer_width < 80 || mbi->framebuffer_height < 25) {
        return;
    }

    uint32_t cw = mbi->framebuffer_width / 80u;
    uint32_t ch = mbi->framebuffer_height / 25u;
    if (cw < 8u) cw = 8u;
    if (ch < 8u) ch = 8u;

    for (int i = 0; text[i] != '\0'; ++i) {
        int current_col = col + i;
        if (current_col < 0 || current_col >= 80 || row < 0 || row >= 25) {
            continue;
        }

        uint32_t px = (uint32_t)current_col * cw;
        uint32_t py = (uint32_t)row * ch;
        int glyph = (text[i] >= 32 && text[i] < 127) ? text[i] - 32 : 0;

        /* Clear the complete cell before drawing the glyph. */
        for (uint32_t r = 0; r < ch; ++r) {
            for (uint32_t c = 0; c < cw; ++c) {
                fb_put_pixel(mbi, px + c, py + r, bg);
            }
        }

        fb_glyph(mbi,
                 px + (cw - 8u) / 2u,
                 py + (ch - 8u) / 2u,
                 text_font8x8[glyph], fg, bg);
    }
}

/*
 * Pixel-positioned text helpers for the graphical emergency screen.
 *
 * The VGA fallback stays a genuine 80x25 DOS-style screen.  A linear
 * framebuffer is instead laid out in pixels so long sentences can be truly
 * centered and diagnostic sections can have deliberate vertical spacing.
 */
static void fb_text_pixels(const MbInfo *mbi, uint32_t x, uint32_t y,
                           const char *text, uint32_t fg, uint32_t bg) {
    uint32_t width = mbi->framebuffer_width;
    uint32_t height = mbi->framebuffer_height;

    for (int i = 0; text[i] != '\0'; ++i) {
        uint32_t px = x + (uint32_t)i * 8u;
        if (px + 8u > width || y + 8u > height) {
            break;
        }

        int glyph = (text[i] >= 32 && text[i] < 127) ? text[i] - 32 : 0;
        fb_glyph(mbi, px, y, text_font8x8[glyph], fg, bg);
    }
}

static void fb_text_center(const MbInfo *mbi, uint32_t y, const char *text,
                           uint32_t fg, uint32_t bg) {
    uint32_t len = 0;
    while (text[len] != '\0') ++len;

    uint32_t text_width = len * 8u;
    uint32_t x = (text_width >= mbi->framebuffer_width)
                   ? 0u
                   : (mbi->framebuffer_width - text_width) / 2u;
    fb_text_pixels(mbi, x, y, text, fg, bg);
}

static void fb_text_left(const MbInfo *mbi, uint32_t y, uint32_t margin,
                         const char *text, uint32_t fg, uint32_t bg) {
    if (margin >= mbi->framebuffer_width) margin = 0;
    fb_text_pixels(mbi, margin, y, text, fg, bg);
}

static void fb_text_center_block(const MbInfo *mbi, uint32_t y,
                                 const char *const *lines, uint32_t count,
                                 uint32_t line_step, uint32_t fg, uint32_t bg) {
    for (uint32_t i = 0; i < count; ++i) {
        fb_text_center(mbi, y + i * line_step, lines[i], fg, bg);
    }
}

/*
 * Backend-agnostic error-screen renderer. When GRUB handed over a usable
 * linear framebuffer, draw into it directly (works on every modern GPU);
 * otherwise fall back to the legacy VGA text-mode path. All callers use
 * err_write()/err_fill() so both backends show the identical 80x25 layout.
 */
static const MbInfo *err_mbi;

static int err_use_framebuffer(const MbInfo *mbi) {
    /*
     * Only use the existing bootloader framebuffer for a real direct-RGB
     * pixel mode.  Indexed/planar legacy modes are not linear RGB pixels;
     * treating a 1/4/8-bpp VGA surface as bytes-per-pixel produces exactly
     * the vertical "barcode" corruption seen on IBM VGA/PCBox.
     *
     * EGA text (type 2) is explicitly non-pixel data and must stay on the
     * classic DOS-compatible text path.  Unknown framebuffer types are also
     * rejected instead of being guessed at.
     */
    if (mbi == 0 || (mbi->flags & MB_FLAG_FB) == 0 ||
        mbi->framebuffer_addr == 0 || mbi->framebuffer_addr > 0xFFFFFFFFull ||
        mbi->framebuffer_pitch == 0 || mbi->framebuffer_width < 80 ||
        mbi->framebuffer_height < 25 || mbi->framebuffer_type != 1) {
        return 0;
    }

    if (mbi->framebuffer_bpp != 15 && mbi->framebuffer_bpp != 16 &&
        mbi->framebuffer_bpp != 24 && mbi->framebuffer_bpp != 32) {
        return 0;
    }

    {
        uint32_t bytes_per_pixel = ((uint32_t)mbi->framebuffer_bpp + 7u) / 8u;
        uint64_t min_pitch = (uint64_t)mbi->framebuffer_width * bytes_per_pixel;
        if (min_pitch == 0 || min_pitch > 0xFFFFFFFFull ||
            (uint64_t)mbi->framebuffer_pitch < min_pitch) {
            return 0;
        }
    }

    return 1;
}

static void err_dump_fb(const MbInfo *mbi) {
    uint8_t rp, rs, gp, gs, bp, bs;

    if (mbi == 0) {
        serial_line("fb info: unavailable (no Multiboot info)");
        return;
    }

    fb_read_color_info(mbi, &rp, &rs, &gp, &gs, &bp, &bs);
    serial_line_hex("fb addr", (uint32_t)(size_t_)mbi->framebuffer_addr);
    serial_line_uint("fb pitch", mbi->framebuffer_pitch);
    serial_line_uint("fb width", mbi->framebuffer_width);
    serial_line_uint("fb height", mbi->framebuffer_height);
    serial_line_uint("fb bpp", mbi->framebuffer_bpp);
    serial_line_uint("fb type", mbi->framebuffer_type);
    serial_line_uint("fb red pos", rp);
    serial_line_uint("fb red size", rs);
    serial_line_uint("fb green pos", gp);
    serial_line_uint("fb green size", gs);
    serial_line_uint("fb blue pos", bp);
    serial_line_uint("fb blue size", bs);
    serial_line_hex("fb color red", fb_red(mbi));
    serial_line_hex("fb color white", fb_white(mbi));
}

/* Prepare the error screen: fill background. Call once before err_write. */
static void err_begin(const MbInfo *mbi) {
    err_mbi = mbi;
    if (err_use_framebuffer(mbi)) {
        serial_line("error screen: drawing into the linear framebuffer");
        err_dump_fb(mbi);
        fb_fill(mbi, fb_red(mbi));
    } else {
        serial_line("error screen: unsupported/legacy framebuffer; using DOS text mode");
        err_dump_fb(mbi);
        text_force_mode3();
    }
}

/* Write text at 80x25 cell (row, col). */
static void err_write(int row, int col, const char *text) {
    if (err_mbi != 0 && err_use_framebuffer(err_mbi)) {
        fb_text(err_mbi, row, col, text, fb_white(err_mbi), fb_red(err_mbi));
    } else {
        text_screen_write(row, col, text, 0x4F);
    }
}

/*
 * 8x8 ASCII 32..127 font, identical to the kernel's font8x8_basic table
 * (src/kernel/system/kernel.c). A VBE mode set by GRUB clears video RAM,
 * which destroys the BIOS text font in plane 2, so the loader must
 * re-upload glyphs of its own or every character renders as a blank cell.
 */
static const uint8_t text_font8x8[96][8] = {
    {0,0,0,0,0,0,0,0},{24,60,60,24,24,0,24,0},{54,54,20,0,0,0,0,0},{54,54,127,54,127,54,54,0},
    {24,62,3,30,48,31,24,0},{0,99,102,12,24,51,99,0},{28,54,28,59,102,102,59,0},{6,6,12,0,0,0,0,0},
    {12,6,3,3,3,6,12,0},{6,12,24,24,24,12,6,0},{0,102,60,255,60,102,0,0},{0,12,12,63,12,12,0,0},
    {0,0,0,0,0,12,12,24},{0,0,0,63,0,0,0,0},{0,0,0,0,0,24,24,0},{96,48,24,12,6,3,1,0},
    {62,99,115,123,111,103,62,0},{12,14,15,12,12,12,63,0},{30,51,48,28,6,51,63,0},{30,51,48,28,48,51,30,0},
    {56,60,54,51,127,48,120,0},{63,3,31,48,48,51,30,0},{28,6,3,31,51,51,30,0},{63,51,48,24,12,12,12,0},
    {30,51,51,30,51,51,30,0},{30,51,51,62,48,24,14,0},{0,24,24,0,0,24,24,0},{0,12,12,0,0,12,12,24},
    {24,12,6,3,6,12,24,0},{0,0,63,0,63,0,0,0},{3,6,12,24,12,6,3,0},{30,51,48,24,12,0,12,0},
    {62,99,123,123,123,3,30,0},{12,30,51,51,63,51,51,0},{31,54,54,30,54,54,31,0},{60,102,3,3,3,102,60,0},
    {31,54,102,102,102,54,31,0},{127,70,22,30,22,70,127,0},{127,70,22,30,22,6,15,0},{60,102,3,3,115,102,124,0},
    {51,51,51,63,51,51,51,0},{30,12,12,12,12,12,30,0},{120,48,48,48,51,51,30,0},{103,102,54,30,54,102,103,0},
    {15,6,6,6,70,102,127,0},{99,119,127,107,99,99,99,0},{99,103,111,123,115,99,99,0},{28,54,99,99,99,54,28,0},
    {31,54,54,30,6,6,15,0},{30,51,51,51,59,30,56,0},{31,54,54,30,54,102,103,0},{30,51,7,14,56,51,30,0},
    {63,45,12,12,12,12,30,0},{51,51,51,51,51,51,63,0},{51,51,51,51,51,30,12,0},{99,99,99,107,127,119,99,0},
    {99,99,54,28,54,99,99,0},{51,51,51,30,12,12,30,0},{127,99,49,24,76,102,127,0},{30,6,6,6,6,6,30,0},
    {3,6,12,24,48,96,64,0},{30,24,24,24,24,24,30,0},{8,28,54,99,0,0,0,0},{0,0,0,0,0,0,0,255},
    {12,12,24,0,0,0,0,0},{0,0,30,48,62,51,110,0},{7,6,6,30,54,54,27,0},{0,0,30,51,3,51,30,0},
    {56,48,48,60,54,54,108,0},{0,0,30,51,63,3,30,0},{28,54,6,15,6,6,15,0},{0,0,108,54,54,60,48,31},
    {7,6,54,110,102,102,103,0},{12,0,14,12,12,12,30,0},{48,0,56,48,48,54,54,28},{7,6,102,54,30,54,103,0},
    {14,12,12,12,12,12,30,0},{0,0,51,127,107,99,99,0},{0,0,31,51,51,51,51,0},{0,0,30,51,51,51,30,0},
    {0,0,27,54,54,30,6,15},{0,0,108,54,54,60,48,120},{0,0,27,54,6,6,15,0},{0,0,62,3,30,48,31,0},
    {8,12,62,12,12,44,24,0},{0,0,51,51,51,51,110,0},{0,0,51,51,51,30,12,0},{0,0,99,99,107,127,54,0},
    {0,0,99,54,28,54,99,0},{0,0,51,51,51,62,48,31},{0,0,63,25,12,38,63,0},{56,12,12,7,12,12,56,0},
    {24,24,24,0,24,24,24,0},{7,12,12,56,12,12,7,0},{110,59,0,0,0,0,0,0},{0,8,28,54,99,99,127,0}
};

/*
 * Re-upload the 8x8 font into VGA font plane 2, mirroring the kernel's
 * vga_restore_text_font(): switch to font-access mode (sequential addressing,
 * even/odd disabled), write each glyph twice to fill a 16-scanline cell so
 * both scanline halves carry the glyph, then restore normal text addressing.
 * With a 9th "8-width" blank column the glyphs need the usual doubling: a
 * value repeats in consecutive bytes so the 9-dot character cell shows an
 * 8x8 glyph stretched to 16 rows.
 */
static void text_load_font(void) {
    volatile uint8_t *vram = (volatile uint8_t *)0xA0000;
    uint8_t cell[16];

    for (int ch = 0; ch < 256; ++ch) {
        int glyph = (ch >= 32 && ch < 127) ? ch - 32 : 0;

        for (int row = 0; row < 8; ++row) {
            /* reverse bits: VGA fonts store the leftmost pixel in bit 7 */
            uint8_t bits = text_font8x8[glyph][row];
            uint8_t rev = 0;

            for (int b = 0; b < 8; ++b) {
                rev = (uint8_t)((rev << 1) | (bits & 1u));
                bits >>= 1;
            }
            cell[row * 2] = rev;
            cell[row * 2 + 1] = rev;
        }

        outb(0x3C4, 0x02); outb(0x3C5, 0x04);   /* map mask: plane 2 only */
        outb(0x3C4, 0x04); outb(0x3C5, 0x06);   /* chain4/odd-even off, sequential access */
        outb(0x3CE, 0x04); outb(0x3CF, 0x02);   /* read map: plane 2 */
        outb(0x3CE, 0x05); outb(0x3CF, 0x00);   /* read mode 0, write mode 0 */
        outb(0x3CE, 0x06); outb(0x3CF, 0x04);   /* map window at 0xA0000 */
        for (int i = 0; i < 16; ++i) {
            vram[ch * 32 + i] = cell[i];
        }
    }

    outb(0x3C4, 0x02); outb(0x3C5, 0x03);       /* map mask: planes 0+1 (text) */
    outb(0x3C4, 0x04); outb(0x3C5, 0x03);       /* back to normal text addressing */
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);
}

/*
 * Drop the card into a real 80x25 VGA mode 3 so the error is readable on
 * any hardware, including machines where GRUB left a graphics mode.
 *
 * FIRST kill the Bochs VBE extension the same way the kernel's VGA backend
 * does (src/driver/video/vga.c): when a VBE mode is enabled, the video
 * memory window at 0xA0000 is a single 64K bank and 0xB8000-0xBFFFF is not
 * decoded at all, so every text write below would silently vanish (black
 * screen with only the hardware cursor blinking). Disabling the extension
 * with ENABLE=0 before touching any VGA register restores the classic
 * 128K text/graphics window mapping on every VBE-capable card (std, VMWare,
 * QXL, VirtIO and Bochs alike), because QEMU's VGA core is the same.
 */
static void text_force_mode3(void) {
    static const uint8_t seq[5]    = {0x03, 0x00, 0x03, 0x00, 0x02};
    static const uint8_t crtc[25] = {0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF,
                                     0x1F, 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00,
                                     0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x1F,
                                     0x96, 0xB9, 0xA3, 0xFF};
    static const uint8_t gc[9]     = {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E,
                                      0x00, 0xFF};

    /* ---- SVGA extension reset (86Box / real 486-era hardware) ----
     * GRUB's VESA mode set leaves vendor extension registers in
     * graphics state. On real SVGA cards the standard mode-3 registers
     * alone cannot re-map 0xB8000, so every text write vanishes into a
     * bank/linear window. Each write below targets one vendor's
     * graphics-mode enable bits; on plain VGA and other vendors the
     * register indices are simply not implemented (ignored). */

    /* Bochs DISPI (QEMU std/QXL/VMware/VirtIO): VBE off. */
    outw(0x1CE, 0x0004);
    outw(0x1CF, 0x0000);

    /* Tseng ET4000/W32: segment select 0 (no linear/banked window),
     * extended clock selects back to defaults. */
    outb(0x3CD, 0x00);
    outb(0x3C4, 0x06); outb(0x3C5, 0x00);
    outb(0x3C4, 0x07); outb(0x3C5, 0x00);

    /* Trident TVGA: page/bank 0. */
    outb(0x3C4, 0x0E); outb(0x3C5, 0x00);

    /* Paradise/WD90C: lock the extensions (plain VGA decode). */
    outb(0x3D4, 0x29); outb(0x3D5, 0x00);

    /* S3 86C9xx/Trio: unlock, enhanced mode + linear aperture + ext
     * clock/bank state off, then re-lock. */
    outb(0x3D4, 0x38); outb(0x3D5, 0x48);
    outb(0x3D4, 0x40); outb(0x3D5, 0x00);
    outb(0x3D4, 0x31); outb(0x3D5, 0x00);
    outb(0x3D4, 0x51); outb(0x3D5, 0x00);
    outb(0x3D4, 0x58); outb(0x3D5, 0x00);
    outb(0x3D4, 0x38); outb(0x3D5, 0x00);

    /* Cirrus CL-GD54xx: unlock, plain VGA memory decode, no MMIO
     * overlay on 0xB8000, bank registers 0, then re-lock. */
    outb(0x3C4, 0x06); outb(0x3C5, 0x12);
    outb(0x3C4, 0x07); outb(0x3C5, 0x00);
    outb(0x3C4, 0x17); outb(0x3C5, 0x00);
    outb(0x3CE, 0x09); outb(0x3CF, 0x00);
    outb(0x3CE, 0x0A); outb(0x3CF, 0x00);
    outb(0x3C4, 0x06); outb(0x3C5, 0x0F);

    /* Trident TVGA: back to "old mode" = plain VGA decode. Must come
     * after the Cirrus block (SR0B is only writable in Trident's new
     * mode; on other cards it is not implemented). */
    outb(0x3C4, 0x0B); outb(0x3C5, 0x00);

    outb(0x3C2, 0x67);            /* MISC: color text mode clocking */

    outb(0x3C4, 0x00);            /* SEQ reset */
    outb(0x3C5, 0x01);
    for (int i = 0; i < 5; ++i) {
        outb(0x3C4, (uint8_t)i);
        outb(0x3C5, seq[i]);
    }

    outb(0x3D4, 0x11);            /* CRTC: clear write protect first */
    outb(0x3D5, 0x7E);
    for (int i = 0; i < 25; ++i) {
        outb(0x3D4, (uint8_t)i);
        outb(0x3D5, crtc[i]);
    }
    outb(0x3D4, 0x11);
    outb(0x3D5, 0x8E);

    for (int i = 0; i < 9; ++i) { /* GC */
        outb(0x3CE, (uint8_t)i);
        outb(0x3CF, gc[i]);
    }

    /* Attribute controller: palette identity, text-mode control registers,
     * then enable display. Registers 0x10-0x14 are critical: AC[0x10]=0x0C
     * selects TEXT mode. If the card was left in a VBE graphics mode and
     * this register keeps its graphics bit, every write to 0xB8000 lands
     * in graphics addressing and the screen stays black. */
    (void)inb(0x3DA);
    for (int i = 0; i < 16; ++i) {
        outb(0x3C0, (uint8_t)i);
        outb(0x3C0, (uint8_t)i);
    }
    outb(0x3C0, 0x10); outb(0x3C0, 0x0C); /* mode: text, color */
    outb(0x3C0, 0x11); outb(0x3C0, 0x00); /* overscan */
    outb(0x3C0, 0x12); outb(0x3C0, 0x0F); /* color plane enable */
    outb(0x3C0, 0x13); outb(0x3C0, 0x08); /* horizontal pel */
    outb(0x3C0, 0x14); outb(0x3C0, 0x00); /* color select */
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);                    /* enable display */

    /* Clear the CRT disable state a VBE mode set may have left. */
    outb(0x3C0, 0x00); outb(0x3C0, 0x20);
    (void)inb(0x3DA);

    text_load_font();             /* VBE wiped the BIOS font: re-upload */
    text_screen_fill(0x4F);       /* white on solid red */
}

static void text_append_uint(char *buffer, int *len, int max_len, uint32_t value) {
    char digits[12];
    int pos = 0;

    if (value == 0) {
        if (*len < max_len - 1) {
            buffer[(*len)++] = '0';
        }
        return;
    }
    while (value > 0 && pos < 11) {
        digits[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (pos > 0 && *len < max_len - 1) {
        buffer[(*len)++] = digits[--pos];
    }
    if (*len < max_len) {
        buffer[*len] = '\0';
    }
}

static void text_append_hex32(char *buffer, int *len, int max_len, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";

    for (int shift = 28; shift >= 0 && *len < max_len - 9; shift -= 4) {
        buffer[(*len)++] = digits[(value >> shift) & 0x0Fu];
    }
    buffer[*len] = '\0';
}

/*
 * Report installed/advertised physical RAM rather than summing only the
 * usable E820 fragments.  Firmware commonly reserves small regions inside
 * the RAM address space; summing usable ranges therefore makes an 8 MiB
 * machine look like roughly 7.5 MiB and incorrectly trips an "8 MB minimum"
 * check.  Multiboot's mem_lower/mem_upper fields describe the conventional
 * and extended physical-memory size, so use that as the baseline.
 *
 * The E820 map is still consulted and can raise the result for machines
 * where the basic memory fields are missing or capped.  The value is
 * saturated at 0xFFFFFFFF, which the display already treats as 4 GiB+. */
static uint32_t detect_total_ram(const MbInfo *mbi) {
    uint64_t reported = 0;
    uint64_t e820_top = 0;

    if ((mbi->flags & MB_FLAG_MEM) != 0) {
        reported = ((uint64_t)mbi->mem_lower + (uint64_t)mbi->mem_upper) * 1024ull;
    }

    if ((mbi->flags & MB_FLAG_MMAP) != 0 && mbi->mmap_addr != 0 && mbi->mmap_length != 0) {
        uint32_t cursor = mbi->mmap_addr;
        uint32_t end = mbi->mmap_addr + mbi->mmap_length;

        while (cursor + 4 <= end) {
            const uint32_t *entry_size_ptr = (const uint32_t *)(size_t_)cursor;
            uint32_t entry_size = *entry_size_ptr;

            if (entry_size < 20u || cursor + entry_size + 4u > end) {
                break;
            }

            /* entry: size(4) base(8) length(8) type(4); type 1 = usable */
            {
                const uint32_t *fields = (const uint32_t *)(size_t_)(cursor + 4);
                uint64_t base = ((uint64_t)fields[1] << 32) | fields[0];
                uint64_t length = ((uint64_t)fields[3] << 32) | fields[2];
                uint32_t type = fields[4];

                if (type == 1u && length != 0) {
                    uint64_t end_addr = base + length;
                    if (end_addr > e820_top) {
                        e820_top = end_addr;
                    }
                }
            }

            cursor += entry_size + 4u;
        }
    }

    if (e820_top > reported) {
        reported = e820_top;
    }

    if (reported == 0) {
        return 0;
    }
    if (reported > 0xFFFFFFFFull) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)reported;
}

typedef struct {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, eip, cs, ds, es, fs, gs, eflags;
} LoaderRegs;

static void capture_regs(LoaderRegs *regs) {
    __asm__ volatile(
        "movl %%eax, %0\n"
        "movl %%ebx, %1\n"
        "movl %%ecx, %2\n"
        "movl %%edx, %3\n"
        "movl %%esi, %4\n"
        "movl %%edi, %5\n"
        "movl %%ebp, %6\n"
        "movl %%esp, %7\n"
        "mov %%cs, %%eax\n movl %%eax, %8\n"
        "mov %%ds, %%eax\n movl %%eax, %9\n"
        "mov %%es, %%eax\n movl %%eax, %10\n"
        "mov %%fs, %%eax\n movl %%eax, %11\n"
        "mov %%gs, %%eax\n movl %%eax, %12\n"
        "pushfl\n popl %%eax\n movl %%eax, %13\n"
        : "=m"(regs->eax), "=m"(regs->ebx), "=m"(regs->ecx), "=m"(regs->edx),
          "=m"(regs->esi), "=m"(regs->edi), "=m"(regs->ebp), "=m"(regs->esp),
          "=m"(regs->cs), "=m"(regs->ds), "=m"(regs->es), "=m"(regs->fs),
          "=m"(regs->gs), "=m"(regs->eflags)
        :
        : "eax", "memory"
    );
    regs->eip = 0; /* no exception frame in the loader */
}


/*
 * Graphical RAM-error layout.
 *
 * Keep this separate from the VGA text-mode layout.  The graphical screen
 * is a 640x480-style pixel canvas in common configurations, so using the
 * old 80x25 cell coordinates made the long warning look cramped and left
 * the diagnostic block with inconsistent vertical spacing.
 */
static void show_ram_error_screen_graphical(const MbInfo *mbi, uint32_t total) {
    char line[VGA_TEXT_COLS + 1];
    uint32_t fg = fb_white(mbi);
    uint32_t bg = fb_red(mbi);

    const char *warning[] = {
        "Looks like you don't have enough memory in this system to run properly!",
        "HaloxOS requires at least 8MB of RAM to boot...",
        "Please make sure to upgrade the memory to more."
    };

    err_begin(mbi);

    /* Header: centered near the top with deliberate breathing room. */
    fb_text_center(mbi, 24u, "***** BOOT ERROR!!! *****", fg, bg);

    /* Main explanation: each sentence is independently centered. */
    fb_text_center_block(mbi, 64u, warning, 3u, 20u, fg, bg);

    /* Memory amount: centered and visually separated from the paragraph. */
    line[0] = '\0';
    {
        int len = 0;
        if (total == 0) {
            const char *s = "Your Memory: unknown";
            while (s[len] != '\0' && len < (int)sizeof(line) - 1) {
                line[len] = s[len];
                ++len;
            }
            line[len] = '\0';
        } else if (total == 0xFFFFFFFFu) {
            const char *s = "Your Memory: 4.00 GB or more";
            while (s[len] != '\0' && len < (int)sizeof(line) - 1) {
                line[len] = s[len];
                ++len;
            }
            line[len] = '\0';
        } else {
            uint32_t mb_x100 = (uint32_t)(((uint64_t)total * 100u) >> 20);
            const char *unit = "MB";
            uint32_t value_x100 = mb_x100;
            if (mb_x100 >= 100000u) {
                value_x100 = mb_x100 / 1024u;
                unit = "GB";
            }

            const char *prefix = "Your Memory: ";
            int p = 0;
            while (prefix[p] != '\0' && len < (int)sizeof(line) - 1) {
                line[len++] = prefix[p++];
            }
            text_append_uint(line, &len, sizeof(line), value_x100 / 100u);
            if (len < (int)sizeof(line) - 2) line[len++] = '.';
            if (len < (int)sizeof(line) - 3) {
                uint32_t f = value_x100 % 100u;
                line[len++] = (char)('0' + (f / 10u));
                line[len++] = (char)('0' + (f % 10u));
            }
            line[len] = '\0';
            {
                int u = 0;
                while (unit[u] != '\0' && len < (int)sizeof(line) - 1) {
                    line[len++] = unit[u++];
                }
                line[len] = '\0';
            }
        }
    }
    fb_text_center(mbi, 148u, line, fg, bg);

#if LOADER_DEBUG
    {
        LoaderRegs regs;
        int len;

        capture_regs(&regs);

        /* Diagnostics are deliberately left aligned in a clean lower block. */
        fb_text_left(mbi, 198u, 32u, "** DEBUGGER INFO:", fg, bg);

        line[0] = '\0'; len = 0;
        {
            const char *s = "EAX=";
            int n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0';
            text_append_hex32(line, &len, sizeof(line), regs.eax);
            s = " EBX="; n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0';
            text_append_hex32(line, &len, sizeof(line), regs.ebx);
        }
        fb_text_left(mbi, 214u, 32u, line, fg, bg);

        line[0] = '\0'; len = 0;
        {
            const char *s = "ECX="; int n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0'; text_append_hex32(line, &len, sizeof(line), regs.ecx);
            s = " EDX="; n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0'; text_append_hex32(line, &len, sizeof(line), regs.edx);
        }
        fb_text_left(mbi, 230u, 32u, line, fg, bg);

        line[0] = '\0'; len = 0;
        {
            const char *s = "ESI="; int n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0'; text_append_hex32(line, &len, sizeof(line), regs.esi);
            s = " EDI="; n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0'; text_append_hex32(line, &len, sizeof(line), regs.edi);
        }
        fb_text_left(mbi, 246u, 32u, line, fg, bg);

        line[0] = '\0'; len = 0;
        {
            const char *s = "EBP="; int n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0'; text_append_hex32(line, &len, sizeof(line), regs.ebp);
            s = " ESP="; n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0'; text_append_hex32(line, &len, sizeof(line), regs.esp);
        }
        fb_text_left(mbi, 262u, 32u, line, fg, bg);

        line[0] = '\0'; len = 0;
        {
            const char *s = "EFLAGS="; int n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0'; text_append_hex32(line, &len, sizeof(line), regs.eflags);
            s = " CS="; n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0'; text_append_hex32(line, &len, sizeof(line), regs.cs);
            s = " DS="; n = 0;
            while (s[n] && len < (int)sizeof(line)-1) line[len++] = s[n++];
            line[len] = '\0'; text_append_hex32(line, &len, sizeof(line), regs.ds);
        }
        fb_text_left(mbi, 278u, 32u, line, fg, bg);

        fb_text_left(mbi, 310u, 32u, "Memory Map:", fg, bg);

        if ((mbi->flags & MB_FLAG_MMAP) != 0 && mbi->mmap_addr != 0 && mbi->mmap_length != 0) {
            uint32_t cursor = mbi->mmap_addr;
            uint32_t end = mbi->mmap_addr + mbi->mmap_length;
            uint32_t y = 330u;
            uint32_t column = 0;
            line[0] = '\0'; len = 0;

            while (cursor + 4u <= end && y < mbi->framebuffer_height - 8u) {
                const uint32_t *entry_size = (const uint32_t *)(size_t_)cursor;
                const uint32_t *fields;
                uint32_t base_lo;

                if (*entry_size < 20u || cursor + *entry_size + 4u > end) break;
                fields = (const uint32_t *)(size_t_)(cursor + 4u);
                base_lo = fields[0];

                text_append_hex32(line, &len, sizeof(line), base_lo);
                ++column;
                if (column == 4u) {
                    fb_text_left(mbi, y, 32u, line, fg, bg);
                    y += 16u;
                    column = 0;
                    line[0] = '\0';
                    len = 0;
                } else if (len + 1 < (int)sizeof(line)) {
                    line[len++] = ' ';
                    line[len] = '\0';
                }

                cursor += *entry_size + 4u;
            }
            if (column != 0u && y < mbi->framebuffer_height - 8u) {
                fb_text_left(mbi, y, 32u, line, fg, bg);
            }
        } else {
            fb_text_left(mbi, 330u, 32u, "unavailable", fg, bg);
        }
    }
#endif

    for (;;) __asm__ volatile("hlt");
}

/*
 * RAM error screen: solid red 80x25 error screen with white text, shown
 * when the machine has less memory than the boot chain needs. Uses the
 * linear framebuffer when GRUB provided one (modern GPUs), else classic
 * VGA text mode.
 */
static void show_ram_error_screen(const MbInfo *mbi) {
    char line[VGA_TEXT_COLS + 1];
    int len;
    uint32_t total = detect_total_ram(mbi);

    serial_line("NOT ENOUGH RAM: showing the error screen");

    if (err_use_framebuffer(mbi)) {
        show_ram_error_screen_graphical(mbi, total);
    }

    err_begin(mbi);

    /* Classic 80x25 DOS-style layout: center only the human-facing
     * header/message lines, while debugger data remains left-aligned. */
    text_screen_write_center(1, "***** BOOT ERROR!!! *****", 0x4F);
    text_screen_write_center(3, "Looks like you don't have enough memory in this system to run properly!", 0x4F);
    text_screen_write_center(4, "HaloxOS requires at least 8MB of RAM to boot...", 0x4F);
    text_screen_write_center(5, "Please make sure to upgrade the memory to more.", 0x4F);

    /* Build the RAM amount before rendering it so it can be centered exactly. */
    if (total == 0) {
        text_screen_write_center(7, "Your Memory: unknown", 0x4F);
    } else if (total == 0xFFFFFFFFu) {
        text_screen_write_center(7, "Your Memory: 4.00 GB or more", 0x4F);
    } else {
        /* value in hundredths of MB: (bytes * 100) / 1048576 */
        uint32_t mb_x100 = (uint32_t)(((uint64_t)total * 100u) >> 20);
        const char *unit;
        uint32_t value_x100;

        if (mb_x100 >= 100000u) {          /* >= 1000 MB: show GB */
            value_x100 = mb_x100 / 1024u;   /* hundredths of GB */
            unit = "GB";
        } else {
            value_x100 = mb_x100;
            unit = "MB";
        }

        line[0] = '\0';
        len = 0;
        {
            const char *prefix = "Your Memory: ";
            int p = 0;
            while (prefix[p] != '\0' && len < (int)sizeof(line) - 1) {
                line[len++] = prefix[p++];
            }
        }
        text_append_uint(line, &len, sizeof(line), value_x100 / 100u);
        if (len < (int)sizeof(line) - 1) {
            line[len++] = '.';
        }
        if (len < (int)sizeof(line) - 2) {
            uint32_t f = value_x100 % 100u;
            line[len++] = (char)('0' + (f / 10u));
            line[len++] = (char)('0' + (f % 10u));
        }
        {
            int u = 0;
            while (unit[u] != '\0' && len < (int)sizeof(line) - 1) {
                line[len++] = unit[u++];
            }
        }
        line[len] = '\0';
        text_screen_write_center(7, line, 0x4F);
    }

#if LOADER_DEBUG
    {
        LoaderRegs regs;
        int row = 10;

        capture_regs(&regs);
        err_write(row, 4, "** DEBUGGER INFO:");
        row = 12;

        {
            char *names[4][2] = {
                {"EAX=", "EBX="}, {"ECX=", "EDX="}, {"ESI=", "EDI="}, {"EBP=", "ESP="}
            };
            const uint32_t *values[4][2] = {
                {&regs.eax, &regs.ebx}, {&regs.ecx, &regs.edx},
                {&regs.esi, &regs.edi}, {&regs.ebp, &regs.esp}
            };
            for (int r = 0; r < 4 && row < VGA_TEXT_ROWS; ++r, ++row) {
                line[0] = '\0';
                len = 0;
                for (int c = 0; c < 2; ++c) {
                    if (c > 0) {
                        if (len < (int)sizeof(line) - 2) {
                            line[len++] = ' ';
                            line[len] = '\0';
                        }
                    }
                    const char *nm = names[r][c];
                    int n = 0;
                    while (nm[n] != '\0' && len < (int)sizeof(line) - 1) {
                        line[len++] = nm[n++];
                    }
                    line[len] = '\0';
                    text_append_hex32(line, &len, sizeof(line), *values[r][c]);
                }
                err_write(row, 4, line);
            }
        }

        {
            line[0] = '\0';
            len = 0;
            const char *nm = "EFLAGS=";
            int n = 0;
            while (nm[n] != '\0') { line[len++] = nm[n++]; }
            text_append_hex32(line, &len, sizeof(line), regs.eflags);
            nm = " CS=";
            n = 0;
            while (nm[n] != '\0') { line[len++] = nm[n++]; }
            text_append_hex32(line, &len, sizeof(line), regs.cs);
            nm = " DS=";
            n = 0;
            while (nm[n] != '\0' && len < (int)sizeof(line) - 10) { line[len++] = nm[n++]; }
            text_append_hex32(line, &len, sizeof(line), regs.ds);
            line[len] = '\0';
            if (row < VGA_TEXT_ROWS) {
                err_write(row, 4, line);
                ++row;
            }
        }

        /* Leave a blank row between the register block and flags/map. */
        row += 1;
        if (row < VGA_TEXT_ROWS) {
            err_write(row, 4, "Memory Map:");
            ++row;
        }
        if ((mbi->flags & MB_FLAG_MMAP) != 0 && mbi->mmap_addr != 0 && mbi->mmap_length != 0) {
            uint32_t cursor = mbi->mmap_addr;
            uint32_t end = mbi->mmap_addr + mbi->mmap_length;
            int column = 0;

            while (cursor + 4 <= end && row < VGA_TEXT_ROWS) {
                const uint32_t *entry_size = (const uint32_t *)(size_t_)cursor;
                const uint32_t *fields;
                uint32_t base_lo;

                if (cursor + *entry_size + 4 > end) {
                    break;
                }
                fields = (const uint32_t *)(size_t_)(cursor + 4);
                base_lo = fields[0];

                if (column == 0) {
                    line[0] = '\0';
                    len = 0;
                }
                text_append_hex32(line, &len, sizeof(line), base_lo);
                ++column;
                if (column == 4) {
                    err_write(row, 4, line);
                    ++row;
                    column = 0;
                } else if (len + 12 < (int)sizeof(line)) {
                    line[len++] = ' ';
                    line[len] = '\0';
                }

                cursor += *entry_size + 4;
            }
            if (column > 0 && row < VGA_TEXT_ROWS) {
                err_write(row, 4, line);
            }
        } else {
            if (row < VGA_TEXT_ROWS) {
                err_write(row, 4, "unavailable");
            }
        }
    }
#endif

    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void serial_init(void) {
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x01);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);
}

static void serial_tx(char ch) {
    int i;
    for (i = 0; i < 100000; ++i) {
        if ((inb(SERIAL_PORT + 5) & 0x20u) != 0) {
            break;
        }
    }
    outb(SERIAL_PORT, (uint8_t)ch);
}

static void serial_puts(const char *text) {
    while (*text) {
        if (*text == '\n') {
            serial_tx('\r');
        }
        serial_tx(*text++);
    }
}

static void serial_put_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        serial_tx(digits[(value >> shift) & 0x0Fu]);
    }
}

static void serial_put_uint(uint32_t value) {
    char buffer[12];
    int pos = 0;

    if (value == 0) {
        serial_tx('0');
        return;
    }
    while (value > 0 && pos < 11) {
        buffer[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (pos > 0) {
        serial_tx(buffer[--pos]);
    }
}

static void serial_line(const char *text) {
    serial_puts("[LDR]: ");
    serial_puts(text);
    serial_puts("\n");
}

static void serial_line_hex(const char *label, uint32_t value) {
    serial_puts("[LDR]: ");
    serial_puts(label);
    serial_puts(": ");
    serial_put_hex32(value);
    serial_puts("\n");
}

static void serial_line_uint(const char *label, uint32_t value) {
    serial_puts("[LDR]: ");
    serial_puts(label);
    serial_puts(": ");
    serial_put_uint(value);
    serial_puts("\n");
}

static void loader_memset(void *dest, uint8_t value, size_t_ n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) {
        *d++ = value;
    }
}

#define ZSTD_STATIC_LINKING_ONLY
#include "../third_party/zstd-1.5.7/lib/zstd.h"

static void loader_hang(const char *reason, const MbInfo *mbi);

/*
 * Loader entry. Never returns: either jumps into the kernel or halts with
 * a fatal serial trace line.
 */
void loader_main(uint32_t magic, const MbInfo *mbi) {
    const MbModule *modules;
    const ModuleHeader *header;
    const MbModule *kernel_module = 0;
    LoaderInfo *info = (LoaderInfo *)LOADER_INFO_ADDR;
    ZSTD_DCtx *dctx;
    size_t result;
    uint32_t mods;
    uint32_t i;

    serial_init();
    serial_line("HaloxOS zstd loader 1.0");
    serial_line("zstd decompressor: 1.5.7 (vendored)");
    serial_line_hex("multiboot magic", magic);
    serial_line_hex("multiboot info", (uint32_t)(size_t_)mbi);

    if (magic != MB_MAGIC_EXPECTED) {
        loader_hang("not started by a multiboot bootloader", mbi);
    }
    if (mbi == 0) {
        loader_hang("no multiboot info pointer", mbi);
    }

    serial_line_hex("multiboot flags", mbi->flags);

    /* Log the raw Multiboot memory fields before the RAM gate so a failing
     * machine can be diagnosed from COM1 even when the loader halts here. */
    if ((mbi->flags & MB_FLAG_MEM) != 0) {
        serial_line_uint("mem lower KB", mbi->mem_lower);
        serial_line_uint("mem upper KB", mbi->mem_upper);
    }
    if ((mbi->flags & MB_FLAG_MMAP) != 0) {
        serial_line_hex("mmap addr", mbi->mmap_addr);
        serial_line_hex("mmap length", mbi->mmap_length);
    }

    /* RAM floor FIRST: on a machine with too little memory, nothing else
     * in this loader can safely run (module memory may not even exist),
     * and the graphics checks below would hang invisibly otherwise. */
    {
        uint32_t total_ram = detect_total_ram(mbi);

        serial_line_uint("required RAM bytes", MIN_RAM_BYTES);
        serial_line_uint("detected/advertised RAM bytes", total_ram);
#if HALOXOS_FORCE_RAM_ERROR
        serial_line("TEST BUILD: forcing the not-enough-RAM error screen");
        (void)total_ram;
        show_ram_error_screen(mbi);
#else
        if (total_ram < MIN_RAM_BYTES) {
            show_ram_error_screen(mbi);
        }
#endif
    }

    /*
     * Video handoff check: with a graphics-mode request in the loader's
     * multiboot header, GRUB programs the VBE mode and reports it via the
     * framebuffer fields. If GRUB could NOT set a graphics mode (bit 12
     * clear) on a graphics build, the kernel would fall through to its
     * classic VGA register probing, which corrupts real graphics cards
     * (jailbar garbage). Refuse to boot instead.
     */
#if HALOXOS_BOOT_GRAPHICS_EXPECTED
    if ((mbi->flags & (1u << 12)) == 0) {
        serial_line("GRUB reported no framebuffer info");
        loader_hang("graphics mode was not set by the bootloader", mbi);
    }
    serial_line_hex("framebuffer addr", (uint32_t)(size_t_)mbi_fb_addr(mbi));
    serial_line_uint("framebuffer width", mbi_fb_width(mbi));
    serial_line_uint("framebuffer height", mbi_fb_height(mbi));
    serial_line_uint("framebuffer bpp", mbi_fb_bpp(mbi));
    serial_line_uint("framebuffer pitch", mbi_fb_pitch(mbi));
#endif

    if ((mbi->flags & MB_FLAG_MODS) == 0 || mbi->mods_count == 0) {
        loader_hang("no multiboot module (kernel.bin.zst missing)", mbi);
    }

    mods = mbi->mods_count;
    modules = (const MbModule *)(size_t_)mbi->mods_addr;
    serial_line_uint("module count", mods);

    for (i = 0; i < mods; ++i) {
        const ModuleHeader *h = (const ModuleHeader *)(size_t_)modules[i].mod_start;
        serial_puts("[LDR]: module ");
        serial_put_uint(i);
        serial_puts(": ");
        serial_put_hex32(modules[i].mod_start);
        serial_puts(" - ");
        serial_put_hex32(modules[i].mod_end);
        serial_puts("\n");
        if (h->magic == HALOXOS_MODULE_MAGIC) {
            kernel_module = &modules[i];
        }
    }

    if (kernel_module == 0) {
        loader_hang("no module carries the HaloxOS zstd header", mbi);
    }

    header = (const ModuleHeader *)(size_t_)kernel_module->mod_start;
    serial_line("compressed kernel module found");
    serial_line_hex("kernel entry", header->kernel_entry);
    serial_line_uint("original kernel bytes", header->image_size);
    serial_line_uint("zstd frame bytes", header->frame_size);

    if (header->frame_size == 0 ||
        kernel_module->mod_start + sizeof(ModuleHeader) + header->frame_size > kernel_module->mod_end) {
        loader_hang("module header frame size does not match module bounds", mbi);
    }

    if (header->image_size == 0 ||
        KERNEL_LOAD_ADDR + header->image_size > KERNEL_SCRATCH) {
        loader_hang("decompressed kernel would overlap the zstd scratch area", mbi);
    }

    serial_line_hex("decompress target", KERNEL_LOAD_ADDR);
    serial_line_hex("dctx scratch", KERNEL_SCRATCH);
    serial_line_uint("dctx scratch bytes", KERNEL_SCRATCH_SIZE);

    serial_line_hex("zstd version number", ZSTD_versionNumber());
    serial_puts("[LDR]: zstd version string: ");
    serial_puts(ZSTD_versionString());
    serial_puts("\n");
    serial_line_uint("zstd estimateDCtxSize", (uint32_t)ZSTD_estimateDCtxSize());

    dctx = ZSTD_initStaticDCtx((void *)KERNEL_SCRATCH, KERNEL_SCRATCH_SIZE);
    if (dctx == 0) {
        loader_hang("ZSTD_initStaticDCtx rejected the scratch block", mbi);
    }
    serial_line("static DCtx ready");

    serial_line("zstd frame check");
    {
        ZSTD_FrameHeader fh;
        size_t_ fh_result = ZSTD_getFrameHeader(&fh,
                                                (const void *)(kernel_module->mod_start + sizeof(ModuleHeader)),
                                                header->frame_size);
        if (ZSTD_isError(fh_result)) {
            serial_puts("[LDR]: FATAL frame header error: ");
            serial_puts(ZSTD_getErrorName(fh_result));
            serial_puts("\n");
            loader_hang("zstd frame header invalid", mbi);
        }
        serial_line_uint("frame content size", (uint32_t)fh.frameContentSize);
        if (fh.frameContentSize != (unsigned long long)header->image_size) {
            loader_hang("frame content size does not match module header", mbi);
        }
        if (fh.checksumFlag) {
            serial_line("frame uses XXH64 content checksum");
        } else {
            serial_line("frame has no content checksum");
        }
        serial_line_hex("frame window size", (uint32_t)fh.windowSize);
    }

    serial_line("decompression started");
    result = ZSTD_decompressDCtx(dctx,
                                 (void *)KERNEL_LOAD_ADDR,
                                 (size_t_)header->image_size,
                                 (const void *)(kernel_module->mod_start + sizeof(ModuleHeader)),
                                 (size_t_)header->frame_size);

    if (ZSTD_isError(result)) {
        serial_puts("[LDR]: FATAL decompress error: ");
        serial_puts(ZSTD_getErrorName(result));
        serial_puts(" code ");
        serial_put_hex32((uint32_t)ZSTD_getErrorCode(result));
        serial_puts("\n");
        loader_hang("zstd decompression failed", mbi);
    }

    serial_line("decompression finished");
    serial_line_uint("regenerated bytes", (uint32_t)result);
    if (result != (size_t_)header->image_size) {
        loader_hang("regenerated size does not match module header", mbi);
    }

    /* Flat-image sanity: offset 0 must be the kernel's multiboot header,
     * whose first dword is the multiboot magic. (The image is a flat binary
     * laid out at its link address, not an ELF file.) */
    {
        const uint8_t *image = (const uint8_t *)KERNEL_LOAD_ADDR;
        uint32_t image_magic = (uint32_t)image[0] |
                              ((uint32_t)image[1] << 8) |
                              ((uint32_t)image[2] << 16) |
                              ((uint32_t)image[3] << 24);
        serial_line_hex("kernel image first dword", image_magic);
        if (image_magic != 0x1BADB002u) {
            loader_hang("decompressed image is not a HaloxOS flat kernel", mbi);
        }
        serial_line("flat kernel image verified");
    }

    /* hand the measured stats to the kernel for its own debug trace */
    loader_memset(info, 0, sizeof(*info));
    info->magic = HALOXOS_MODULE_MAGIC;
    info->module_start = kernel_module->mod_start;
    info->module_end = kernel_module->mod_end;
    info->compressed_size = header->frame_size;
    info->original_size = header->image_size;
    if (header->image_size != 0) {
        /* percent x10 (77 = 7.7%) in pure 32-bit math.  frame x 1000
         * stays below 2^32 for any frame under 4 MB, which every real
         * zstd frame of this kernel is by a wide margin. */
        if (header->frame_size < 4000000u) {
            info->ratio_percent_x10 = (header->frame_size * 1000u) / header->image_size;
        } else {
            info->ratio_percent_x10 = 999u;
        }
    }
    info->dctx_size = (uint32_t)ZSTD_estimateDCtxSize();
    info->kernel_entry = header->kernel_entry;
    serial_line_hex("loader info block", LOADER_INFO_ADDR);

    serial_line("jumping to kernel");
    serial_puts("[LDR]: loader done\n");

    /* Register handoff per the multiboot spec: EAX = magic, EBX = info.
     * The kernel's entry point reads registers, not stack arguments. */
    jump_to_kernel(header->kernel_entry, saved_magic, saved_mbi);

    loader_hang("kernel returned to the loader", mbi);
}

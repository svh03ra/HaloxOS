// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: loader/efi/main.c, HaloxOS UEFI boot loader.
//
// This repository is licensed under the GNU General Public License.

/*
 * HaloxOS UEFI boot loader.
 *
 * On a BIOS machine GRUB boots the OS: it sets a VBE mode, loads loader.elf as
 * a multiboot kernel and hands the compressed kernel over as a multiboot
 * module.  A UEFI machine has no VBE and no INT 10h, so this file does that job
 * instead, using only interfaces the UEFI specification defines:
 *
 *   Graphics Output Protocol   the linear framebuffer and the mode list - the
 *                              UEFI equivalent of the VBE mode set, so the
 *                              desktop can still run at a resolution of the
 *                              user's choosing on a machine whose video BIOS
 *                              cannot be called at all
 *   Simple File System         the boot volume: under UEFI the kernel and its
 *                              loader are files on the ESP, not sectors found
 *                              through firmware disk services
 *   Boot Services              memory allocation, the memory map that becomes
 *                              the multiboot memory map, ExitBootServices
 *   Configuration Table        the ACPI RSDP, which a UEFI machine publishes
 *                              there rather than in the legacy BIOS area
 *
 * What the rest of the boot chain sees is deliberately identical to a BIOS
 * boot: loader.elf is placed where it was linked, a Multiboot v1 information
 * block describes memory and modules the way GRUB's does, and the loader is
 * entered in 32-bit protected mode with the multiboot register state
 * (EAX = 0x2BADB002, EBX = info block).  Everything after that point - zstd
 * decompression, the loader info block, the kernel itself - is the same code on
 * both firmware types.
 *
 * The boot volume is expected to look like a GRUB CD's /boot directory:
 *
 *   \EFI\BOOT\BOOTX64.EFI   this loader
 *   \boot\loader.elf        linked at 1 MiB
 *   \boot\kernel.bin.zst    multiboot module 1: the compressed kernel
 *   \boot\doom.wadmodule    multiboot module 2: DOOM game data (optional)
 *   \boot\uefi.cfg          display sizes for the boot menu (optional)
 *
 * Order matters in one place: no firmware service may be called after
 * ExitBootServices, so the memory map, the command line, the module table and
 * the video mode are all complete before the call, and the trace switches to
 * direct serial port writes for the last few lines.
 */

#include "efi.h"
/*
 * The kernel RAM trace ring's addresses (see src/kernel/debug/serial_log.c).
 * The kernel mirrors its COM1 characters there on a UEFI boot, and this
 * loader harvests them on the NEXT boot - RAM survives a warm reset, which
 * is when a trace is wanted.  Two addresses, tried in order:
 *
 *   high  768 MiB, the ring a machine with a gigabyte or more uses, because
 *         low memory is the first thing the next boot's firmware reuses:
 *         measured, OVMF puts DXE code over the low ring within one second
 *         of the reset, before this loader runs at all;
 *   low   the handoff area next to the loader info block, the fallback for
 *         machines too small for the high address.  Its base mirrors the
 *         kernel's own selection: the embedded-WAD build parks the handoff
 *         block at a fixed 21 MiB, every other build uses the generated
 *         span-derived address.
 */
#define KERNEL_RING_MAGIC       0x4B525458u /* 'XTRK' little endian */
#define KERNEL_RING_TEXT_OFFSET 0x1000u
#define KERNEL_RING_TEXT_BYTES  0x7000u
#define KERNEL_RING_HIGH        0x30000000u
#ifdef HALOXOS_KERNEL_IMAGE_SPAN
#define HALOXOS_KERNEL_RING_BASE 0x1500000u
#else
#include "ram_requirement.h"
#define HALOXOS_KERNEL_RING_BASE ((UINT32)HALOXOS_LOADER_INFO_ADDR)
#endif
#define KERNEL_RING_LOW (HALOXOS_KERNEL_RING_BASE + 0x8000u)

#ifndef HALOXOS_UEFI_DEFAULT_WIDTH
#define HALOXOS_UEFI_DEFAULT_WIDTH 640
#endif
#ifndef HALOXOS_UEFI_DEFAULT_HEIGHT
#define HALOXOS_UEFI_DEFAULT_HEIGHT 480
#endif
#ifndef HALOXOS_UEFI_DEFAULT_TIMEOUT
#define HALOXOS_UEFI_DEFAULT_TIMEOUT 3
#endif
/* The firmware console can be invisible on a machine with no screen attached;
 * the serial trace is what such a machine can still be diagnosed from. */
#ifndef HALOXOS_UEFI_SERIAL
#define HALOXOS_UEFI_SERIAL 1
#endif

#define SERIAL_PORT 0x3F8

/* The loader is not relocatable: one megabyte is where its ELF program headers
 * put it, and the whole megabyte is reserved so nothing else is handed out
 * there while the loader and its stack run. */
#define LOADER_LOAD_ADDR 0x00100000u
#define LOADER_LOAD_PAGES 256u

/* One 128 KiB block carries the entire handoff: the multiboot info structure,
 * the command line, the module table, the memory map and the firmware's own
 * descriptors while the map is being built. */
#define MBI_BLOCK_PAGES 32u
#define MBI_OFF_INFO 0x0000u
#define MBI_OFF_CMDLINE 0x0200u
#define MBI_OFF_LOADER_NAME 0x0400u
#define MBI_OFF_MODULES 0x0600u
#define MBI_OFF_MMAP 0x1000u
#define MBI_MMAP_MAX 1365u
#define MBI_OFF_DESCRIPTORS 0x9000u
#define MBI_DESC_BYTES 0x7000u

#define MBI_CMDLINE_MAX 192u
#define MBI_MODULE_MAX 4u
#define MENU_ENTRY_MAX 9u
#define RESERVED_MAX 12u
#define WORKSET_MAX 6u
#define ELF_PHDR_MAX 24u

/* Multiboot v1 flags. */
#define MB_FLAG_MEM (1u << 0)
#define MB_FLAG_CMDLINE (1u << 2)
#define MB_FLAG_MODS (1u << 3)
#define MB_FLAG_MMAP (1u << 6)
#define MB_FLAG_BOOT_LOADER (1u << 9)
#define MB_FLAG_FB (1u << 12)

#define HALOXOS_MODULE_MAGIC 0x484C585Au

typedef struct {
    UINT32 flags;
    UINT32 mem_lower;
    UINT32 mem_upper;
    UINT32 boot_device;
    UINT32 cmdline;
    UINT32 mods_count;
    UINT32 mods_addr;
    UINT32 syms[4];
    UINT32 mmap_length;
    UINT32 mmap_addr;
    UINT32 drives_length;
    UINT32 drives_addr;
    UINT32 config_table;
    UINT32 boot_loader_name;
    UINT32 apm_table;
    UINT32 vbe_control_info;
    UINT32 vbe_mode_info;
    UINT16 vbe_mode;
    UINT16 vbe_interface_seg;
    UINT16 vbe_interface_off;
    UINT16 vbe_interface_len;
    UINT64 framebuffer_addr;
    UINT32 framebuffer_pitch;
    UINT32 framebuffer_width;
    UINT32 framebuffer_height;
    UINT8 framebuffer_bpp;
    UINT8 framebuffer_type;
    UINT8 framebuffer_red_field_position;
    UINT8 framebuffer_red_mask_size;
    UINT8 framebuffer_green_field_position;
    UINT8 framebuffer_green_mask_size;
    UINT8 framebuffer_blue_field_position;
    UINT8 framebuffer_blue_mask_size;
} __attribute__((packed)) MultibootInfo;

typedef struct {
    UINT32 mod_start;
    UINT32 mod_end;
    UINT32 cmdline;
    UINT32 reserved;
} __attribute__((packed)) MultibootModule;

typedef struct {
    UINT32 size;
    UINT32 base_low;
    UINT32 base_high;
    UINT32 length_low;
    UINT32 length_high;
    UINT32 type;
} __attribute__((packed)) MultibootMmapEntry;

/* The compressed kernel module's own header, written by the Makefile in front
 * of the zstd frame.  It carries the size the decompressed image needs, which
 * is what tells this loader how much low memory the boot chain will use. */
typedef struct {
    UINT32 magic;
    UINT32 kernel_entry;
    UINT32 image_size;
    UINT32 frame_size;
} ModuleHeader;

typedef struct {
    UINT8 ident[16];
    UINT16 type;
    UINT16 machine;
    UINT32 version;
    UINT32 entry;
    UINT32 phoff;
    UINT32 shoff;
    UINT32 flags;
    UINT16 ehsize;
    UINT16 phentsize;
    UINT16 phnum;
    UINT16 shentsize;
    UINT16 shnum;
    UINT16 shstrndx;
} Elf32Header;

typedef struct {
    UINT32 type;
    UINT32 offset;
    UINT32 vaddr;
    UINT32 paddr;
    UINT32 filesz;
    UINT32 memsz;
    UINT32 flags;
    UINT32 align;
} Elf32ProgramHeader;

typedef struct {
    UINT32 index;
    UINT32 width;
    UINT32 height;
    UINT32 bpp;
    UINT32 pitch;
    UINT64 framebuffer;
    UINT32 red_mask;
    UINT32 green_mask;
    UINT32 blue_mask;
} VideoChoice;

typedef struct {
    UINT64 base;
    UINT64 length;
} Range;

static EFI_SYSTEM_TABLE *efi_st;
static EFI_BOOT_SERVICES *efi_bs;
static EFI_HANDLE efi_image;
static UINT8 *mbi_block;
static Range reserved[RESERVED_MAX];
static UINT32 reserved_count;
static Range workset[WORKSET_MAX];
static UINT32 workset_count;
static UINT32 mmap_entries_written;
static UINT32 mem_lower_kb;
static UINT32 mem_upper_kb;

/* ---------------------------------------------------------------------- */
/* Tiny freestanding runtime.                                             */
/* ---------------------------------------------------------------------- */

void *memcpy(void *dest, const void *src, UINTN count) {
    volatile UINT8 *d = (volatile UINT8 *)dest;
    const volatile UINT8 *s = (const volatile UINT8 *)src;

    while (count--) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, UINTN count) {
    volatile UINT8 *d = (volatile UINT8 *)dest;
    const volatile UINT8 *s = (const volatile UINT8 *)src;

    if (d < s) {
        while (count--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += count;
        s += count;
        while (count--) {
            *--d = *--s;
        }
    }
    return dest;
}

void *memset(void *dest, int value, UINTN count) {
    volatile UINT8 *d = (volatile UINT8 *)dest;

    while (count--) {
        *d++ = (UINT8)value;
    }
    return dest;
}

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
    UINT32 index;

    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3) {
        return 0;
    }
    for (index = 0; index < 8; ++index) {
        if (a->Data4[index] != b->Data4[index]) {
            return 0;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Serial trace.                                                          */
/* ---------------------------------------------------------------------- */

static void outb(UINT16 port, UINT8 value) {
    __asm__ __volatile__("outb %0, %1" ::"a"(value), "Nd"(port));
}

static UINT8 inb(UINT16 port) {
    UINT8 value;

    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
#if HALOXOS_UEFI_SERIAL
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x03);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);
#endif
}

/*
 * Mirror of the serial trace, kept in RAM so the whole loader side of the boot
 * can be handed to the boot medium as one file (see write_serial_log below).
 * Only the firmware-console echo goes through serial_putc, so every [EFI]:
 * line - and every hex dump, which is what a value like the RSDP is only
 * useful as - ends up here in the same order it was printed.
 */
#define EFI_TRACE_MAX 8192u
static char efi_trace[EFI_TRACE_MAX];
static UINTN efi_trace_bytes;

static void serial_putc(char ch) {
#if HALOXOS_UEFI_SERIAL
    UINT32 spins = 0;

    while ((inb(SERIAL_PORT + 5) & 0x20) == 0 && spins < 200000u) {
        ++spins;
    }
    outb(SERIAL_PORT, (UINT8)ch);
#else
    (void)ch;
#endif
    if (efi_trace_bytes < EFI_TRACE_MAX) {
        efi_trace[efi_trace_bytes++] = ch;
    }
}

static void serial_puts(const char *text) {
    while (*text != '\0') {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text++);
    }
}

static void serial_hex(UINT64 value) {
    static const char digits[] = "0123456789abcdef";
    int shift;
    int started = 0;

    serial_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        UINT32 nibble = (UINT32)((value >> shift) & 0xFull);

        if (nibble != 0 || started || shift == 0) {
            serial_putc(digits[nibble]);
            started = 1;
        }
    }
}

static void serial_uint(UINT32 value) {
    char buffer[11];
    int length = 0;

    if (value == 0) {
        serial_putc('0');
        return;
    }
    while (value != 0 && length < 10) {
        buffer[length++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (length > 0) {
        serial_putc(buffer[--length]);
    }
}

static void serial_line(const char *label, const char *value) {
    serial_puts("[EFI]: ");
    serial_puts(label);
    serial_puts(": ");
    serial_puts(value);
    serial_puts("\n");
}

static void serial_line_hex(const char *label, UINT64 value) {
    serial_puts("[EFI]: ");
    serial_puts(label);
    serial_puts(" ");
    serial_hex(value);
    serial_puts("\n");
}

static void serial_line_uint(const char *label, UINT32 value) {
    serial_puts("[EFI]: ");
    serial_puts(label);
    serial_puts(" ");
    serial_uint(value);
    serial_puts("\n");
}

static void serial_line_wide(const char *label, const CHAR16 *value) {
    serial_puts("[EFI]: ");
    serial_puts(label);
    serial_puts(" ");
    while (*value != 0) {
        serial_putc((char)(*value & 0x7F));
        ++value;
    }
    serial_puts("\n");
}

/* ---------------------------------------------------------------------- */
/* Firmware console.                                                      */
/* ---------------------------------------------------------------------- */

static CHAR16 console_buffer[256];
static int console_alive;

static void console_write(const CHAR16 *text) {
    if (!console_alive) {
        return;
    }
    efi_st->ConOut->OutputString(efi_st->ConOut, (CHAR16 *)text);
}

static void console_line(const CHAR16 *text) {
    console_write(text);
    console_write(L"\r\n");
}

static void console_ascii_line(const char *text) {
    UINTN index = 0;

    while (*text != '\0' && index + 1 < sizeof(console_buffer) / sizeof(CHAR16)) {
        if (*text == '\n') {
            console_buffer[index++] = '\r';
        }
        console_buffer[index++] = (CHAR16)(UINT8)*text++;
    }
    console_buffer[index] = 0;
    console_write(console_buffer);
    console_write(L"\r\n");
}

static void halt(void) {
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/* The boot volume, kept from efi_main so a panic - or any later milestone -
 * can still put the loader's trace on the medium.  A machine with no serial
 * port has no other way to report where a UEFI boot died. */
static EFI_FILE_PROTOCOL *boot_root;
static void capture_uefi_trace(void);

/* A boot failure has to stay on screen: the message IS the report, and a
 * machine that cannot be attached to a serial cable has nowhere else to show
 * it.  So the loader stops here rather than returning to the firmware. */
static void panic(const char *reason) {
    serial_line("panic", reason);
    /* The trace, including this reason, goes onto the boot medium before the
     * machine stops: on a machine with no serial port it is the only report
     * that will ever exist. */
    capture_uefi_trace();
    console_write(L"\r\nHaloxOS UEFI boot failed.\r\n");
    console_ascii_line(reason);
    console_line(L"");
    console_line(L"Check that \\boot\\loader.elf and \\boot\\kernel.bin.zst exist on the");
    console_line(L"EFI System Partition, then power cycle the machine.");
    halt();
}

/* ---------------------------------------------------------------------- */
/* Memory bookkeeping.                                                    */
/* ---------------------------------------------------------------------- */

static void range_note(Range *list, UINT32 *count, UINT32 max, UINT64 base, UINT64 length) {
    if (*count < max && length != 0) {
        list[*count].base = base;
        list[*count].length = length;
        ++*count;
    }
}

/* Areas the boot chain writes to on its own, independent of anything this
 * loader allocates: 2 MiB upwards is the kernel's fixed shadow (decompression
 * target, moved module, zeroed .bss, loader info block), and 0xE00000 is where
 * the loader parks the WAD module.  They are recorded so an allocation of ours
 * landing inside one is refused instead of silently overwritten later. */
static void workset_setup(void) {
    range_note(workset, &workset_count, WORKSET_MAX, 0x00200000ull, 0x00600000ull);
    range_note(workset, &workset_count, WORKSET_MAX, 0x00E00000ull, 0x00800000ull);
}

static int range_overlaps(UINT64 base, UINT64 length) {
    UINT32 index;

    for (index = 0; index < workset_count; ++index) {
        UINT64 start = workset[index].base;
        UINT64 end = start + workset[index].length;

        if (base < end && start < base + length) {
            return (int)index;
        }
    }
    return -1;
}

/* Allocate below 4 GiB: everything the 32-bit boot chain can address.  An
 * allocation that lands where the boot chain will write on its own is refused
 * here, while a readable message is still possible, rather than being quietly
 * overwritten during decompression. */
static EFI_STATUS allocate_low(UINTN pages, UINTN memory_type, UINT64 *out) {
    EFI_PHYSICAL_ADDRESS address = 0xFFFFFFFFull;
    EFI_STATUS status = efi_bs->AllocatePages(AllocateMaxAddress, memory_type, pages, &address);

    if (EFI_ERROR(status)) {
        return status;
    }
    if (range_overlaps((UINT64)address, (UINT64)pages * 4096ull) >= 0) {
        serial_line_hex("allocation inside the boot chain's memory", (UINT64)address);
        panic("the firmware placed a boot file where the kernel image will be loaded");
    }
    *out = (UINT64)address;
    range_note(reserved, &reserved_count, RESERVED_MAX, *out, (UINT64)pages * 4096ull);
    return EFI_SUCCESS;
}

static void reserve_fixed(UINT64 base, UINTN pages, const char *what) {
    EFI_PHYSICAL_ADDRESS address = base;

    if (EFI_ERROR(efi_bs->AllocatePages(AllocateAddress, EfiLoaderData, pages, &address))) {
        serial_line_hex("unavailable address", base);
        panic(what);
    }
    range_note(reserved, &reserved_count, RESERVED_MAX, base, (UINT64)pages * 4096ull);
}

/* ---------------------------------------------------------------------- */
/* Video: choose a mode from the firmware's own list.                      */
/* ---------------------------------------------------------------------- */

static UINT32 popcount(UINT32 value) {
    UINT32 count = 0;

    while (value != 0) {
        value &= value - 1u;
        ++count;
    }
    return count;
}

static UINT32 lowest_bit(UINT32 value) {
    UINT32 index = 0;

    while (index < 32u && (value & (1u << index)) == 0) {
        ++index;
    }
    return index;
}

/* Colour masks and pixel depth for a mode, in the two shapes a firmware
 * reports: the named RGB/BGR byte layouts, or explicit bit masks.  A mode with
 * no reachable linear framebuffer (BltOnly) or without RGB masks returns 0. */
static UINT32 mode_layout(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info, UINT32 *red, UINT32 *green,
                          UINT32 *blue) {
    if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        *red = 0x00FF0000u;
        *green = 0x0000FF00u;
        *blue = 0x000000FFu;
        return 32;
    }
    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        *red = 0x000000FFu;
        *green = 0x0000FF00u;
        *blue = 0x00FF0000u;
        return 32;
    }
    if (info->PixelFormat == PixelBitMask) {
        UINT32 all = info->PixelInformation.RedMask | info->PixelInformation.GreenMask |
                     info->PixelInformation.BlueMask | info->PixelInformation.ReservedMask;
        UINT32 bits = 0;

        if (info->PixelInformation.RedMask == 0 || info->PixelInformation.GreenMask == 0 ||
            info->PixelInformation.BlueMask == 0) {
            return 0;
        }
        while (bits < 32u && ((all >> bits) & 1u) != 0) {
            ++bits;
        }
        if (bits < 8u || bits > 32u) {
            return 0;
        }
        *red = info->PixelInformation.RedMask;
        *green = info->PixelInformation.GreenMask;
        *blue = info->PixelInformation.BlueMask;
        return bits;
    }
    return 0; /* PixelBltOnly: there is no linear framebuffer to hand over */
}

static void fill_choice(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 index, VideoChoice *choice) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
    UINTN info_size = 0;

    choice->index = index;
    if (EFI_ERROR(gop->QueryMode(gop, index, &info_size, &info)) || info == 0) {
        return;
    }
    choice->width = info->HorizontalResolution;
    choice->height = info->VerticalResolution;
    choice->bpp = mode_layout(info, &choice->red_mask, &choice->green_mask, &choice->blue_mask);
    choice->pitch = info->PixelsPerScanLine * (choice->bpp / 8u);
}

/*
 * Pick the mode to run the desktop in.
 *
 * The requested size wins when the firmware has it.  Otherwise the smallest
 * mode at least as large as the request is taken, so a desktop laid out for the
 * requested canvas still fits inside the screen; if the firmware has nothing
 * that large, the largest mode it does have is used.  A mode whose colour
 * layout cannot be described to the kernel is never chosen.
 */
static int choose_mode(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 want_width, UINT32 want_height,
                       VideoChoice *choice) {
    UINT32 max_mode = gop->Mode->MaxMode;
    UINT32 index;
    int exact = -1;
    int at_least = -1;
    int closest = -1;
    UINT64 want_area = (UINT64)want_width * (UINT64)want_height;
    UINT32 red_active = 0;
    UINT32 green_active = 0;
    UINT32 blue_active = 0;
    UINT64 at_least_area = 0;
    UINT64 closest_area = 0;
    UINT32 queried = 0;
    UINT32 undescribed = 0;
    UINT32 empty = 0;

    for (index = 0; index < max_mode; ++index) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
        UINTN info_size = 0;
        UINT32 red = 0;
        UINT32 green = 0;
        UINT32 blue = 0;
        UINT64 area;

        if (EFI_ERROR(gop->QueryMode(gop, index, &info_size, &info)) || info == 0) {
            continue;
        }
        ++queried;
        if (mode_layout(info, &red, &green, &blue) == 0) {
            ++undescribed;
            continue;
        }
        if (info->HorizontalResolution == 0 || info->VerticalResolution == 0) {
            ++empty;
            continue;
        }
        area = (UINT64)info->HorizontalResolution * (UINT64)info->VerticalResolution;
        if (info->HorizontalResolution == want_width && info->VerticalResolution == want_height) {
            exact = (int)index;
            break;
        }
        if (area >= want_area) {
            if (at_least < 0 || area < at_least_area) {
                at_least = (int)index;
                at_least_area = area;
            }
        } else {
            UINT64 distance = want_area - area;

            if (closest < 0 || distance < closest_area) {
                closest = (int)index;
                closest_area = distance;
            }
        }
    }
    if (exact >= 0) {
        fill_choice(gop, (UINT32)exact, choice);
        return choice->bpp != 0;
    }
    if (at_least >= 0) {
        fill_choice(gop, (UINT32)at_least, choice);
        serial_puts("[EFI]: requested size not offered; using the smallest larger mode\n");
        return choice->bpp != 0;
    }
    if (closest >= 0) {
        fill_choice(gop, (UINT32)closest, choice);
        serial_puts("[EFI]: no mode as large as the request; using the largest available\n");
        return choice->bpp != 0;
    }

    /*
     * Nothing the firmware lists can be handed over.  Report what it offered
     * instead of only that the search failed, so a machine that reaches this
     * can be told apart from one whose firmware never answered.  One last
     * resort is taken first: the mode the firmware is running in right now,
     * which is reported even where QueryMode is not implemented - and a
     * machine that renders the firmware's own screen has a linear
     * framebuffer for it by definition.
     */
    if (gop->Mode != 0 && gop->Mode->Info != 0) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *active = gop->Mode->Info;
        UINT32 bpp = mode_layout(active, &red_active, &green_active, &blue_active);

        if (bpp != 0 && active->HorizontalResolution != 0 && active->VerticalResolution != 0) {
            choice->index = gop->Mode->Mode;
            choice->width = active->HorizontalResolution;
            choice->height = active->VerticalResolution;
            choice->bpp = bpp;
            choice->red_mask = red_active;
            choice->green_mask = green_active;
            choice->blue_mask = blue_active;
            choice->pitch = active->PixelsPerScanLine * (bpp / 8u);
            serial_puts("[EFI]: using the mode the firmware is already running in\n");
            return 1;
        }
    }

    serial_line_uint("modes the firmware offers", max_mode);
    serial_line_uint("modes QueryMode answered", queried);
    serial_line_uint("modes with no describable framebuffer", undescribed);
    serial_line_uint("modes with an empty size", empty);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Files on the boot volume.                                              */
/* ---------------------------------------------------------------------- */

typedef struct {
    UINT64 size;
    UINT64 file_size;
    UINT64 physical_size;
    UINT8 times[48];
    UINT64 attribute;
    CHAR16 name[1];
} EfiFileInfo;

typedef struct {
    UINT64 address;
    UINT64 size;
} LoadedFile;

static EFI_FILE_PROTOCOL *volume_root(void) {
    EFI_LOADED_IMAGE_PROTOCOL *image = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;

    if (EFI_ERROR(efi_bs->HandleProtocol(efi_image, (EFI_GUID *)&EFI_LOADED_IMAGE_PROTOCOL_GUID,
                                         (void **)&image)) ||
        image == 0) {
        panic("cannot find this loader's own loaded image");
    }
    if (EFI_ERROR(efi_bs->HandleProtocol(image->DeviceHandle,
                                         (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
                                         (void **)&fs)) ||
        fs == 0) {
        panic("the boot device has no Simple File System (not started from a volume)");
    }
    if (EFI_ERROR(fs->OpenVolume(fs, &root)) || root == 0) {
        panic("cannot open the root directory of the boot volume");
    }
    return root;
}

static EFI_STATUS load_file(EFI_FILE_PROTOCOL *root, const CHAR16 *path, UINTN memory_type,
                            LoadedFile *out) {
    EFI_FILE_PROTOCOL *file = 0;
    /* EFI_FILE_INFO carries the file name after its fixed fields, so the
     * buffer has to hold the longest name the volume can have - a struct-sized
     * buffer makes GetInfo answer EFI_BUFFER_TOO_SMALL instead of a size. */
    UINT8 info_buffer[1024];
    EfiFileInfo *file_info = (EfiFileInfo *)info_buffer;
    UINTN info_size = sizeof(info_buffer);
    EFI_STATUS status;
    UINT64 size;
    UINTN pages;

    out->address = 0;
    out->size = 0;

    status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || file == 0) {
        return status;
    }
    status = file->GetInfo(file, (EFI_GUID *)&EFI_FILE_INFO_GUID, &info_size, info_buffer);
    if (EFI_ERROR(status) || file_info->file_size == 0) {
        file->Close(file);
        return EFI_ERROR(status) ? status : EFI_ABORTED;
    }
    size = file_info->file_size;
    /* Rounded up by shifting, not dividing: a 64-bit divide compiles to a
     * libgcc helper routine, and neither firmware gets a libgcc to link. */
    pages = (UINTN)((size + 4095ull) >> 12);
    status = allocate_low(pages, memory_type, &out->address);
    if (EFI_ERROR(status)) {
        file->Close(file);
        return status;
    }
    {
        UINT8 *buffer = (UINT8 *)(UINTN)out->address;
        UINTN got = (UINTN)size;

        while (got > 0) {
            UINTN chunk = got;
            UINTN offset = (UINTN)size - got;

            status = file->Read(file, &chunk, buffer + offset);
            if (EFI_ERROR(status) || chunk == 0) {
                break;
            }
            got -= chunk;
        }
        if (got != 0) {
            file->Close(file);
            return EFI_ABORTED;
        }
    }
    file->Close(file);
    out->size = size;
    return EFI_SUCCESS;
}

/* ---------------------------------------------------------------------- */
/* SERIAL.LOG: the UEFI half of the debug capture.                        */
/* ---------------------------------------------------------------------- */

#define SERIAL_LOG_MAGIC 0x4C535848u /* 'HXSL' little endian */
#define SERIAL_LOG_STATE_UEFI 3u     /* mirrors serial_log.c's state dword */

static UINT32 le32(const UINT8 *bytes) {
    return (UINT32)bytes[0] | ((UINT32)bytes[1] << 8) |
           ((UINT32)bytes[2] << 16) | ((UINT32)bytes[3] << 24);
}

static void put_le32(UINT8 *bytes, UINT32 value) {
    bytes[0] = (UINT8)value;
    bytes[1] = (UINT8)(value >> 8);
    bytes[2] = (UINT8)(value >> 16);
    bytes[3] = (UINT8)(value >> 24);
}

/*
 * Harvest the PREVIOUS boot's kernel COM1 trace from the RAM ring.
 *
 * The kernel cannot write this medium on a UEFI boot, so it keeps its whole
 * trace in a fixed-address ring (the design mirrors the zstd loader's own
 * pre-kernel trace block, one boot later): this runs as early as this loader
 * can run, before any firmware allocation this loader makes could reuse the
 * pages, copies the text out, and retires the magic so a repeated call - or
 * a warm reset that never re-boots HaloxOS - can never harvest it twice.
 *
 * Both candidate addresses are tried, high first: low memory is the first
 * thing the next boot's firmware reuses during POST (measured - OVMF puts
 * DXE code over the low ring within one second of the reset), so a match
 * there is the fallback, never the second harvest.
 *
 * False starts are the common case and are silent by design: after a power
 * cycle the ring is garbage, after the first-ever boot it was never written,
 * and after a legacy boot the kernel wrote the medium itself and left no
 * ring.  The magic, the capacity and a sane counter have to all agree before
 * one byte is believed.
 */
static char kernel_prev_trace[KERNEL_RING_TEXT_BYTES];
static UINTN kernel_prev_bytes;
static int kernel_prev_stage;
static int kernel_prev_harvested;

static int kernel_ring_read_at(UINT32 ring_addr) {
    volatile UINT32 *header;
    const volatile char *text;
    UINT32 total;
    UINT32 index;
    UINT32 run;

    if (ring_addr == 0u || (UINT64)ring_addr > 0xFFFFFFFFull) {
        return 0;
    }
    header = (volatile UINT32 *)(UINTN)ring_addr;
    if (header[0] != KERNEL_RING_MAGIC || header[2] != KERNEL_RING_TEXT_BYTES) {
        return 0;
    }
    total = header[1];
    if (total == 0u || total > 0xFFFFFFFu) {
        /* Zero means the kernel never printed; absurd means stale RAM. */
        return 0;
    }
    kernel_prev_stage = (int)header[3];
    text = (const volatile char *)(UINTN)(ring_addr + KERNEL_RING_TEXT_OFFSET);
    kernel_prev_bytes = 0;
    if (total <= KERNEL_RING_TEXT_BYTES) {
        for (index = 0u; index < total && kernel_prev_bytes < sizeof(kernel_prev_trace);
             ++index) {
            kernel_prev_trace[kernel_prev_bytes++] = text[index];
        }
    } else {
        /* Wrapped: the oldest surviving byte is at total % capacity. */
        UINT32 head = total % KERNEL_RING_TEXT_BYTES;

        run = KERNEL_RING_TEXT_BYTES - head;
        for (index = 0u; index < run && kernel_prev_bytes < sizeof(kernel_prev_trace);
             ++index) {
            kernel_prev_trace[kernel_prev_bytes++] = text[head + index];
        }
        for (index = 0u; index < head && kernel_prev_bytes < sizeof(kernel_prev_trace);
             ++index) {
            kernel_prev_trace[kernel_prev_bytes++] = text[index];
        }
    }
    header[0] = 0u; /* retire the magic: harvested once is enough */
    return 1;
}

static void kernel_trace_harvest(void) {
    if (kernel_prev_harvested) {
        return;
    }
    kernel_prev_harvested = 1;
    if (kernel_ring_read_at(KERNEL_RING_HIGH) ||
        kernel_ring_read_at(KERNEL_RING_LOW)) {
        serial_puts("[EFI]: previous boot kernel trace recovered from RAM: ");
        serial_uint((UINT32)kernel_prev_bytes);
        serial_puts(" bytes, boot stage ");
        serial_uint((UINT32)kernel_prev_stage);
        serial_puts("\n");
    }
}

/*
 * Put this boot's trace on the boot medium.
 *
 * This is the UEFI answer to the kernel's own serial.log capture, and it
 * exists because the two halves of a UEFI boot can reach different things: the
 * kernel can reach its COM1 trace but not the disk (no INT 13h, no USB
 * mass-storage driver, and firmware runtime services that a 32-bit kernel
 * cannot call on a 64-bit boot), while this loader has the boot volume open
 * through the Simple File System protocol right up to ExitBootServices.
 * Whatever the loader can say, therefore, has to be said now or never.
 *
 * The medium keeps its reserved slot.  The 512-byte header is rewritten in
 * place with only its diagnostic half changed, so a later legacy (CSM) boot of
 * the same stick still finds the 'HXSL' magic, the slot height and the FAT
 * directory entry location exactly where the build put them - and the loader's
 * text, from the second sector on, is simply replaced by the kernel's.
 *
 * The file is opened read/write and never created: a volume without the
 * reservation gets a note on COM1 instead of a file the kernel would not
 * recognize as its slot.
 */
static void write_serial_log(EFI_FILE_PROTOCOL *root) {
    EFI_FILE_PROTOCOL *file = 0;
    UINT8 header[512];
    UINTN header_bytes = sizeof(header);
    /* Static: the harvested kernel trace makes this far bigger than a
     * firmware stack should be asked to hold in one frame. */
    static UINT8 body[EFI_TRACE_MAX + KERNEL_RING_TEXT_BYTES + 2048u];
    UINTN body_bytes = 0;
    UINTN room = 0; /* text bytes the slot can hold, 0 = none known */
    UINT32 previous = 0; /* text bytes the slot held before this capture */
    UINTN index;
    EFI_STATUS status;
    static const char note[] =
        "HaloxOS UEFI boot: this file was written by the EFI loader.\r\n"
        "The kernel cannot write this medium on a UEFI boot - it has no BIOS\r\n"
        "disk services, no USB mass-storage driver, and the firmware's own\r\n"
        "runtime services are 64-bit code a 32-bit kernel cannot call - so the\r\n"
        "kernel's COM1 trace is kept in RAM and the NEXT boot's EFI loader\r\n"
        "writes it here, under the 'previous boot' banner below.  RAM survives\r\n"
        "a warm reset (reset button, Ctrl-Alt-Del, crash) but not a power\r\n"
        "cycle; the kernel's own live capture returns when the same medium\r\n"
        "boots through legacy (CSM/BIOS) mode.\r\n"
        "\r\n";

    status = root->Open(root, &file, L"\\uefi-serial.log",
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(status) || file == 0) {
        /* An older medium carries one shared SERIAL.LOG instead of the split
         * pair; it is still this half's home there. */
        status = root->Open(root, &file, L"\\SERIAL.LOG",
                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
        if (EFI_ERROR(status) || file == 0) {
            serial_puts("[EFI]: no \\uefi-serial.log slot on the boot volume, "
                        "capture skipped\n");
            return;
        }
        serial_puts("[EFI]: no \\uefi-serial.log slot; using the shared \\SERIAL.LOG\n");
    }
    if (!EFI_ERROR(file->Read(file, &header_bytes, header)) && header_bytes == 512) {
        if (le32(header) == SERIAL_LOG_MAGIC) {
            UINT32 sectors = le32(header + 12);

            if (sectors >= 2u && sectors <= 0x20000u) {
                room = (UINTN)sectors * 512u - 512u;
            }
            previous = le32(header + 44u);
        }
    }

    for (index = 0; note[index] != '\0' && body_bytes < sizeof(body); ++index) {
        body[body_bytes++] = (UINT8)note[index];
    }
    /* The previous boot's kernel trace first: it is the half of the capture
     * the kernel could never write itself, and the reason this file exists
     * on a UEFI machine at all.  An empty harvest (cold boot, legacy boot)
     * simply leaves it out. */
    if (kernel_prev_bytes != 0u) {
        static const char kernel_banner[] =
            "=== previous boot: kernel COM1 trace, recovered from RAM ===\r\n";
        static const char kernel_sep[] =
            "\r\n=== this boot: EFI loader trace ===\r\n";

        for (index = 0; kernel_banner[index] != '\0' && body_bytes < sizeof(body);
             ++index) {
            body[body_bytes++] = (UINT8)kernel_banner[index];
        }
        for (index = 0; index < kernel_prev_bytes && body_bytes < sizeof(body);
             ++index) {
            body[body_bytes++] = (UINT8)kernel_prev_trace[index];
        }
        for (index = 0; kernel_sep[index] != '\0' && body_bytes < sizeof(body);
             ++index) {
            body[body_bytes++] = (UINT8)kernel_sep[index];
        }
    }
    for (index = 0; index < efi_trace_bytes && body_bytes < sizeof(body); ++index) {
        body[body_bytes++] = (UINT8)efi_trace[index];
    }
    /* Never run past the reserved slot: the sectors after it belong to other
     * files, and the kernel's own capture would find them rewritten. */
    if (room != 0u && body_bytes > room) {
        body_bytes = room;
    }

    if (room != 0u) {
        /* Keep the slot's identifying fields, replace its diagnostic half with
         * the truth about this boot, so the first thing a Notepad view shows
         * is how much of the capture is here and where the rest went. */
        static const char summary[] =
            "capture=UEFI+kernel-RAM  written by the EFI loader;\r\n"
            "kernel trace from the previous boot; live kernel capture needs CSM.\r\n";

        for (index = 64u; index < 224u && index < sizeof(header); ++index) {
            header[index] = 0;
        }
        for (index = 0; summary[index] != '\0' && 64u + index < 224u; ++index) {
            header[64u + index] = (UINT8)summary[index];
        }
        put_le32(header + 44u, (UINT32)body_bytes); /* stored text bytes */
        put_le32(header + 16u, 0u);                /* no BIOS drive on UEFI */
        put_le32(header + 56u, SERIAL_LOG_STATE_UEFI);
        status = file->SetPosition(file, 0);
        if (!EFI_ERROR(status)) {
            UINTN written = sizeof(header);

            (void)file->Write(file, &written, header);
        }
    }

    status = file->SetPosition(file, 512);
    if (!EFI_ERROR(status)) {
        UINTN written = body_bytes;

        status = file->Write(file, &written, body);
        if (EFI_ERROR(status)) {
            serial_line_hex("SERIAL.LOG write status", (UINT64)status);
        }
    }

    /*
     * Writing inside a file does not shorten it: a capture longer than this
     * boot's stays on the end of the file, and an older boot's note and trace
     * then look like a second copy of this boot's own - which is what a
     * Notepad view of a re-flashed stick shows.  The older tail is blanked in
     * place rather than resized away, because the file's cluster chain is the
     * very region the kernel writes its own trace into: a resize would free
     * those clusters and leave the kernel's capture in space the file no
     * longer covers.
     */
    {
        if (room != 0u && (UINTN)previous > body_bytes) {
            UINT8 blank[256];
            UINTN left = (UINTN)previous - body_bytes;
            UINT64 offset = 512ull + (UINT64)body_bytes;

            if (left > room - body_bytes) {
                left = room - body_bytes;
            }
            for (index = 0; index < sizeof(blank); ++index) {
                blank[index] = ' ';
            }
            while (left > 0) {
                UINTN chunk = left < sizeof(blank) ? left : sizeof(blank);
                UINTN written = chunk;

                if (EFI_ERROR(file->SetPosition(file, offset)) ||
                    EFI_ERROR(file->Write(file, &written, blank)) || written == 0) {
                    break;
                }
                offset += written;
                left -= written;
            }
        }
    }
    file->Close(file);
    serial_puts("[EFI]: SERIAL.LOG capture: ");
    serial_uint((UINT32)body_bytes);
    serial_puts(" bytes stored in uefi-serial.log\n");
}

/* Called at each boot milestone and from panic(), so a UEFI boot that stops
 * part way still leaves behind how far it got.  write_serial_log rewrites the
 * slot in place, so the newest call is what a Notepad view shows. */
static void capture_uefi_trace(void) {
    if (boot_root != 0) {
        write_serial_log(boot_root);
    }
}

/* Place loader.elf where its ELF program headers say it belongs. */
static void place_loader(EFI_FILE_PROTOCOL *root, UINT32 *entry_out) {
    LoadedFile elf;
    Elf32Header *header;
    UINT32 index;

    {
        EFI_STATUS status = load_file(root, L"\\boot\\loader.elf", EfiLoaderData, &elf);

        if (EFI_ERROR(status)) {
            serial_line_hex("loader.elf read status", (UINT64)status);
            panic("\\boot\\loader.elf could not be read from the boot volume");
        }
    }
    header = (Elf32Header *)(UINTN)elf.address;
    if (header->ident[0] != 0x7F || header->ident[1] != 'E' || header->ident[2] != 'L' ||
        header->ident[3] != 'F') {
        panic("\\boot\\loader.elf is not an ELF image");
    }
    if (header->ident[4] != 1 || header->machine != 3) {
        panic("\\boot\\loader.elf is not a 32-bit x86 image");
    }
    if (header->phnum == 0 || header->phnum > ELF_PHDR_MAX || header->phentsize != 32) {
        panic("\\boot\\loader.elf has an unusable program header table");
    }
    for (index = 0; index < header->phnum; ++index) {
        Elf32ProgramHeader *ph =
            (Elf32ProgramHeader *)(UINTN)(elf.address + header->phoff + (UINT64)index * 32ull);
        UINT64 end;

        if (ph->type != 1 /* PT_LOAD */ || ph->memsz == 0) {
            continue;
        }
        end = (UINT64)ph->paddr + ph->memsz;
        if (end > 0x100000000ull || ph->filesz > ph->memsz) {
            panic("\\boot\\loader.elf has a program segment outside the 32-bit address space");
        }
        if (ph->paddr < LOADER_LOAD_ADDR || end > LOADER_LOAD_ADDR + 0x100000ull) {
            panic("\\boot\\loader.elf is linked outside its reserved megabyte");
        }
        memcpy((void *)(UINTN)ph->paddr, (const void *)(UINTN)(elf.address + ph->offset),
               ph->filesz);
        if (ph->memsz > ph->filesz) {
            memset((void *)(UINTN)(ph->paddr + ph->filesz), 0, ph->memsz - ph->filesz);
        }
    }
    *entry_out = header->entry;
    serial_line_hex("loader entry", header->entry);
}

/* ---------------------------------------------------------------------- */
/* Display size menu, the UEFI counterpart of the BIOS boot menu.          */
/* ---------------------------------------------------------------------- */

typedef struct {
    UINT32 width;
    UINT32 height;
} SizeEntry;

static SizeEntry size_menu[MENU_ENTRY_MAX];
static UINT32 size_menu_count;
static UINT32 size_default_index;
static UINT32 menu_timeout = HALOXOS_UEFI_DEFAULT_TIMEOUT;

static void size_add(UINT32 width, UINT32 height) {
    UINT32 index;

    if (width == 0 || height == 0 || size_menu_count >= MENU_ENTRY_MAX) {
        return;
    }
    for (index = 0; index < size_menu_count; ++index) {
        if (size_menu[index].width == width && size_menu[index].height == height) {
            return;
        }
    }
    size_menu[size_menu_count].width = width;
    size_menu[size_menu_count].height = height;
    ++size_menu_count;
}

static UINT32 parse_uint_ascii(const char **cursor) {
    UINT32 value = 0;

    while (**cursor >= '0' && **cursor <= '9') {
        value = value * 10u + (UINT32)(**cursor - '0');
        ++*cursor;
    }
    return value;
}

static void read_size_line(char *line) {
    const char *cursor = line;

    if (line[0] == '#' || line[0] == '\0') {
        return;
    }
    if (line[0] == 't' && line[1] == 'i') { /* timeout=SECONDS */
        while (*cursor != '=' && *cursor != '\0') {
            ++cursor;
        }
        if (*cursor == '=') {
            ++cursor;
            menu_timeout = parse_uint_ascii(&cursor);
            if (menu_timeout > 30u) {
                menu_timeout = 30u;
            }
        }
        return;
    }
    if ((line[0] == 's' && line[1] == 'i') || (line[0] == 'd' && line[1] == 'e')) {
        UINT32 width;
        UINT32 height;

        while (*cursor != '=' && *cursor != '\0') {
            ++cursor;
        }
        if (*cursor != '=') {
            return;
        }
        ++cursor;
        width = parse_uint_ascii(&cursor);
        if (*cursor != 'x') {
            return;
        }
        ++cursor;
        height = parse_uint_ascii(&cursor);
        if (width == 0 || height == 0) {
            return;
        }
        size_add(width, height);
        if (line[0] == 'd') {
            size_default_index = size_menu_count - 1;
        }
    }
}

/* \boot\uefi.cfg, if present, replaces the built-in size with the menu the
 * Makefile generates: the configured geometry first, then the display sizes the
 * boot menu offers, and the size booted when nothing is pressed. */
static void read_size_config(EFI_FILE_PROTOCOL *root) {
    EFI_FILE_PROTOCOL *file = 0;
    UINT8 buffer[1024];
    UINTN size = sizeof(buffer) - 1;
    UINTN index;
    UINTN line_length = 0;
    char line[64];

    size_menu_count = 0;
    size_default_index = 0;
    size_add(HALOXOS_UEFI_DEFAULT_WIDTH, HALOXOS_UEFI_DEFAULT_HEIGHT);

    if (EFI_ERROR(root->Open(root, &file, L"\\boot\\uefi.cfg", EFI_FILE_MODE_READ, 0)) ||
        file == 0) {
        serial_line("display size menu", "no \\boot\\uefi.cfg; using the built-in size");
        return;
    }
    if (EFI_ERROR(file->Read(file, &size, buffer))) {
        file->Close(file);
        return;
    }
    file->Close(file);

    size_menu_count = 0;
    size_default_index = 0;
    for (index = 0; index < size; ++index) {
        char ch = (char)buffer[index];

        if (ch == '\r' || ch == '\n') {
            line[line_length] = '\0';
            if (line_length != 0) {
                read_size_line(line);
            }
            line_length = 0;
            continue;
        }
        if (line_length + 1 < sizeof(line)) {
            line[line_length++] = ch;
        }
    }
    if (size_menu_count == 0) {
        size_add(HALOXOS_UEFI_DEFAULT_WIDTH, HALOXOS_UEFI_DEFAULT_HEIGHT);
    }
    if (size_default_index >= size_menu_count) {
        size_default_index = 0;
    }
    serial_line_uint("menu sizes", size_menu_count);
    serial_line_uint("default entry", size_default_index + 1);
}

static CHAR16 *append_text(CHAR16 *cursor, const CHAR16 *text) {
    while (*text != 0) {
        *cursor++ = *text++;
    }
    return cursor;
}

static CHAR16 *append_uint(CHAR16 *cursor, UINT32 value) {
    CHAR16 digits[11];
    int length = 0;

    if (value == 0) {
        *cursor++ = '0';
        return cursor;
    }
    while (value != 0 && length < 10) {
        digits[length++] = (CHAR16)('0' + (value % 10u));
        value /= 10u;
    }
    while (length > 0) {
        *cursor++ = digits[--length];
    }
    return cursor;
}

static void draw_size_menu(void) {
    UINT32 index;
    CHAR16 line[64];
    CHAR16 *cursor;

    console_line(L"HaloxOS! (UEFI video)");
    console_line(L"Choose the desktop display size:");
    console_line(L"");
    for (index = 0; index < size_menu_count && index < MENU_ENTRY_MAX; ++index) {
        cursor = line;
        *cursor++ = ' ';
        *cursor++ = (CHAR16)('1' + index);
        *cursor++ = ')';
        *cursor++ = ' ';
        cursor = append_uint(cursor, size_menu[index].width);
        *cursor++ = 'x';
        cursor = append_uint(cursor, size_menu[index].height);
        if (index == size_default_index) {
            cursor = append_text(cursor, L"   (default)");
        }
        *cursor = 0;
        console_line(line);
    }
    console_line(L"");
    console_line(L"Press 1-9 to pick a size, or Enter to boot the default now.");
}

/* Count down, watching the keyboard: a digit chooses a size immediately, Enter
 * takes the default, and hitting zero seconds boots the default. */
static void run_size_menu(UINT32 *chosen_index) {
    UINT32 seconds_left = menu_timeout;
    EFI_INPUT_KEY key;

    draw_size_menu();
    while (seconds_left > 0) {
        UINT32 tick;

        for (tick = 0; tick < 10; ++tick) {
            if (efi_st->ConIn != 0 && efi_st->ConIn->ReadKeyStroke != 0 &&
                !EFI_ERROR(efi_st->ConIn->ReadKeyStroke(efi_st->ConIn, &key))) {
                CHAR16 ch = key.UnicodeChar;

                if (ch >= '1' && ch <= (CHAR16)('0' + size_menu_count)) {
                    *chosen_index = (UINT32)(ch - '1');
                    return;
                }
                if (ch == '\r' || ch == '\n') {
                    *chosen_index = size_default_index;
                    return;
                }
            }
            efi_bs->Stall(100000u);
        }
        --seconds_left;
    }
    *chosen_index = size_default_index;
}

/* ---------------------------------------------------------------------- */
/* Multiboot information block.                                           */
/* ---------------------------------------------------------------------- */

static MultibootMmapEntry *mmap_entry(UINT32 index) {
    return (MultibootMmapEntry *)(void *)(mbi_block + MBI_OFF_MMAP +
                                          (UINT64)index * sizeof(MultibootMmapEntry));
}

static void mmap_add(UINT32 base, UINT32 length, UINT32 type) {
    MultibootMmapEntry *entry;

    if (length == 0 || mmap_entries_written >= MBI_MMAP_MAX) {
        return;
    }
    entry = mmap_entry(mmap_entries_written++);
    entry->size = 20;
    entry->base_low = base;
    entry->base_high = 0;
    entry->length_low = length;
    entry->length_high = 0;
    entry->type = type;
}

/* Emit [base, base+length) with this loader's own reserved ranges cut out, so
 * the kernel is never told that the handoff block, the module buffers or the
 * loader's megabyte are free memory. */
static void mmap_emit_available(UINT32 base, UINT32 length, UINT32 type) {
    UINT32 end = base + length;
    UINT32 cursor = base;

    while (cursor < end) {
        UINT32 next = end;
        UINT32 index;
        int moved = 1;

        while (moved) {
            moved = 0;
            for (index = 0; index < reserved_count; ++index) {
                UINT64 start = reserved[index].base;
                UINT64 stop = reserved[index].base + reserved[index].length;

                if (start <= (UINT64)cursor && stop > (UINT64)cursor) {
                    if (stop >= (UINT64)end) {
                        return;
                    }
                    cursor = (UINT32)stop;
                    moved = 1;
                    break;
                }
            }
        }
        for (index = 0; index < reserved_count; ++index) {
            UINT64 start = reserved[index].base;

            if (start > (UINT64)cursor && start < (UINT64)next) {
                next = (UINT32)start;
            }
        }
        mmap_add(cursor, next - cursor, type);
        cursor = next;
    }
}

/* Firmware memory type -> multiboot memory type.  Only memory genuinely free
 * after ExitBootServices is offered as available; firmware runtime code, ACPI
 * tables and MMIO stay reserved. */
static void mmap_emit_descriptor(UINT64 base, UINT64 length, UINT32 efi_type) {
    const UINT64 limit = 0x100000000ull;
    UINT32 type;

    /*
     * EfiLoaderCode and EfiLoaderData are deliberately absent from the
     * available list, and that is what keeps the kernel's interrupt gates
     * working.  The kernel runs on the descriptor table installed by the handoff
     * stub - it is in this image's .data, on both firmware architectures - and
     * names its gate selector from the live CS.  The firmware counts those pages
     * as loader memory, which is free for reuse the moment boot services are
     * gone, so offering them to the kernel lets its own allocations eat the
     * table: the first interrupt then loads CS through a gate naming zeroes and
     * the machine double faults and resets.
     *
     * Where the image lands decides whether it bites.  BOOTX64.EFI links at
     * 0x140000000, above anything a 32-bit kernel can address, so it survived by
     * accident; BOOTIA32.EFI links at 4 MiB, in the middle of the RAM the kernel
     * is handed, and reset-looped every few seconds until these two cases were
     * taken out.  They fall to the default arm and stay reserved, which costs
     * the size of the boot images rather than any memory that matters.
     */
    switch (efi_type) {
        case EfiConventionalMemory:
        case EfiBootServicesCode:
        case EfiBootServicesData:
            type = 1;
            break;
        case EfiACPIReclaimMemory:
            type = 3;
            break;
        case EfiACPIMemoryNVS:
            type = 4;
            break;
        case EfiUnusableMemory:
            type = 5;
            break;
        default:
            type = 2;
            break;
    }
    if (base >= limit) {
        return;
    }
    if (base + length > limit) {
        length = limit - base;
    }
    if (length == 0) {
        return;
    }
    if (type == 1) {
        mmap_emit_available((UINT32)base, (UINT32)length, type);
    } else {
        mmap_add((UINT32)base, (UINT32)length, type);
    }
}

/* The conventional/extended memory figures, the way GRUB reports them: the
 * loader reads mem_lower/mem_upper first and only then the map. */
static void measure_memory(void) {
    UINT32 index;
    UINT64 low_end = 0;
    UINT64 high_end = 0x100000ull;

    mem_lower_kb = 0;
    mem_upper_kb = 0;
    for (index = 0; index < mmap_entries_written; ++index) {
        MultibootMmapEntry *entry = mmap_entry(index);
        UINT64 base = entry->base_low;
        UINT64 end = base + entry->length_low;

        if (entry->type != 1u) {
            continue;
        }
        if (base == 0 && end > low_end) {
            low_end = end > 0xA0000ull ? 0xA0000ull : end;
        }
        if (end > high_end) {
            high_end = end;
        }
    }
    /* Shifts rather than divides, for the same reason as the page count above:
     * 64-bit division would drag in a helper the freestanding image lacks. */
    mem_lower_kb = (UINT32)(low_end >> 10);
    mem_upper_kb = (UINT32)((high_end - 0x100000ull) >> 10);
}

static UINT32 build_cmdline(UINT32 width, UINT32 height, UINT64 rsdp) {
    char text[MBI_CMDLINE_MAX];
    UINTN index = 0;
    UINTN i;
    static const char platform[] = "haloxos_platform=efi haloxos_size=";
    static const char hex_digits[] = "0123456789abcdef";

    for (i = 0; platform[i] != '\0' && index + 1 < sizeof(text); ++i) {
        text[index++] = platform[i];
    }
    {
        char digits[11];
        int length = 0;
        UINT32 value = width;

        while (value != 0 && length < 10) {
            digits[length++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        while (length > 0 && index + 1 < sizeof(text)) {
            text[index++] = digits[--length];
        }
        if (index + 1 < sizeof(text)) {
            text[index++] = 'x';
        }
        value = height;
        length = 0;
        while (value != 0 && length < 10) {
            digits[length++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        while (length > 0 && index + 1 < sizeof(text)) {
            text[index++] = digits[--length];
        }
    }
    if (rsdp != 0) {
        static const char prefix[] = " haloxos_rsdp=0x";
        int shift;

        for (i = 0; prefix[i] != '\0' && index + 1 < sizeof(text); ++i) {
            text[index++] = prefix[i];
        }
        for (shift = 28; shift >= 0; shift -= 4) {
            if (index + 1 < sizeof(text)) {
                text[index++] = hex_digits[(rsdp >> shift) & 0xFull];
            }
        }
    }
    text[index] = '\0';
    memcpy(mbi_block + MBI_OFF_CMDLINE, text, index + 1);
    return (UINT32)(UINTN)(mbi_block + MBI_OFF_CMDLINE);
}

/* ---------------------------------------------------------------------- */
/* ACPI: the RSDP, which UEFI publishes in the configuration table.        */
/* ---------------------------------------------------------------------- */

static UINT64 find_rsdp(void) {
    UINTN index;

    if (efi_st->ConfigurationTable == 0) {
        return 0;
    }
    for (index = 0; index < efi_st->NumberOfTableEntries; ++index) {
        EFI_CONFIGURATION_TABLE *table = &efi_st->ConfigurationTable[index];

        if (guid_equal(&table->VendorGuid, (EFI_GUID *)&EFI_ACPI_20_TABLE_GUID) ||
            guid_equal(&table->VendorGuid, (EFI_GUID *)&EFI_ACPI_TABLE_GUID)) {
            if (table->VendorTable != 0) {
                return (UINT64)(UINTN)table->VendorTable;
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Leaving boot services.                                                 */
/* ---------------------------------------------------------------------- */

/*
 * How far the handoff got, drawn where a machine that dies silently can still
 * be read.
 *
 * Everything this loader can say on the screen is said through the firmware's
 * own console, and that channel ends at ExitBootServices - which is exactly
 * where a machine with a modern firmware and no legacy VGA is most likely to
 * stop.  From there the only thing still on screen is the framebuffer the
 * firmware handed over, so the progress is reported the one way that needs no
 * firmware at all: one block of the strip below is filled in as each step
 * completes, and the strip stays painted if the step after it never finishes.
 * The number of filled blocks is then the number of completed steps, and a
 * photograph of a frozen screen says which one it was.
 *
 * The strip is drawn as a pale bar with dark blocks so that it shows against
 * any console background, and it is sized from the mode actually chosen - a
 * mode too small for it is simply not marked, because a machine that can draw
 * the desktop can draw this.
 */
static void progress_step(const VideoChoice *video, UINT32 index, UINT32 count) {
    const UINT32 bytes = (UINT32)((video->bpp + 7u) / 8u);
    const UINT32 block = 18u;
    const UINT32 gap = 6u;
    UINT32 bar_width;
    UINT32 bar_left;
    UINT32 bar_top;
    UINT32 x;
    UINT32 y;
    volatile UINT8 *framebuffer;

    if (video->framebuffer == 0 || video->framebuffer > 0xFFFFFFFFull) {
        return;
    }
    if (video->bpp != 15 && video->bpp != 16 && video->bpp != 24 && video->bpp != 32) {
        return;
    }
    bar_width = count * block + (count - 1u) * gap;
    if (video->width < bar_width + 32u || video->height < 96u) {
        return;
    }
    bar_left = (video->width - bar_width) / 2u;
    bar_top = video->height - block - 24u;
    framebuffer = (volatile UINT8 *)(UINTN)(UINT32)video->framebuffer;

    /* The bar first, so the step that is being marked now shows as a dark
     * block on it even if the marking is the last thing that ever runs. */
    for (y = bar_top; y < bar_top + block; ++y) {
        for (x = bar_left; x < bar_left + bar_width; ++x) {
            volatile UINT8 *pixel = framebuffer + (UINTN)y * video->pitch +
                                    (UINTN)x * bytes;

            if (video->bpp == 15) {
                pixel[0] = 0xC6; pixel[1] = 0x7F;
            } else if (video->bpp == 16) {
                pixel[0] = 0xDF; pixel[1] = 0xFF;
            } else {
                pixel[0] = 0xE8; pixel[1] = 0xE8; pixel[2] = 0xE8;
                if (video->bpp == 32) {
                    pixel[3] = 0x00;
                }
            }
        }
    }
    for (y = bar_top + 2u; y < bar_top + block - 2u; ++y) {
        for (x = bar_left + index * (block + gap) + 2u;
             x < bar_left + index * (block + gap) + block - 2u; ++x) {
            volatile UINT8 *pixel = framebuffer + (UINTN)y * video->pitch +
                                    (UINTN)x * bytes;

            pixel[0] = 0x18;
            pixel[1] = 0x18;
            if (video->bpp != 15 && video->bpp != 16) {
                pixel[2] = 0x18;
                if (video->bpp == 32) {
                    pixel[3] = 0x00;
                }
            } else {
                pixel[1] = (UINT8)(video->bpp == 15 ? 0x0C : 0x08);
            }
        }
    }
}

/* The steps progress_step is called with, in order: what each mark means is
 * only useful next to the list. */
#define PROGRESS_STEPS 4u

static EFI_STATUS exit_boot_services(void) {
    UINT32 attempt;

    for (attempt = 0; attempt < 4; ++attempt) {
        UINTN map_size = MBI_DESC_BYTES;
        UINTN map_key = 0;
        UINTN descriptor_size = 0;
        UINT32 descriptor_version = 0;
        UINT32 offset;
        EFI_STATUS status;

        mmap_entries_written = 0;
        status = efi_bs->GetMemoryMap(
            &map_size, (EFI_MEMORY_DESCRIPTOR *)(void *)(mbi_block + MBI_OFF_DESCRIPTORS),
            &map_key, &descriptor_size, &descriptor_version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            serial_line_uint("memory map needs bytes", (UINT32)map_size);
            panic("the handoff block is too small for this machine's memory map");
        }
        if (EFI_ERROR(status) || descriptor_size == 0) {
            serial_line_hex("GetMemoryMap status", status);
            return status;
        }
        for (offset = 0; offset + (UINT32)descriptor_size <= (UINT32)map_size;
             offset += (UINT32)descriptor_size) {
            EFI_MEMORY_DESCRIPTOR *descriptor =
                (EFI_MEMORY_DESCRIPTOR *)(void *)(mbi_block + MBI_OFF_DESCRIPTORS + offset);

            mmap_emit_descriptor(descriptor->PhysicalStart,
                                 descriptor->NumberOfPages * 4096ull, descriptor->Type);
        }
        serial_line_uint("memory map entries", mmap_entries_written);

        status = efi_bs->ExitBootServices(efi_image, map_key);
        if (!EFI_ERROR(status)) {
            return EFI_SUCCESS;
        }
        serial_line_hex("ExitBootServices retry, status", status);
    }
    return EFI_ABORTED;
}

/* ---------------------------------------------------------------------- */

EFIAPI UINTN efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table);

EFIAPI UINTN efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_FILE_PROTOCOL *root;
    VideoChoice choice;
    LoadedFile kernel;
    LoadedFile wad;
    ModuleHeader kernel_header;
    UINT32 loader_entry = 0;
    UINT32 module_count = 0;
    UINT32 chosen = 0;
    UINT64 rsdp;
    UINT64 handoff_page = 0;
    MultibootInfo *info;
    MultibootModule *module_table;
    int have_wad = 0;
    int have_video = 0;

    memset(&choice, 0, sizeof(choice));
    memset(&kernel, 0, sizeof(kernel));
    memset(&wad, 0, sizeof(wad));

    efi_image = image;
    efi_st = system_table;
    if (system_table == 0 || system_table->BootServices == 0) {
        halt();
    }
    efi_bs = system_table->BootServices;
    console_alive = (system_table->ConOut != 0 && system_table->ConOut->OutputString != 0);
    reserved_count = 0;
    workset_count = 0;
    mmap_entries_written = 0;
    workset_setup();

    serial_init();
    serial_puts("\n[EFI]: HaloxOS UEFI boot loader 1.0\n");
    /* Harvest the previous boot's kernel COM1 trace before anything this
     * loader does can reuse the pages it lives on: the kernel parked it in
     * the handoff area at the end of the previous (warm-reset) boot, and
     * this is the only moment it is guaranteed intact. */
    kernel_trace_harvest();
    if (system_table->FirmwareVendor != 0) {
        serial_line_wide("firmware", system_table->FirmwareVendor);
    }
    serial_puts("[EFI]: boot source: ");
    serial_puts(console_alive ? "firmware console, serial trace\n" : "serial only\n");

    if (efi_bs->SetWatchdogTimer != 0) {
        efi_bs->SetWatchdogTimer(0, 0, 0, 0);
    }
    if (console_alive && efi_st->ConOut->SetAttribute != 0) {
        efi_st->ConOut->SetAttribute(efi_st->ConOut, 0x07);
    }

    root = volume_root();
    boot_root = root;
    read_size_config(root);
    /* First milestone: the volume is open, so even a boot that dies in the
     * video setup below now leaves its trace on the medium. */
    capture_uefi_trace();
    if (console_alive && efi_st->ConOut->ClearScreen != 0) {
        efi_st->ConOut->ClearScreen(efi_st->ConOut);
    }
    run_size_menu(&chosen);
    serial_line_uint("chosen size entry", chosen + 1);
    /* Milestone: something the firmware does while setting a video mode can
     * hang a machine, and then this is the last thing it ever said. */
    capture_uefi_trace();

    /* Video mode: the UEFI replacement for programming a VBE mode. */
    /* The Graphics Output Protocol belongs to the graphics controller, not to
     * this loader's own image handle, so it is located by protocol GUID. */
    if (EFI_ERROR(efi_bs->LocateProtocol((EFI_GUID *)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, 0,
                                         (void **)&gop)) ||
        gop == 0) {
        serial_puts("[EFI]: this firmware offers no Graphics Output Protocol\n");
        gop = 0;
    }
    have_video = (gop != 0) && choose_mode(gop, size_menu[chosen].width,
                                           size_menu[chosen].height, &choice);
    if (!have_video) {
        /*
         * A firmware can describe its modes and still have no linear
         * framebuffer for any of them - the blit-only case - and both this
         * loader and the kernel draw straight into such a framebuffer, so
         * there is nothing to hand over and the boot stops here.  The counts
         * above name which of the two reasons it was, because they are the
         * difference between a machine that cannot run the desktop and a
         * machine this loader could not understand.
         */
        panic("this firmware has no linear framebuffer the desktop can draw into");
    }
    if (EFI_ERROR(gop->SetMode(gop, choice.index))) {
        serial_line_hex("SetMode failed for mode", choice.index);
        panic("the firmware refused to set the chosen video mode");
    }
    choice.framebuffer = gop->Mode->FrameBufferBase;
    serial_line_uint("video width", choice.width);
    serial_line_uint("video height", choice.height);
    serial_line_uint("video bpp", choice.bpp);
    serial_line_hex("framebuffer", choice.framebuffer);
    serial_line_uint("framebuffer pitch", choice.pitch);
    if (choice.framebuffer == 0 || choice.framebuffer > 0xFFFFFFFFull) {
        panic("the chosen video mode has no 32-bit linear framebuffer address");
    }
    range_note(reserved, &reserved_count, RESERVED_MAX, choice.framebuffer,
               (UINT64)choice.pitch * choice.height);
    /* Milestone: video mode chosen, so the trace names the mode even if the
     * medium has no readable loader.elf on it. */
    capture_uefi_trace();

    /* Reserve the loader's link address before anything else can be handed out
     * there, then place the loader exactly where it was linked. */
    reserve_fixed(LOADER_LOAD_ADDR, LOADER_LOAD_PAGES,
                  "the loader's 1 MiB load address is not free memory");
    place_loader(root, &loader_entry);
    /* Second milestone: firmware, video choice and loader address are known. */
    capture_uefi_trace();

    /* The handoff block, the kernel module and the WAD module. */
    if (EFI_ERROR(allocate_low(MBI_BLOCK_PAGES, EfiLoaderData, (UINT64 *)&mbi_block))) {
        panic("cannot allocate the multiboot information block");
    }
    memset(mbi_block, 0, MBI_BLOCK_PAGES * 4096u);

    /* The x64 handoff has to execute after its code segment has become a
     * 32-bit compatibility segment.  Firmware is allowed to load BOOTX64.EFI
     * above 4 GiB, where that segment cannot fetch it, so reserve one low page
     * for the small position-independent transition trampoline now. */
    if (EFI_ERROR(allocate_low(1u, EfiLoaderData, &handoff_page))) {
        panic("cannot allocate the low-memory UEFI handoff trampoline");
    }
    serial_line_hex("UEFI handoff trampoline", handoff_page);

    if (EFI_ERROR(load_file(root, L"\\boot\\kernel.bin.zst", EfiLoaderData, &kernel))) {
        panic("\\boot\\kernel.bin.zst could not be read from the boot volume");
    }
    kernel_header = *(const ModuleHeader *)(UINTN)kernel.address;
    serial_line_hex("kernel module at", kernel.address);
    serial_line_uint("kernel module bytes", (UINT32)kernel.size);
    serial_line_hex("kernel image bytes", kernel_header.image_size);
    if (kernel_header.magic != HALOXOS_MODULE_MAGIC) {
        panic("\\boot\\kernel.bin.zst does not carry the HaloxOS module header");
    }
    if (kernel_header.image_size == 0 ||
        kernel_header.image_size > 0x10000000u - 0x200000u) {
        panic("the compressed kernel header reports an unusable image size");
    }

    if (EFI_SUCCESS == load_file(root, L"\\boot\\doom.wadmodule", EfiLoaderData, &wad)) {
        have_wad = 1;
        serial_line_hex("WAD module at", wad.address);
        serial_line_uint("WAD module bytes", (UINT32)wad.size);
    } else {
        serial_puts("[EFI]: no DOOM WAD module on the boot volume\n");
    }

    rsdp = find_rsdp();
    serial_line_hex("ACPI RSDP", rsdp);

    module_table = (MultibootModule *)(void *)(mbi_block + MBI_OFF_MODULES);
    module_table[module_count].mod_start = (UINT32)kernel.address;
    module_table[module_count].mod_end = (UINT32)(kernel.address + kernel.size);
    module_table[module_count].cmdline = 0;
    module_table[module_count].reserved = 0;
    ++module_count;
    if (have_wad && module_count < MBI_MODULE_MAX) {
        module_table[module_count].mod_start = (UINT32)wad.address;
        module_table[module_count].mod_end = (UINT32)(wad.address + wad.size);
        module_table[module_count].cmdline = 0;
        module_table[module_count].reserved = 0;
        ++module_count;
    }

    info = (MultibootInfo *)(void *)(mbi_block + MBI_OFF_INFO);
    info->flags = MB_FLAG_CMDLINE | MB_FLAG_MODS | MB_FLAG_MMAP | MB_FLAG_BOOT_LOADER | MB_FLAG_FB;
    info->cmdline = build_cmdline(size_menu[chosen].width, size_menu[chosen].height, rsdp);
    info->mods_count = module_count;
    info->mods_addr = (UINT32)(UINTN)module_table;
    info->mmap_addr = (UINT32)(UINTN)(mbi_block + MBI_OFF_MMAP);
    {
        static const char loader_name[] = "HaloxOS UEFI boot loader 1.0";

        memcpy(mbi_block + MBI_OFF_LOADER_NAME, loader_name, sizeof(loader_name));
        info->boot_loader_name = (UINT32)(UINTN)(mbi_block + MBI_OFF_LOADER_NAME);
    }
    info->framebuffer_addr = choice.framebuffer;
    info->framebuffer_pitch = choice.pitch;
    info->framebuffer_width = choice.width;
    info->framebuffer_height = choice.height;
    info->framebuffer_bpp = (UINT8)choice.bpp;
    info->framebuffer_type = 1; /* direct RGB */
    info->framebuffer_red_field_position = (UINT8)lowest_bit(choice.red_mask);
    info->framebuffer_red_mask_size = (UINT8)popcount(choice.red_mask);
    info->framebuffer_green_field_position = (UINT8)lowest_bit(choice.green_mask);
    info->framebuffer_green_mask_size = (UINT8)popcount(choice.green_mask);
    info->framebuffer_blue_field_position = (UINT8)lowest_bit(choice.blue_mask);
    info->framebuffer_blue_mask_size = (UINT8)popcount(choice.blue_mask);

    console_line(L"");
    console_line(L"Starting HaloxOS...");
    serial_line_hex("cmdline address", info->cmdline);
    serial_line_hex("multiboot info address", (UINT64)(UINTN)info);
    serial_line_hex("memory map address", info->mmap_addr);

    /* Last moment at which the boot volume is still reachable: the loader's
     * trace - and the previous boot's kernel COM1 trace recovered from RAM -
     * goes onto the medium as the UEFI half of the debug capture, for a
     * machine with no serial port to read it from.  This final rewrite is the
     * most complete one - the milestone captures above already cover a boot
     * that dies before it. */
    serial_puts("[EFI]: capture mode: UEFI - previous boot's kernel trace (recovered "
                "from RAM) plus this loader's trace, into uefi-serial.log\n");
    write_serial_log(root);
    console_line(L"");
    console_line(L"Debug capture: uefi-serial.log holds the recovered kernel trace");
    console_line(L"and this boot chain's trace; bios-serial.log is the CSM boot log.");

    /* Step 1 of 4: the handoff block is complete and the firmware is about to
     * be left.  Nothing after this can be reported on the firmware console. */
    progress_step(&choice, 0u, PROGRESS_STEPS);
    if (EFI_ERROR(exit_boot_services())) {
        /* Boot services are still alive here (the exit failed), so the panic
         * screen is safe to draw. */
        console_alive = 1;
        panic("the firmware refused ExitBootServices");
    }
    /* From here on: no firmware services, direct serial only. */
    serial_puts("[EFI]: boot services exited\n");
    /* Step 2 of 4: boot services are gone, which is the transition a machine
     * with no legacy VGA either makes or does not. */
    progress_step(&choice, 1u, PROGRESS_STEPS);
    measure_memory();
    info->flags |= MB_FLAG_MEM;
    info->mem_lower = mem_lower_kb;
    info->mem_upper = mem_upper_kb;
    info->mmap_length = mmap_entries_written * (UINT32)sizeof(MultibootMmapEntry);
    serial_line_uint("mem_lower KB", info->mem_lower);
    serial_line_uint("mem_upper KB", info->mem_upper);
    serial_line_uint("mmap bytes", info->mmap_length);
    serial_puts("[EFI]: entering the 32-bit loader\n");

    /* Step 3 of 4: the memory map the kernel will be handed is built.  Step 4
     * is the jump itself, marked immediately before it because the loader it
     * enters has no way to report that it arrived. */
    progress_step(&choice, 2u, PROGRESS_STEPS);
    progress_step(&choice, 3u, PROGRESS_STEPS);
    efi_enter_loader(loader_entry, (UINT32)(UINTN)info, (UINT32)handoff_page);
    halt();
    return EFI_SUCCESS;
}

/* Copyright Svh03ra (C) 2026, All rights reserved */
/* Source File: linker.ld, kernel linker script. */

/* This repository is licensed under the GNU General Public License. */

/*
 * The kernel links at 2 MB: the zstd boot loader lives at 1 MB and
 * decompresses this image in place at its link address. The kernel must
 * also keep its multiboot header at the very start of the image so the
 * loader can verify it and find the entry point.
 */

ENTRY(start)

SECTIONS
{
    . = 2M;

    .multiboot ALIGN(4K) :
    {
        KEEP(*(.multiboot))
    }
    .text ALIGN(4K) :
    {
        *(.text*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata*)
    }

    .data ALIGN(4K) :
    {
        *(.data*)
    }

    .bss ALIGN(4K) :
    {
        /*
         * __bss_start/__bss_end bracket the ENTIRE bss (all kernel .bss
         * fragments plus the boot stack), not just one object's piece.
         * boot.asm zeroes this whole range at entry because the zstd
         * loader decompresses this kernel without GRUB loading the ELF,
         * so nothing else initializes .bss.
         */
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        . = ALIGN(4);
        __bss_end = .;
    }
}

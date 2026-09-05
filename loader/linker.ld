/* Copyright Svh03ra (C) 2026, All rights reserved */
/* Source File: loader/linker.ld, zstd boot loader linker script. */

/* This repository is licensed under the GNU General Public License. */

ENTRY(loader_start)

SECTIONS
{
    . = 1M;

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
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.comment*)
    }
}

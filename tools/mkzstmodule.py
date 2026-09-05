#!/usr/bin/env python3
# Copyright Svh03ra (C) 2026, All rights reserved
# Source File: tools/mkzstmodule.py, zstd kernel module packager.
#
# This repository is licensed under the GNU General Public License.

"""
Packages the HaloxOS kernel for the zstd boot loader.

Layout of the produced module file:
    offset 0:  ModuleHeader (20 bytes, little endian)
    offset 20: raw zstd frame of the kernel ELF image

ModuleHeader fields:
    magic         0x484C585A ('ZXLH')
    kernel_entry  virtual address of the kernel 'start' symbol
    image_size    byte size of the original (uncompressed) kernel ELF
    frame_size    byte size of the zstd frame that follows the header
    crc           XOR of the four fields above (sanity guard)

Usage: mkzstmodule.py <kernel.elf> <kernel.zst-frame> <output> <magic> <entry>
"""

import struct
import sys


def main() -> int:
    if len(sys.argv) != 6:
        print("usage: mkzstmodule.py <kernel.elf> <frame> <output> <magic> <entry>", file=sys.stderr)
        return 1

    kernel_path, frame_path, output_path, magic_text, entry_text = sys.argv[1:]

    with open(kernel_path, "rb") as f:
        kernel = f.read()
    with open(frame_path, "rb") as f:
        frame = f.read()

    magic = int(magic_text, 0)
    entry = int(entry_text, 0)
    image_size = len(kernel)
    frame_size = len(frame)

    if frame_size == 0:
        print("mkzstmodule: empty zstd frame", file=sys.stderr)
        return 1
    if frame_size >= image_size:
        print("mkzstmodule: warning, compressed frame is not smaller than the kernel", file=sys.stderr)

    crc = (magic ^ entry ^ image_size ^ frame_size) & 0xFFFFFFFF
    header = struct.pack("<IIIII", magic, entry, image_size, frame_size, crc)

    with open(output_path, "wb") as f:
        f.write(header)
        f.write(frame)

    ratio = frame_size * 10000 // image_size
    print("mkzstmodule: {} bytes -> {} bytes ({}.{:02d}% of original)".format(
        image_size, frame_size, ratio // 100, ratio % 100))
    return 0


if __name__ == "__main__":
    sys.exit(main())

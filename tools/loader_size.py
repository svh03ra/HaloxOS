#!/usr/bin/env python3
# Copyright Svh03ra (C) 2026, All rights reserved
# Source File: loader_size.py, per-size copies of the zstd boot loader.
#
# This repository is licensed under the GNU General Public License.
#
# The multiboot header is what tells the bootloader which display mode to
# program, and for a multiboot kernel that header is the only thing that does:
# gfxpayload is honoured for Linux kernels, not for this one (measured: a boot
# with gfxpayload=800x600x16 still came up at the header's 640x480).  So a boot
# menu that offers display sizes has to offer a loader whose header asks for
# each one.
#
# The header is 12 dwords at the start of the ELF's file image - magic, flags,
# checksum, the five a.out kludge fields (unused here, all zero) and then
# mode_type, width, height, depth - so a copy with a different geometry is the
# same binary with four dwords changed.  Patching the copy keeps the checksum
# valid (it covers magic and flags only) and keeps the header inside the first
# 8 KB of the file, which is where the multiboot specification requires it.
#
# usage: loader_size.py <in.elf> <out.elf> <width> <height> <depth>

import struct
import sys

MB_MAGIC = 0x1BADB002
MB_HEADER_WORDS = 12
MB_MODE_TYPE_OFFSET = 8   # dword index of mode_type in the header
MB_WIDTH_OFFSET = 9
MB_HEIGHT_OFFSET = 10
MB_DEPTH_OFFSET = 11


def main(argv):
    if len(argv) != 6:
        print("usage: loader_size.py <in.elf> <out.elf> <width> <height> <depth>")
        return 1
    source, target, width, height, depth = argv[1], argv[2], int(argv[3]), int(argv[4]), int(argv[5])

    with open(source, "rb") as handle:
        image = bytearray(handle.read())

    magic = struct.pack("<I", MB_MAGIC)
    position = image.find(magic)
    if position < 0 or position + MB_HEADER_WORDS * 4 > len(image):
        print("loader_size.py: no multiboot header in %s" % source)
        return 1
    if position & 3:
        print("loader_size.py: multiboot header is not dword aligned in %s" % source)
        return 1

    header = struct.unpack_from("<%dI" % MB_HEADER_WORDS, image, position)
    flags = header[1]
    checksum = header[2]
    if (MB_MAGIC + flags + checksum) & 0xFFFFFFFF:
        print("loader_size.py: multiboot header checksum is wrong in %s" % source)
        return 1
    if not (flags & 0x4):
        # A build that did not ask for a video mode (the 4bpp text build) has no
        # geometry for this tool to change.
        print("loader_size.py: %s does not request a video mode" % source)
        return 1

    struct.pack_into("<IIII", image, position + MB_MODE_TYPE_OFFSET * 4,
                     header[MB_MODE_TYPE_OFFSET], width, height, depth)

    with open(target, "wb") as handle:
        handle.write(image)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

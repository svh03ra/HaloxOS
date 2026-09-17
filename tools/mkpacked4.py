#!/usr/bin/env python3
# Copyright Svh03ra (C) 2026, All rights reserved
# Tool: mkpacked4.py - losslessly repack a background asset as 4bpp indexed.
#
# This repository is licensed under the GNU General Public License.
#
# Usage: mkpacked4.py <asset.bin>
#
# Backgrounds (login, theme1, theme2) are dithered images built from a very
# small set of colours. The v2 container stores them as raw RGB565 - two
# bytes per pixel - which is 614 KB per 640x480 image and is one of the
# biggest single consumers of kernel RAM.
#
# This tool detects a v2 opaque asset whose pixels use at most 16 distinct
# colours and rewrites it in place as the v3 "packed4" container:
#
#   u16 width, u16 height, u8 0xC4 (format), u8 0x48 (tag),
#   u8 palette_count (<= 16),
#   palette_count * u16 RGB565,
#   height rows of ceil(width / 2) bytes, high nibble = even x.
#
# The pixels are reproduced bit-for-bit (the input has <= 16 colours), so
# the result is a quarter of the size with no visual change, and the
# renderer expands two pixels per byte instead of copying one at a time.
# Assets that do not qualify (too many colours, alpha, already packed) are
# left untouched, so the pipeline stays correct for any art.

import os
import sys

FORMAT_V2_OPAQUE = 0xA2
FORMAT_V2_ALPHA = 0xB3
FORMAT_PACKED4 = 0xC4
TAG = 0x48
MAX_COLOURS = 16


def fail(message):
    sys.stderr.write("mkpacked4: %s\n" % message)
    raise SystemExit(1)


def repack(path):
    data = open(path, "rb").read()
    if len(data) < 6:
        fail("%s: too small to be an asset" % path)

    width = data[0] | (data[1] << 8)
    height = data[2] | (data[3] << 8)
    fmt = data[4]

    if fmt == FORMAT_PACKED4 and data[5] == TAG:
        return "already packed"
    if fmt == FORMAT_V2_ALPHA:
        return "has alpha - kept as v2"
    if fmt != FORMAT_V2_OPAQUE or data[5] != TAG:
        return "unknown format 0x%02x - kept as is" % fmt

    pixels = width * height
    body = data[6:]
    if len(body) < pixels * 2:
        fail("%s: truncated pixel data" % path)

    rgb = [body[i * 2] | (body[i * 2 + 1] << 8) for i in range(pixels)]
    palette = []
    index_of = {}
    for value in rgb:
        if value not in index_of:
            if len(palette) >= MAX_COLOURS:
                return "%d+ colours - kept as v2" % MAX_COLOURS
            index_of[value] = len(palette)
            palette.append(value)

    rows = []
    stride = (width + 1) // 2
    for y in range(height):
        row = bytearray(stride)
        base = y * width
        for x in range(width):
            packed = index_of[rgb[base + x]]
            if x & 1:
                row[x >> 1] |= packed
            else:
                row[x >> 1] = packed << 4
        rows.append(bytes(row))

    out = bytearray()
    out += bytes((width & 0xFF, (width >> 8) & 0xFF,
                  height & 0xFF, (height >> 8) & 0xFF,
                  FORMAT_PACKED4, TAG, len(palette)))
    for value in palette:
        out += bytes((value & 0xFF, (value >> 8) & 0xFF))
    for row in rows:
        out += row

    tmp = path + ".packed4"
    with open(tmp, "wb") as handle:
        handle.write(bytes(out))
    os.replace(tmp, path)
    return "packed4: %d colours, %d -> %d bytes" % (len(palette), len(data), len(out))


def main():
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    print("%s: %s" % (os.path.basename(sys.argv[1]), repack(sys.argv[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main())

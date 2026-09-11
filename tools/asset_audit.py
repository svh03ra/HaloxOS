#!/usr/bin/env python3
"""Audit redundancy inside HaloxOS .bin image assets (old 4B/px format).

Format: u16 width, u16 height, index plane (w*h), alpha plane (w*h),
rgb565 plane (w*h*2). Checks, per asset:
  1. alpha plane entropy: is it all 0xFF (pure padding for opaque draws)?
  2. index reconstructibility: is stored index == nearest_palette(rgb565)?
     i.e. could the index plane be regenerated from the rgb565 plane?
"""
import struct, sys

CUBE = [0, 51, 102, 153, 204, 255]
PAL = [(r, g, b) for r in CUBE for g in CUBE for b in CUBE]
PAL += [((i * 255) // 39,) * 3 for i in range(40)]

def nearest(r, g, b):
    best, bd = 0, 1 << 30
    for i, (pr, pg, pb) in enumerate(PAL):
        d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
        if d < bd:
            bd, best = d, i
    return best

def r565_to_rgb(v):
    return (((v >> 11) & 31) * 255 // 31,
            ((v >> 5) & 63) * 255 // 63,
            (v & 31) * 255 // 31)

for path in sys.argv[1:]:
    data = open(path, 'rb').read()
    w, h = struct.unpack_from('<HH', data, 0)
    n = w * h
    idx = data[4:4 + n]
    alpha = data[4 + n:4 + 2 * n]
    rgb = struct.unpack_from('<%dH' % n, data, 4 + 2 * n)
    opaque = all(a >= 128 for a in alpha)
    all_ff = all(a == 0xFF for a in alpha)
    mismatch = 0
    for i in range(n):
        r, g, b = r565_to_rgb(rgb[i])
        if idx[i] != nearest(r, g, b):
            mismatch += 1
    print("%-28s %dx%d  alpha: all-FF=%s opaque=%s  index-mismatch %d/%d (%.4f%%)"
          % (path.split('/')[-1], w, h, all_ff, opaque,
             mismatch, n, mismatch * 100.0 / n))

#!/usr/bin/env python3
"""Verify closed-form nearest-color == brute-force nearest_color over the
HaloxOS system palette (6x6x6 cube + 40 grays), for all 65536 RGB565
values, with identical tie-break semantics (first minimal index wins)."""
import sys

CUBE = [0, 51, 102, 153, 204, 255]
GRAY = [(i * 255) // 39 for i in range(40)]

def brute(r, g, b):
    best, bd = 0, 1 << 30
    for i in range(256):
        if i < 216:
            pr, pg, pb = CUBE[i // 36], CUBE[(i // 6) % 6], CUBE[i % 6]
        else:
            pr = pg = pb = GRAY[i - 216]
        d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
        if d < bd:
            bd, best = d, i
    return best

def closed(v):
    r = (((v >> 11) & 31) * 255) // 31
    g = (((v >> 5) & 63) * 255) // 63
    b = ((v & 31) * 255) // 31
    # per-channel nearest cube entry, first minimal wins
    ri = 0
    for c in range(1, 6):
        if (CUBE[c]-r)**2 < (CUBE[ri]-r)**2: ri = c
    gi = 0
    for c in range(1, 6):
        if (CUBE[c]-g)**2 < (CUBE[gi]-g)**2: gi = c
    bi = 0
    for c in range(1, 6):
        if (CUBE[c]-b)**2 < (CUBE[bi]-b)**2: bi = c
    dc = (CUBE[ri]-r)**2 + (CUBE[gi]-g)**2 + (CUBE[bi]-b)**2
    # gray: minimize (s-r)^2+(s-g)^2+(s-b)^2 over 40 floor shades;
    # quadratic in s with min at m=(r+g+b)/3 -> check i0-1..i0+1
    m = (r + g + b) // 3
    i0 = (m * 39) // 255
    best_i, best_d = -1, 1 << 30
    for i in (i0 - 1, i0, i0 + 1):
        if 0 <= i < 40:
            s = GRAY[i]
            d = (s-r)**2 + (s-g)**2 + (s-b)**2
            if d < best_d:
                best_d, best_i = d, i
    if best_i >= 0 and best_d < dc:
        return 216 + best_i
    return 36*ri + 6*gi + bi

bad = 0
for v in range(65536):
    r = (((v >> 11) & 31) * 255) // 31
    g = (((v >> 5) & 63) * 255) // 63
    b = ((v & 31) * 255) // 31
    if brute(r, g, b) != closed(v):
        bad += 1
        if bad < 5:
            print("MISMATCH v=%04x brute=%d closed=%d" % (v, brute(r, g, b), closed(v)))
print("mismatches:", bad, "/ 65536")
sys.exit(1 if bad else 0)

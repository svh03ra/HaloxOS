#!/usr/bin/env python3
# Copyright Svh03ra (C) 2026, All rights reserved
# Tool: mkwaddata.py - embed DOOM1.WAD into the kernel as a C byte array.
#
# This repository is licensed under the GNU General Public License.
#
# Usage: mkwaddata.py <wad-file> <placeholder-source> <output-source>
#
# Reads the WAD and emits a C source with two symbols the kernel's WAD
# reader consumes (declared in doom_wad_embed.h):
#   const uint8_t  doom_wad_data[]   - the raw IWAD bytes
#   const uint32_t doom_wad_data_size - exact byte count
# Without a WAD (or with DOOM_EMBED_WAD=0) the Makefile copies the
# placeholder source instead, which defines a 1-byte array and size 0.

import sys

def main() -> int:
    if len(sys.argv) != 4:
        print("usage: mkwaddata.py <wad-file> <placeholder-source> <output-source>", file=sys.stderr)
        return 1

    wad_path, placeholder_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    try:
        with open(wad_path, "rb") as f:
            data = f.read()
    except OSError as exc:
        print(f"mkwaddata.py: cannot read {wad_path}: {exc}", file=sys.stderr)
        return 1

    if len(data) < 12 or len(data) > 0x2000000:
        print(f"mkwaddata.py: {wad_path} is not a plausible WAD ({len(data)} bytes)", file=sys.stderr)
        return 1

    ident = data[:4]
    if ident not in (b"IWAD", b"PWAD"):
        print(f"mkwaddata.py: {wad_path} has no IWAD/PWAD magic: {ident!r}", file=sys.stderr)
        return 1

    with open(placeholder_path, "r", encoding="utf-8") as f:
        header = f.read()
    # Keep the placeholder's license banner (up to the include guard body).
    banner_end = header.find("#include")
    if banner_end < 0:
        banner_end = 0
    banner = header[:banner_end]

    lines = []
    line = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        line = ",".join(str(b) for b in chunk)
        lines.append(line)

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(banner)
        f.write("#include <stdint.h>\n\n")
        f.write(f"__attribute__((aligned(16), used))\n")
        f.write(f"const uint8_t doom_wad_data[{len(data)}] = {{\n")
        for entry in lines:
            f.write(entry + ",\n")
        f.write("};\n\n")
        f.write(f"const uint32_t doom_wad_data_size = {len(data)};\n")

    print(f"mkwaddata.py: embedded {len(data)} bytes from {wad_path} into {out_path}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

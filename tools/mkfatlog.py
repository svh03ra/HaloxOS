#!/usr/bin/env python3
# Copyright Svh03ra (C) 2026, All rights reserved
# Source File: tools/mkfatlog.py, pre-allocated bios-serial.log template
#              builder for the on-medium FAT boot log.
#
# This repository is licensed under the GNU General Public License.

"""
Builds the 64 KiB `bios-serial.log` template the Makefile drops into the
boot media's FAT filesystem (the ISO stick's embedded FAT12 partition and
the disk image's FAT16 partition).

The template is a fully pre-allocated, zero-filled file whose LAST 512
bytes hold a hidden stamp (past EOF, so the host never sees it) the
kernel validates before touching anything:

     0  magic  'HLXL' (0x4C584C48)
     4  version (2)
     8  reserved (0)
    12  capacity - file allocation bytes the stamp describes
    16  write_bytes - valid log bytes (0 fresh)
    20  boot_count - boots that have flushed (0 fresh)
    24  checksum - XOR of the six words above

At boot the kernel finds the *.LOG directory entry, walks the FAT chain,
and only arms itself when this stamp checks out - a foreign FAT volume
with a coincidental .LOG file is therefore never written.

Usage: mkfatlog.py <output> <capacity_bytes>
"""

import struct
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mkfatlog.py <output> <capacity_bytes>", file=sys.stderr)
        return 1

    output_path, capacity_text = sys.argv[1:]
    capacity = int(capacity_text, 0)
    if capacity < 4096 or capacity % 512 != 0:
        print("[ERROR!] mkfatlog: implausible capacity %d" % capacity,
              file=sys.stderr)
        return 1

    stamp = struct.pack("<7I",
                       0x4C584C48,   # 'HLXL'
                       2,            # version 2: FAT-file format
                       0,            # reserved
                       capacity,
                       0,            # write_bytes
                       0,            # boot_count
                       0)            # checksum placeholder
    checksum = 0
    for word in struct.unpack_from("<6I", stamp, 0):
        checksum ^= word
    stamp = stamp[:24] + struct.pack("<I", checksum)

    data = bytearray(capacity)
    # Place the 28-byte stamp at the START of the file's last sector;
    # a plain slice assignment here would SHRINK the bytearray.
    off = capacity - 512
    data[off:off + len(stamp)] = stamp

    with open(output_path, "wb") as out:
        out.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())

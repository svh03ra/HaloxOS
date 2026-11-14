#!/usr/bin/env python3
# Copyright Svh03ra (C) 2026, All rights reserved
# Source File: tools/fatpatch.py, build-time FAT patches for the
#              on-medium bios-serial.log volume.
#
# This repository is licensed under the GNU General Public License.

"""
Two small build-time patches for the bios-serial.log volume:

zero-size  <image> <byte-offset>
    The template file is mcopy'd in with its full 64 KiB allocation so
    the cluster chain exists, but a fresh stick should show an EMPTY
    bios-serial.log (the kernel grows the size field as it writes).
    This finds the *.LOG root-directory entry at <byte-offset> inside
    the image (0 for a standalone FAT volume, the partition start for
    the disk image) and zeroes its size field.

mbr  <image> <start_lba512> <sectors512>
    Registers the ISO stick's embedded FAT12 volume as MBR partition
    entry 2 (type 0x01, inactive, CHS out-of-range markers) so any host
    that plugs the dd'd stick in mounts it as an ordinary drive. Refuses
    to touch an MBR whose entry 2 is not free or whose signature is bad.

Usage:
    fatpatch.py zero-size build/bootlog-fat.img 0
    fatpatch.py zero-size build/HaloxOS-Disk_1.1.img 8388608
    fatpatch.py mbr build/HaloxOS-LiveCD_1.1.iso 20480 1024
"""

import struct
import sys


def die(msg):
    print("[ERROR!] fatpatch: %s" % msg, file=sys.stderr)
    sys.exit(1)


def parse_bpb(blob, base):
    """FAT12/16 geometry of the volume starting at byte <base>."""
    bytes_per_sector = struct.unpack_from("<H", blob, base + 11)[0]
    sectors_per_cluster = blob[base + 13]
    reserved = struct.unpack_from("<H", blob, base + 14)[0]
    nfats = blob[base + 16]
    root_entries = struct.unpack_from("<H", blob, base + 17)[0]
    fat_size = struct.unpack_from("<H", blob, base + 22)[0]
    if (bytes_per_sector != 512 or sectors_per_cluster == 0 or
            nfats == 0 or root_entries == 0 or fat_size == 0):
        die("volume at byte %d does not look like FAT12/16" % base)
    root_off = base + (reserved + nfats * fat_size) * 512
    root_sectors = (root_entries * 32 + 511) // 512
    return root_off, root_sectors


def zero_size(image, offset):
    with open(image, "r+b") as f:
        f.seek(offset)
        boot = f.read(512)
        if len(boot) != 512:
            die("cannot read the boot sector at byte %d" % offset)
        root_off, root_sectors = parse_bpb(boot, 0)

        for r in range(root_sectors):
            sec_off = offset + root_off + r * 512
            f.seek(sec_off)
            sec = bytearray(f.read(512))
            for e in range(16):
                entry_off = e * 32
                name = sec[entry_off:entry_off + 11]
                if name[0] == 0x00:
                    die("no *.LOG entry in the root directory")
                if name[0] == 0xE5:
                    continue
                attr = sec[entry_off + 11]
                if (attr & 0x3F) == 0x0F or (attr & 0x08) != 0:
                    continue
                if name[8:11].upper() != b"LOG":
                    continue
                before = struct.unpack_from("<I", sec, entry_off + 28)[0]
                sec[entry_off + 28:entry_off + 32] = b"\x00" * 4
                f.seek(sec_off)
                f.write(sec)
                print("[OK] fatpatch: bios-serial.log entry size "
                      "%d -> 0" % before)
                return 0
    die("no *.LOG entry in the root directory")
    return 1


def mbr(image, start_lba, sectors):
    with open(image, "r+b") as f:
        f.seek(510)
        sig = f.read(2)
        if sig != b"\x55\xAA":
            die("bad MBR signature")
        f.seek(446 + 16 + 4)  # entry 2 type byte
        if f.read(1) != b"\x00":
            die("MBR entry 2 is not free")
        entry = bytes([0x00,                 # not bootable
                       0xFE, 0xFF, 0xFF,    # CHS start: out of range
                       0x01,                # type: FAT12
                       0xFE, 0xFF, 0xFF])   # CHS end: out of range
        entry += struct.pack("<II", start_lba, sectors)
        f.seek(446 + 16)
        f.write(entry)
        print("[OK] fatpatch: MBR entry 2 = FAT12 at LBA %d, %d sectors"
              % (start_lba, sectors))
    return 0


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    mode = sys.argv[1]
    if mode == "zero-size" and len(sys.argv) == 4:
        return zero_size(sys.argv[2], int(sys.argv[3], 0))
    if mode == "mbr" and len(sys.argv) == 5:
        return mbr(sys.argv[2], int(sys.argv[3], 0), int(sys.argv[4], 0))
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())

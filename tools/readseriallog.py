#!/usr/bin/env python3
# Copyright Svh03ra (C) 2026, All rights reserved
# Tool: readseriallog.py - extract serial.log from a HaloxOS debug boot medium.
#
# This repository is licensed under the GNU General Public License.
#
# Usage: readseriallog.py --image <image-or-device> [--output serial.log]
#                          [--lba <lba>]
#
# The debug kernel mirrors the COM1 trace into a reserved slot on the medium
# it booted from (see src/kernel/debug/serial_log.c and tools/mkseriallog.py).
# This tool finds that slot - through the ISO PVD when the medium is an ISO,
# or at the disk build's fixed LBA otherwise - reads the header, and writes
# the captured text out as a plain serial.log, trimmed at the first all-zero
# sector and stripped of the zero padding.
#
# It reads a file (build/HaloxOS-Disk_1.1.img, build/HaloxOS-LiveCD_1.1.iso)
# or a raw device (/dev/sdX, \\.\PhysicalDrive2 - the latter needs an
# elevated shell on Windows).  Only the slot's own sectors are touched; the
# medium is opened read-only.
#
# What the medium looks like is not what the log looks like: the disk and
# floppy images carry a real FAT SERIAL.LOG that Explorer shows, while the
# ISO's slot is a raw region behind the volume that no filesystem exposes.
# A stick written with "ISO Image mode" has no slot at all - that mode
# copies the ISO's files into a fresh filesystem and drops the raw region - so
# an ISO stick meant to capture must be written in DD Image mode.

import argparse
import sys

HXSL_MAGIC = 0x4C535848
SECTOR = 512
DISK_LBA = 65536  # must match SERIAL_LOG_LBA in the Makefile
FLOPPY_LBA = 2624  # must match SERIAL_LOG_FLOPPY_LBA in the Makefile
HEADER_FIELDS = ("magic", "version", "header_bytes", "region_sectors",
                 "boot_drive", "date", "time", "flags")

# The diagnostic dwords the kernel (or the EFI loader) fills after the slot's
# identifying fields; see the header layout in src/kernel/debug/serial_log.c.
# They are the answer to "where did this capture stop", which is the question a
# stick pulled out of a machine that stopped booting early is asked.
DIAG_FIELDS = ("boot_stage", "stored_bytes", "last_error", "failed_writes",
               "capture_state", "reserved")
CAPTURE_STATES = {
    0: "off (release build or unknown medium)",
    1: "early (medium not identified yet)",
    2: "active",
    3: "UEFI - the EFI loader wrote this file, the kernel cannot",
    4: "off - no reserved slot on the boot medium",
    5: "off - read-only medium (pressed CD)",
    6: "stopped - the firmware refused further writes",
    7: "stopped - the reserved slot is full",
}
STATUS_OFFSET = 64
STATUS_MAX = 160


def fail(message):
    print(f"readseriallog: error: {message}", file=sys.stderr)
    sys.exit(1)


def u32(data, offset):
    return (data[offset] | (data[offset + 1] << 8) |
            (data[offset + 2] << 16) | (data[offset + 3] << 24))


def read_at(image, offset, count):
    image.seek(offset)
    return image.read(count)


def fs_type(image):
    """Name the filesystem whose boot sector sits at sector 0, if any.

    Used only to explain a failed search: a FAT32 volume with no reserved
    slot is what an 'ISO Image mode' write of the ISO leaves behind.
    """
    sector = read_at(image, 0, 512)
    if len(sector) < 512 or sector[510:512] != b'\x55\xaa':
        return None
    if sector[3:11] == b'NTFS    ':
        return "NTFS"
    if sector[0x52:0x5A] == b'FAT32   ':
        return "FAT32"
    if sector[0x36:0x3E] in (b'FAT12   ', b'FAT16   '):
        return sector[0x36:0x3B].decode()
    return None


def candidate_lbas(image):
    candidates = []
    pvd = read_at(image, 16 * 2048, 2048)
    if len(pvd) == 2048 and pvd[1:6] == b'CD001':
        volume = u32(pvd, 80)
        if volume:
            # The slot sits right behind the ISO volume, in 512-byte sectors.
            candidates.append(volume * 4)
    candidates.append(DISK_LBA)
    candidates.append(FLOPPY_LBA)
    return candidates


def main():
    parser = argparse.ArgumentParser(
        description="Extract the debug serial.log capture from a boot medium.")
    parser.add_argument("--image", required=True,
                        help="disk image, ISO image, or raw device")
    parser.add_argument("--output", default="serial.log")
    parser.add_argument("--lba", type=int, default=None,
                        help="override the 512-byte slot LBA")
    args = parser.parse_args()

    try:
        image = open(args.image, "rb", buffering=0)
    except OSError as exc:
        fail(f"cannot open {args.image}: {exc}")

    candidates = [args.lba] if args.lba is not None else candidate_lbas(image)
    lba = None
    header = b""
    for candidate in candidates:
        probe = read_at(image, candidate * SECTOR, SECTOR)
        if len(probe) == SECTOR and u32(probe, 0) == HXSL_MAGIC:
            lba = candidate
            header = probe
            break
    if lba is None:
        tried = ", ".join(str(candidate) for candidate in candidates)
        lines = [f"no serial.log slot in {args.image} (tried LBA {tried}).",
                 "  - only debug builds reserve the slot "
                 "(HALOXOS_CONFIG_DEBUG 1); a release build has none.",
                 "  - a USB stick written with \"ISO Image mode\" drops "
                 "the ISO's raw slot: re-write it in DD Image mode.",
                 "  - the Disk build's SERIAL.LOG is a real file, the one "
                 "Explorer shows next to the boot files.",
                 "  - the ISO's slot is a raw region; this tool is the only "
                 "reader that shows it."]
        kind = fs_type(image)
        if kind is not None:
            lines.append(f"  - this medium carries {kind} at sector 0.")
        fail("\n".join(lines))

    fields = dict(zip(HEADER_FIELDS, (u32(header, i * 4) for i in range(8))))
    # Dwords 0-7 are the identifying fields, 8-9 the directory-entry location,
    # and 10-15 the diagnostics.
    fields.update(dict(zip(DIAG_FIELDS, (u32(header, (i + 10) * 4) for i in range(6)))))
    region_sectors = fields["region_sectors"]
    if region_sectors < 2:
        fail(f"slot header names {region_sectors} sectors")

    text = bytearray()
    for index in range(1, region_sectors):
        sector = read_at(image, (lba + index) * SECTOR, SECTOR)
        if not sector:
            break  # beyond the medium; treat as end of capture
        if not any(sector):
            break  # the first untouched sector ends the capture
        text += sector
    image.close()

    text = bytes(text).rstrip(b"\x00")
    with open(args.output, "wb") as output:
        output.write(text)

    stamp = ""
    if fields["date"] or fields["time"]:
        stamp = (f", boot {fields['date']:08d} {fields['time']:06d}"
                 if fields["date"] and fields["time"] else "")
    print(f"readseriallog: {len(text)} bytes from LBA {lba} "
          f"(drive 0x{fields['boot_drive']:02X}{stamp}) -> {args.output}")

    state = fields["capture_state"]
    print(f"readseriallog: capture state {state} "
          f"({CAPTURE_STATES.get(state, 'unknown')})")
    print(f"readseriallog: boot stage {fields['boot_stage']}, "
          f"stored {fields['stored_bytes']} bytes, "
          f"failed writes {fields['failed_writes']}, "
          f"last firmware error 0x{fields['last_error']:02X}")
    status = header[STATUS_OFFSET:STATUS_OFFSET + STATUS_MAX]
    status = status.split(b"\x00", 1)[0].decode("ascii", "replace").rstrip("\r\n")
    if status:
        print("readseriallog: the medium's own summary:")
        for line in status.replace("\r", "").split("\n"):
            print(f"  {line}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# Copyright Svh03ra (C) 2026, All rights reserved
# Tool: mkseriallog.py - reserve the debug serial.log slot on a boot image.
#
# This repository is licensed under the GNU General Public License.
#
# Usage:
#   mkseriallog.py disk --image <disk.img> --partition-offset <bytes>
#                      --lba <lba> --sectors <n>
#   mkseriallog.py iso  --image <image.iso> --sectors <n>
#
# Debug builds mirror the COM1 trace into a slot on the boot medium (see
# src/kernel/debug/serial_log.c).  The kernel only writes into a slot whose
# header already carries the 'HXSL' magic, so the build lays that header down
# in one of two shapes:
#
#   disk   a real SERIAL.LOG file in the FAT partition (FAT16 on a disk,
#          FAT12 on a floppy).  The file's data clusters are pinned to the
#          slot LBA by writing the FAT chain and the root-directory entry
#          directly, which is what lets the kernel address the file by LBA
#          instead of carrying a FAT driver.  The entry's recorded size starts
#          at the header sector and the kernel raises it as it writes, so
#          Windows shows the log itself instead of a blank file the height of
#          the whole reserved slot; the location of the entry is left in the
#          slot header for exactly that.
#
#   iso    the slot is a raw region right after the ISO volume (the PVD's
#          volume-space size), before the DOOM WAD tail.  A pressed CD is
#          read-only; the region is what makes the capture work when the ISO
#          was dd'd to a USB stick, where the kernel still sees an ISO PVD.
#
# Both modes verify what they wrote and fail the build loudly otherwise.

import argparse
import struct
import sys

HXSL_MAGIC = 0x4C535848
VERSION = 1
SECTOR = 512


def fail(message):
    print(f"mkseriallog: error: {message}", file=sys.stderr)
    sys.exit(1)


def u16(data, offset):
    return data[offset] | (data[offset + 1] << 8)


def u32(data, offset):
    return (data[offset] | (data[offset + 1] << 8) |
            (data[offset + 2] << 16) | (data[offset + 3] << 24))


def fat_get(image, fat_start, fat_bits, cluster):
    if fat_bits == 12:
        offset = fat_start + (cluster * 3) // 2
        image.seek(offset)
        raw = image.read(2)
        if len(raw) < 2:
            fail("FAT read is past the end of the image")
        value = raw[0] | (raw[1] << 8)
        return (value >> 4) & 0x0FFF if (cluster & 1) else value & 0x0FFF
    image.seek(fat_start + cluster * 2)
    raw = image.read(2)
    if len(raw) < 2:
        fail("FAT read is past the end of the image")
    return raw[0] | (raw[1] << 8)


def fat_set(image, fat_start, fat_bits, cluster, value):
    if fat_bits == 12:
        # A 12-bit entry shares its third byte with its neighbour, so the
        # pair is read, one nibble group replaced, and written back whole.
        offset = fat_start + (cluster * 3) // 2
        image.seek(offset)
        raw = image.read(2)
        if len(raw) < 2:
            fail("FAT write is past the end of the image")
        old = raw[0] | (raw[1] << 8)
        if cluster & 1:
            new = (old & 0x000F) | ((value & 0x0FFF) << 4)
        else:
            new = (old & 0xF000) | (value & 0x0FFF)
        image.seek(offset)
        image.write(struct.pack('<H', new))
        return
    image.seek(fat_start + cluster * 2)
    image.write(struct.pack('<H', value & 0xFFFF))


# Cosmetic only: bytes 32.. of the header sector explain the file to a
# Notepad user.  The kernel writes its own copy at boot (and the two need not
# match); nothing reads this text.
BANNER = (b"HaloxOS debug serial.log - COM1 trace captured on the boot medium.\r\n"
          b"This 512-byte sector is the slot header, not trace text; the trace\r\n"
          b"starts in the next sector and tools/readseriallog.py strips it.\r\n")


DIRENT_LBA_OFFSET = 8 * 4      # dword 8: sector holding the directory entry
DIRENT_BYTE_OFFSET = 9 * 4     # dword 9: offset of the entry inside it
# The kernel fills the rest of this sector with its own diagnostics at boot:
# six dwords (boot stage, stored bytes, last firmware error, failed writes,
# capture state, reserved) from offset 40, a one-line human summary from 64 and
# the banner from 224.  The seed below carries the same shape so a freshly
# written image reads correctly on both sides of the first boot.
STATUS_OFFSET = 64
BANNER_OFFSET = 224

# Role flags, dword 7 (byte offset 28) of the header.  Bit 1 marks the UEFI
# half's slot: the file the EFI loader writes with the recovered kernel trace
# (plus its own).  The kernel's live capture refuses it; an older medium's
# single unflagged SERIAL.LOG stays the shared home it always was.
ROLE_BIOS = 0x1
ROLE_UEFI = 0x2

BIOS_STATUS = (b"capture=SEEDED  bios-serial.log: the CSM/BIOS boot's live COM1\r\n"
               b"trace.  The kernel writes this file as it runs.\r\n")
UEFI_STATUS = (b"capture=SEEDED  uefi-serial.log: written by the EFI loader with\r\n"
               b"the previous boot's kernel trace recovered from RAM.\r\n")


def make_header(region_sectors, role, status_text,
               dirent_lba=0, dirent_offset=0):
    # magic, version, header size, region size, boot drive, date, time, flags,
    # then the FAT directory entry's location so the kernel can grow the file's
    # recorded size while it writes.  The kernel rewrites this sector with the
    # real date and drive at boot; this copy only has to be recognizable.
    header = bytearray(struct.pack('<8I', HXSL_MAGIC, VERSION, SECTOR,
                                   region_sectors, 0, 0, 0, role))
    header += struct.pack('<II', dirent_lba, dirent_offset)
    header += struct.pack('<6I', 0, 0, 0, 0, 0, 0)
    assert len(header) == STATUS_OFFSET, "header field block has moved"
    header += status_text
    header += bytes(BANNER_OFFSET - len(header))
    header += BANNER
    return bytes(header) + bytes(SECTOR - len(header))


def lfn_checksum(short_11):
    # The FAT long-name checksum over the 11 short-name bytes.
    checksum = 0
    for byte in short_11:
        checksum = (((checksum & 1) << 7) + (checksum >> 1) + byte) & 0xFF
    return checksum


def lfn_entries(long_name, short_11):
    # Long-file-name entries, on-disk order: the tail entry (highest ordinal,
    # flagged 0x40) first, the head entry last, immediately before the short
    # entry that carries the checksum they all share.  The name's terminating
    # NUL is part of the character stream; everything after it pads with
    # 0xFFFF to the 13-character width of each entry.
    encoded = long_name.encode('utf-16-le') + b'\x00\x00'
    per_entry = 13
    count = (len(encoded) // 2 + per_entry - 1) // per_entry
    entries = []
    for ordinal in range(count, 0, -1):
        chunk = encoded[(ordinal - 1) * per_entry * 2: ordinal * per_entry * 2]
        chars = [chunk[i * 2] | (chunk[i * 2 + 1] << 8)
                 for i in range(len(chunk) // 2)]
        while len(chars) < per_entry:
            chars.append(0xFFFF)
        entry = bytearray(32)
        entry[0] = ordinal | (0x40 if ordinal == count else 0)
        for i in range(5):
            struct.pack_into('<H', entry, 1 + i * 2, chars[i])
        entry[11] = 0x0F
        entry[12] = 0
        entry[13] = lfn_checksum(short_11)
        for i in range(6):
            struct.pack_into('<H', entry, 14 + i * 2, chars[5 + i])
        struct.pack_into('<H', entry, 26, 0)
        for i in range(2):
            struct.pack_into('<H', entry, 28 + i * 2, chars[11 + i])
        entries.append(bytes(entry))
    return entries


def open_rw(path):
    try:
        return open(path, 'r+b', buffering=0)
    except OSError as exc:
        fail(f"cannot open {path}: {exc}")


def disk_slot(path, partition_offset, lba, sectors, single=False):
    if partition_offset % SECTOR != 0:
        fail("partition offset is not a multiple of 512")

    image = open_rw(path)
    image.seek(partition_offset)
    bpb = image.read(SECTOR)
    if len(bpb) != SECTOR or bpb[510:512] != b'\x55\xaa':
        fail("partition does not start with a boot sector")

    bytes_per_sector = u16(bpb, 0x0B)
    sectors_per_cluster = bpb[0x0D]
    reserved = u16(bpb, 0x0E)
    fat_count = bpb[0x10]
    root_entries = u16(bpb, 0x11)
    total_sectors = u16(bpb, 0x13) or u32(bpb, 0x20)
    fat_size = u16(bpb, 0x16) or u32(bpb, 0x24)

    if bytes_per_sector != SECTOR:
        fail(f"bytes per sector is {bytes_per_sector}, need 512")
    if sectors_per_cluster == 0 or fat_count < 1 or fat_size == 0:
        fail("not a FAT12/FAT16 partition this tool can patch")
    if root_entries == 0:
        fail("no fixed root directory; FAT32 is not supported")

    root_dir_sectors = (root_entries * 32 + SECTOR - 1) // SECTOR
    data_start = reserved + fat_count * fat_size + root_dir_sectors
    cluster_count = (total_sectors - data_start) // sectors_per_cluster
    if cluster_count == 0 or cluster_count > 0xFFF4:
        fail(f"cluster count {cluster_count} is not FAT12 or FAT16")
    fat_bits = 12 if cluster_count < 0xFF5 else 16

    partition_start = partition_offset // SECTOR
    root_start = partition_offset + (reserved + fat_count * fat_size) * SECTOR
    fat_starts = [partition_offset + (reserved + copy * fat_size) * SECTOR
                  for copy in range(fat_count)]
    end_of_chain = 0xFFF if fat_bits == 12 else 0xFFFF

    # The two halves of the capture, back to back: the CSM/BIOS boot writes
    # bios-serial.log live while it runs, the UEFI boot's EFI loader writes
    # uefi-serial.log with the previous boot's recovered kernel trace.  Their
    # headers carry the role the kernel checks before claiming a slot.
    # `single` keeps the old shape for a floppy: its data area has no room
    # for a second slot, and a floppy can never UEFI-boot, so there is no
    # UEFI half to serve there.
    slots = [
        ("bios-serial.log", b"BIOSSER LOG", lba, ROLE_BIOS, BIOS_STATUS),
    ]
    if not single:
        slots.append(("uefi-serial.log", b"UEFISER LOG", lba + sectors,
                      ROLE_UEFI, UEFI_STATUS))

    for name, short_11, slot_lba, role, status in slots:
        if slot_lba < partition_start + data_start:
            fail(f"slot LBA {slot_lba} is before the partition's data area")
        offset_in_data = slot_lba - partition_start - data_start
        if offset_in_data % sectors_per_cluster != 0:
            fail(f"slot LBA {slot_lba} is not aligned to a cluster")
        if sectors % sectors_per_cluster != 0:
            fail("slot size is not a whole number of clusters")

        first_cluster = offset_in_data // sectors_per_cluster + 2
        slot_clusters = sectors // sectors_per_cluster
        last_cluster = first_cluster + slot_clusters - 1
        if first_cluster < 2 or last_cluster > cluster_count + 1:
            fail("slot falls outside the partition's data area")
        if offset_in_data + sectors > total_sectors - data_start:
            fail("slot runs past the end of the partition")

        # The mapping is the whole point of the tool: the kernel writes at
        # this absolute LBA, so the chain must start exactly there.
        mapped_lba = partition_start + data_start + \
            (first_cluster - 2) * sectors_per_cluster
        if mapped_lba != slot_lba:
            fail(f"cluster {first_cluster} maps to LBA {mapped_lba}, "
                 f"not {slot_lba}")

        # The clusters must be free: a file already there means the pinned
        # LBA would silently overwrite live data.
        for fat_start in fat_starts:
            for cluster in range(first_cluster, last_cluster + 1):
                entry = fat_get(image, fat_start, fat_bits, cluster)
                if entry != 0:
                    fail(f"cluster {cluster} is not free (entry "
                         f"0x{entry:03X}); the disk layout changed under the "
                         "serial.log slots")

        lfn = lfn_entries(name, short_11)
        needed = len(lfn) + 1
        free_entry = None
        index = 0
        while index < root_entries:
            image.seek(root_start + index * 32)
            entry = image.read(32)
            if entry[0:11] == short_11:
                fail(f"{name} already exists in the root directory")
            if (free_entry is None and entry[0] in (0x00, 0xE5) and
                    index + needed <= root_entries):
                # Need `needed` consecutive entries: the LFN block plus the
                # short entry it decorates.
                ok = True
                for run in range(1, needed):
                    image.seek(root_start + (index + run) * 32)
                    nxt = image.read(32)
                    if len(nxt) < 32 or nxt[0] not in (0x00, 0xE5):
                        ok = False
                        break
                if ok:
                    free_entry = index
            if free_entry is not None:
                break
            index += 1
        if free_entry is None:
            fail("the root directory has no room for the serial.log files")

        # Chain the clusters and write the directory entries: the LFN block
        # (tail entry first), then the short entry with the first cluster.
        for fat_start in fat_starts:
            for cluster in range(first_cluster, last_cluster + 1):
                value = end_of_chain if cluster == last_cluster else cluster + 1
                fat_set(image, fat_start, fat_bits, cluster, value)

        # The entry starts at one sector (the slot header, which carries
        # the banner) and the kernel raises the size to match the text it
        # writes.  A full-height entry would make the freshly written image
        # show a blank file the size of the whole slot.
        for index, raw in enumerate(lfn):
            image.seek(root_start + (free_entry + index) * 32)
            image.write(raw)
        dirent_byte = root_start + (free_entry + len(lfn)) * 32
        dirent = bytearray(32)
        dirent[0:11] = short_11
        dirent[11] = 0x20  # archive
        struct.pack_into('<H', dirent, 22, 0)                              # time
        struct.pack_into('<H', dirent, 24, ((2026 - 1980) << 9) | (1 << 5) | 1)  # date
        struct.pack_into('<H', dirent, 26, first_cluster)
        struct.pack_into('<I', dirent, 28, SECTOR)
        image.seek(dirent_byte)
        image.write(dirent)

        # Zero the slot, then seed the header so the kernel recognizes it.
        image.seek(slot_lba * SECTOR)
        image.write(make_header(sectors, role, status,
                                dirent_byte // SECTOR, dirent_byte % SECTOR))
        image.write(bytes(sectors * SECTOR - SECTOR))

        image.seek(slot_lba * SECTOR)
        if u32(image.read(4), 0) != HXSL_MAGIC:
            fail("slot verification failed")
        image.flush()

        print(f"mkseriallog: {name} at LBA {slot_lba} "
              f"({sectors} sectors / {sectors * SECTOR // 1024} KiB, "
              f"FAT{fat_bits}), clusters {first_cluster}..{last_cluster}, "
              f"entry LBA {dirent_byte // SECTOR} +{dirent_byte % SECTOR}")

    image.close()


def iso_slot(path, sectors):
    if sectors % 4 != 0:
        fail("slot size must be a multiple of 4 (2048-byte CD sectors)")

    image = open_rw(path)
    image.seek(16 * 2048)
    pvd = image.read(2048)
    if len(pvd) != 2048 or pvd[1:6] != b'CD001':
        fail("no ISO9660 primary volume descriptor at sector 16")

    volume = u32(pvd, 80)
    if volume == 0:
        fail("the PVD volume-space size is zero")

    image.seek(0, 2)
    size = image.tell()
    if size != volume * 2048:
        fail(f"ISO size {size} does not end at the volume end {volume * 2048}; "
             "reserve the serial.log slot before the DOOM WAD tail is appended")

    region_bytes = sectors * SECTOR
    # The ISO's raw region is the CSM/BIOS half: a UEFI boot of an ISO reads
    # its files through the firmware's file protocol instead, so this slot
    # is what the kernel finds when the same ISO was dd'd to a USB stick
    # and booted legacy.
    image.seek(volume * 2048)
    image.write(make_header(sectors, ROLE_BIOS, BIOS_STATUS))
    image.write(bytes(region_bytes - SECTOR))

    image.seek(volume * 2048)
    if u32(image.read(4), 0) != HXSL_MAGIC:
        fail("slot verification failed")
    image.flush()
    image.close()

    print(f"mkseriallog: serial.log region at CD sector {volume} "
          f"({sectors // 4} CD sectors / {region_bytes // 1024} KiB)")


def main():
    parser = argparse.ArgumentParser(
        description="Reserve the debug serial.log slot on a boot image.")
    modes = parser.add_subparsers(dest="mode", required=True)

    disk = modes.add_parser("disk", help="patch a FAT16 disk image")
    disk.add_argument("--image", required=True)
    disk.add_argument("--partition-offset", type=int, required=True,
                      help="byte offset of the FAT partition in the image")
    disk.add_argument("--lba", type=int, required=True,
                      help="512-byte LBA the kernel writes to")
    disk.add_argument("--sectors", type=int, required=True)
    disk.add_argument("--single", action="store_true",
                      help="create bios-serial.log only (a floppy can never "
                           "UEFI-boot, and its data area has no room for a "
                           "second slot)")

    iso = modes.add_parser("iso", help="append the raw slot to an ISO image")
    iso.add_argument("--image", required=True)
    iso.add_argument("--sectors", type=int, required=True)

    args = parser.parse_args()
    if args.mode == "disk":
        disk_slot(args.image, args.partition_offset, args.lba, args.sectors,
                  single=getattr(args, "single", False))
    else:
        iso_slot(args.image, args.sectors)


if __name__ == "__main__":
    main()

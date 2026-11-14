#!/usr/bin/env python3
# Copyright Svh03ra (C) 2026, All rights reserved
# Source File: tools/serial-capture.py, real-hardware COM1 debug trace capture.
#
# This repository is licensed under the GNU General Public License.

"""
Captures the HaloxOS COM1 debug trace from a real serial port into
build/bios-serial.log - the same file the QEMU boot targets write.

On real hardware there is no host to write the log for you: the trace
leaves the target machine through its physical COM1 port, and the OS
cannot write it back to the boot medium itself (the kernel has no USB
mass-storage stack, a CD-R is read-only, and the ATA path only writes
fixed raw sectors). So the boot medium does not matter - USB stick, real
disk, floppy or CD all produce the identical serial trace - but the file
is written by a SECOND machine listening on the other end of the cable:

    target COM1 --[null-modem cable / USB-serial adapter]--> this machine

Run on the capture machine (pyserial required: pip install pyserial):

    make capture PORT=/dev/ttyUSB0        (Linux)
    make capture PORT=COM3               (Windows)

or directly:

    python3 tools/serial-capture.py /dev/ttyUSB0
    python3 tools/serial-capture.py COM3 --baud 115200 --out build/bios-serial.log

The loader and the kernel both open COM1 at 115200 8N1, so one baud
covers the whole boot. The log is regenerated on every capture, matching
the per-boot behaviour of the QEMU targets. A real BIOS does not emit
serial debug the way QEMU's SeaBIOS debugcon does, so the capture starts
at the loader's [LDR] lines. Press Ctrl+C to stop; the summary line
reports bytes and lines captured.
"""

import argparse
import os
import re
import sys


def normalize_port(name):
    r"""Windows COM ports above COM9 must be opened through the \\.\ prefix."""
    match = re.match(r"^COM(\d+)$", name, re.IGNORECASE)
    if match and int(match.group(1)) >= 10:
        # "\\" is one backslash at runtime: two + dot + one = the \\.\ prefix.
        return "\\" + "\\" + "." + "\\" + name.upper()
    return name


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Capture the HaloxOS COM1 debug trace from a real serial port "
                    "into build/bios-serial.log (the same file QEMU boots write).")
    parser.add_argument("port",
                        help="serial port of the capture machine: /dev/ttyUSB0, /dev/ttyS0, COM3, ...")
    parser.add_argument("--baud", type=int, default=115200,
                        help="baud rate (default 115200: the loader and kernel COM1 speed)")
    parser.add_argument("--out", default=os.path.join("build", "bios-serial.log"),
                        help="output log path (default: build/bios-serial.log)")
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print("[ERROR!] pyserial is required on the capture machine: pip install pyserial",
              file=sys.stderr)
        return 1

    out_dir = os.path.dirname(args.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    print("[INFO] HaloxOS real-hardware serial capture")
    print("[INFO] Port: %s @ %d baud, 8N1" % (args.port, args.baud))
    print("[INFO] Log : %s (regenerated per capture)" % args.out)
    print("[INFO] Boot the target machine now; press Ctrl+C here to stop.")

    try:
        port = serial.Serial(normalize_port(args.port), args.baud,
                             bytesize=serial.EIGHTBITS,
                             parity=serial.PARITY_NONE,
                             stopbits=serial.STOPBITS_ONE,
                             timeout=0.1)
    except serial.SerialException as exc:
        sys.stdout.flush()
        print("[ERROR!] Cannot open %s: %s" % (args.port, exc), file=sys.stderr)
        return 1

    total = 0
    lines = 0
    try:
        with open(args.out, "wb") as log:
            while True:
                data = port.read(4096)
                if data:
                    log.write(data)
                    total += len(data)
                    lines += data.count(b"\n")
                    log.flush()
    except KeyboardInterrupt:
        pass
    finally:
        port.close()

    print("[OK] Serial COM1 debug log saved: %s (%d bytes, %d lines)"
          % (args.out, total, lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())

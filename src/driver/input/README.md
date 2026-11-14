HaloxOS input driver notes
==========================

Current input paths
-------------------

* Legacy PS/2 keyboard via the 8042 data/status ports (0x60/0x64) and IRQ1.
* Firmware USB-legacy emulation, when the firmware exposes a USB keyboard as
  a PS/2-compatible device.
* BIOS INT 16h polling as a fallback until a real PS/2 key event is decoded.
* Native xHCI boot-protocol keyboard: when the 8042 path is not usable,
  `xhci_hid.c` adopts the firmware-configured interrupt-IN endpoint on the
  running controller (no reset, no command ring, no interrupter 0) and
  decodes boot reports into the same key queue. A private event ring on
  interrupter 1 signals completions; single-interrupter controllers fall back
  to endpoint-dequeue polling. An INT 16h service that merely answers a probe
  is not treated as a working keyboard path: on CSM/SeaBIOS systems it can
  answer while no key is ever delivered.

  A hard reset can leave the firmware with no HID endpoint: the previous OS
  owned the controller, so firmware skips USB input setup on the next boot.
  After ten non-destructive retries, `xhci_boot_recover()` flushes the
  on-medium trace, detaches the boot stick and performs a full controller
  reset plus root-port enumeration, even when the boot stick is on that
  controller. The reset invalidates the firmware disk slot, so later trace
  writes are lost; the report up to the reset is already on the medium and
  the kernel is running from RAM. Input recovery takes priority because a
  machine with no keyboard cannot continue.
* PS/2 mouse via the auxiliary controller port and IRQ12/polling.
* Native xHCI mouse-only mode: when the 8042/SMM keyboard is healthy but the
  PS/2 mouse is not, `xhci_mouse_init()` prefers a mouse interrupt-IN endpoint.
  A composite HID device contributes its first interrupt-IN endpoint
  (interface 0, the mouse on the known 30FA:1040); a lone endpoint or the
  remaining composite endpoint is adopted as unclassified, and its first
  report decides keyboard versus mouse. A real 8042 keyboard byte takes
  precedence, so a working PS/2 keyboard is never duplicated; this also
  rescues firmware whose 8042 command byte reads valid but never delivers a
  key. The PS/2 and INT 16h keyboard paths remain active. Both the
  three/four-byte boot-mouse report and the seven-byte report-ID-1 report
  protocol used by the 30FA:1040 are decoded.

UHCI/OHCI/EHCI controllers are still only reported by the boot diagnostics;
they are not initialized. Native HID support is intentionally limited to
xHCI root ports: full initialization enumerates root ports for a keyboard,
while adoption can reuse a firmware-configured interrupt-IN endpoint even if
the keyboard is behind a hub. Mouse-only mode reuses a running mouse endpoint
beside the legacy keyboard. Hubs, isochronous devices and unknown HID report
formats are skipped, and a slot with a bulk endpoint (the boot stick) is left alone during
adoption; the last-resort recovery necessarily invalidates it after
flushing the trace. A machine that exposes only USB HID through a UHCI/OHCI/EHCI
controller still needs a driver for that controller or firmware legacy
emulation.

Boot-menu diagnostics
---------------------

These rows are a debug-build diagnostic; a release build shows only the
boot menu itself.  The input line is refreshed after controller setup and shows the current
8042 configuration, IRQ1 count, raw keyboard-byte count, BIOS-key count, master
PIC mask, and 8042 status byte. Row 20 shows the native xHCI state
(`XHCI bb:ss.f run HID evt1 slot N ep M keys K`, or `XHCI none`). Useful
interpretations:

* `cfg 0x47` normally means IRQ1 enabled, IRQ12 enabled, and Set-1 translation
  enabled, with the first-port clock enabled.
* The command byte is written once and verified once. Do not add an
  `0xAD`/`0xAE` port-disable/enable cycle: VMware's virtual 8042 then reads
  back a correct command byte but never raises IRQ1 again. In a VMware VM,
  click inside the guest or press **Ctrl+G** first; the status bar's "To
  direct input to this VM" message means the host is still receiving the keys.
* In the `PIC` mask, bit 1 must be zero for IRQ1 to be unmasked (for example,
  `PIC 0xF8`).
* If IRQ1 and key-byte counts stay at zero, the keyboard is not reaching the
  legacy 8042 path. If BIOS count stays zero too, the debug build starts the
  native xHCI driver; check row 20 for its state.
* If the XHCI line says `no HID` or the trace says
  `xHCI running but no adoptable HID`, the firmware left no running HID
  endpoint. Normal init defers the reset so the trace stays writable; the
  boot check then flushes it and performs the full takeover if no keyboard
  exists.
* In mouse-only mode the trace prints
  `xHCI mouse-only composite endpoints`, the first DCI,
  `xHCI mouse-only adopting composite first endpoint`,
  `xHCI mouse-only probing extra endpoint` (a keyboard fallback),
  or `xHCI mouse-only probing keyboard-capable DCI` for a lone larger
  endpoint; then `xHCI mouse-only path ready`, and either
  `xHCI mouse report` (boot protocol) or
  `xHCI mouse report protocol (report ID 1)` plus `xHCI mouse delta`
  (the 30FA:1040 report protocol). If it prints
  `xHCI interrupt endpoint not running (DCI)`, the firmware never started
  that endpoint; the driver deliberately does not reset it while the
  firmware owns the command ring.
* The xHCI trace prints the MMIO base, max slots/ports/interrupters, whether
  the controller was running or stopped, the DCBAA address, and every
  non-empty slot's state, endpoint count and root port. A rejected endpoint
  logs whether it was not running, had an out-of-range max packet size, or had
  an unusable dequeue pointer; use those lines to tell a missing keyboard slot
  from one the firmware left halted or on an unconnected port.
* A dead 8042 can leave its status port at `0xFF`; the drain is bounded and is
  skipped once the native path is live, so a stuck output-full bit cannot hang
  the main loop.
* If key-byte count increases but the application does not react, inspect the
  scan-code decoder/event routing rather than the controller or PIC.

Boot input check
----------------

`input_boot_check.c` runs once after the 8042 and xHCI setup, before the main
loop.  Debug builds draw the `***** DEBUGGER *****` panel: `Checking xHCI...`,
a `Keyboard... OK/FAIL` row, a `Mouse... OK/FAIL` row, the live xHCI status
line, the connected root-port count, both input sources, the 8042 counters and
the retry handler, followed by `System started, SUCCESS!!!`.  The checking
stage is black, an OK row is painted with a green background and a failed row
uses the BOOT ERROR red.

When the 8042 keyboard is not usable, the native xHCI keyboard is the only
keyboard source.  The check retries the controller ten times, showing
`Reconnect attempt N of 10` and `Attempts left: N` with a PIT-based delay
between attempts.  Adoption mode only re-scans the firmware's configured
endpoints.  After the tenth failure, `xhci_boot_recover()` flushes
`bios-serial.log`, detaches the medium, resets the controller and enumerates
the root ports itself, even when the USB boot stick is on that controller.
This is the hard-reset recovery path; a successful enumeration continues the
boot, and only a second failure stops the machine.

If the takeover also finds no keyboard, the boot stops on the red
`***** BOOT ERROR!!! *****` screen (`xHCI input initialization failed`, all
inputs permanently disabled); the trace was flushed before the reset.  The
controller is reset and released so the next manual restart finds a clean
BIOS-owned controller.  A missing mouse is shown as FAIL but does not stop
the boot; the keyboard is what selects a boot option.

Release builds draw the same startup screen, but without the
`***** DEBUGGER *****` heading and without any of the detail rows (controller
line, root-port count, input sources, 8042 counters, retry handler).  The
plain `Checking xHCI input devices...` heading, the Keyboard/Mouse OK/FAIL
rows, the retry countdown and `System started, SUCCESS!!!` stay, so the
startup stage before the boot menu looks the same on both profiles.
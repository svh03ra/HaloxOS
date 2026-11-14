# Copyright Svh03ra (C) 2026, All rights reserved
# This repository is licensed under the GNU General Public License.

# Build recipes use Bash pipefail so compiler diagnostics can be shown live
# and simultaneously appended to build_log.txt without hiding failures.
SHELL := bash

AS := nasm
CC := gcc
LD := ld
OBJCOPY := objcopy
HOSTCC := gcc
PYTHON := python3
BUILD_STARTED := $(shell date +%s)
LOG_FILE = build_log.txt
# Truncate the log while make reads the makefile (not from a recipe): with
# a parallel build the recipes start out of order, and a recipe that
# truncated the log could otherwise wipe lines another job already wrote.
#
# Only the top-level make may do this.  The kernel recipe invokes
# sub-makes for its second and third link passes; without the guard each
# of those would truncate the log mid-build and throw away every line
# that explained a failure (exactly what made the first-run parallel
# failure so hard to see).
ifeq ($(MAKELEVEL),0)
$(shell : > $(LOG_FILE))
endif

# Parallel build. The kernel amalgamation, the loader, zstd, the asset
# pipeline and the ISO packaging are independent targets, so the default
# job count is the host CPU count - wall time drops from minutes to
# seconds. `make JOBS=1` restores strictly ordered (interleaving-free)
# output, which is handy when reading a failing build.
JOBS ?= $(shell nproc 2>/dev/null || echo 2)
ifeq ($(JOBS),)
JOBS := 2
endif
ifeq ($(JOBS),0)
JOBS := 2
endif
# Only the top-level make sets the job count.  A sub-make inherits the
# jobserver through MAKEFLAGS; forcing -j again makes make reset the
# jobserver ("warning: -jN forced in makefile") and can hide ordering
# bugs instead of preserving the parent's scheduling.
ifeq ($(MAKELEVEL),0)
ifeq ($(filter -j%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
endif

# Run a tool while preserving its real exit status, showing stdout/stderr live,
# and appending the same complete diagnostics to build_log.txt.
#
# Color handling: compilers are invoked with -fdiagnostics-color=always (see
# CFLAGS/LOADER_FLAGS/HOSTCFLAGS), so warnings/errors arrive here already
# ANSI-colored. On an interactive terminal the raw colored stream is echoed
# via /dev/tty while the file gets an escape-stripped copy, so build_log.txt
# stays readable plain text (editors, `cat`, CI logs). Without a controlling
# terminal (CI, pipes) everything goes to stdout unstripped, exactly as
# before.
define RUN_LOG
set -o pipefail; if [ -t 1 ] && [ -w /dev/tty ]; then $(1) 2>&1 | tee /dev/tty | sed 's/\x1B\[[0-9;]*[mK]//g' >> "$(LOG_FILE)"; else $(1) 2>&1 | tee >(sed 's/\x1B\[[0-9;]*[mK]//g' >> "$(LOG_FILE)"); fi
endef

COLOR_RESET := \033[0m
COLOR_GREEN := \033[92m
COLOR_YELLOW := \033[33m
COLOR_BLUE := \033[94m
COLOR_RED := \033[31m
COLOR_ORANGE := \033[38;5;208m
COLOR_WHITE := \033[97m

# Copy a finished boot image into $(OUT_DIR).  Shared by every media rule
# and by the format conversions, so the output folder always mirrors the
# images this build actually produced.
define PUBLISH_OUT
case "$(1)" in $(OUT_DIR)/*) ;; *) mkdir -p $(OUT_DIR) && cp -f $(1) $(OUT_DIR)/ || { $(call LOG_ERROR,Failed to copy $(1) to $(OUT_DIR)/); exit 1; } ;; esac
endef

LOG_COMPILE = printf '%b[Compiling...]%b %b%s%b %b%s%b\n' '$(COLOR_GREEN)' '$(COLOR_RESET)' '$(COLOR_YELLOW)' "$(1)" '$(COLOR_RESET)' '$(COLOR_BLUE)' "$(2)" '$(COLOR_RESET)' && printf '[Compiling...] %s -> %s\n' "$(1)" "$(2)" >> $(LOG_FILE)
LOG_COMPILE_LOG = printf '[Compiling...] %s -> %s\n' "$(1)" "$(2)" >> $(LOG_FILE)
LOG_DEP = printf '%b%s%b\n' '$(COLOR_BLUE)' "$(1)" '$(COLOR_RESET)' && printf '%s\n' "$(1)" >> $(LOG_FILE)
LOG_DEP_LOG = printf '%s\n' "$(1)" >> $(LOG_FILE)
LOG_ERROR = printf '%b[ERROR!]%b %b%s%b\n' '$(COLOR_RED)' '$(COLOR_RESET)' '$(COLOR_WHITE)' "$(1)" '$(COLOR_RESET)' && printf '[ERROR!] %s\n' "$(1)" >> $(LOG_FILE)
LOG_ERROR_LOG = printf '[ERROR!] %s\n' "$(1)" >> $(LOG_FILE)
LOG_WARNING = printf '%b[WARNING!]%b %b%s%b\n' '$(COLOR_ORANGE)' '$(COLOR_RESET)' '$(COLOR_WHITE)' "$(1)" '$(COLOR_RESET)' && printf '[WARNING!] %s\n' "$(1)" >> $(LOG_FILE)
LOG_WARNING_LOG = printf '[WARNING!] %s\n' "$(1)" >> $(LOG_FILE)
LOG_OK = elapsed_secs=$$(($$(date +%s) - $(BUILD_STARTED))); elapsed=$$(printf '%02d:%02d:%02d' $$((elapsed_secs / 3600)) $$(((elapsed_secs % 3600) / 60)) $$((elapsed_secs % 60))); printf '\n\n%bBuild Timelapse: %s%b\n%b[OK]%b %b%s%b\n' '$(COLOR_BLUE)' "$$elapsed" '$(COLOR_RESET)' '$(COLOR_GREEN)' '$(COLOR_RESET)' '$(COLOR_WHITE)' "$(1)" '$(COLOR_RESET)' && printf '\n\nBuild Timelapse: %s\n[OK] %s\n' "$$elapsed" "$(1)" >> $(LOG_FILE)
LOG_OK_LOG = elapsed_secs=$$(($$(date +%s) - $(BUILD_STARTED))); elapsed=$$(printf '%02d:%02d:%02d' $$((elapsed_secs / 3600)) $$(((elapsed_secs % 3600) / 60)) $$((elapsed_secs % 60))); printf '\n\nBuild Timelapse: %s\n[OK] %s\n' "$$elapsed" "$(1)" >> $(LOG_FILE)
LOG_CLEAN_OK = printf '%b[OK]%b %b%s%b\n' '$(COLOR_GREEN)' '$(COLOR_RESET)' '$(COLOR_WHITE)' "$(1)" '$(COLOR_RESET)' && printf '[OK] %s\n' "$(1)" >> $(LOG_FILE)
LOG_CLEAN_OK_LOG = printf '[OK] %s\n' "$(1)" >> $(LOG_FILE)
LOG_INTRO = printf '%s\n%s\n%s\n%s\nBuild Type: %s | %b%s%b\n%s\n\n%s\n\n' '==================================================' 'HaloxOS Compiler, (C) 2026 Svh03ra' '  ----------------------------------------------  ' 'Compiler Version: 2.0' "$(BUILD_MEDIA)" '$(BUILD_PROFILE_COLOR)' "$(BUILD_PROFILE)" '$(COLOR_RESET)' '==================================================' 'Starting to build...' && printf '%s\n%s\n%s\n%s\nBuild Type: %s | %s\n%s\n\n%s\n\n' '==================================================' 'HaloxOS Compiler, (C) 2026 Svh03ra' '  ----------------------------------------------  ' 'Compiler Version: 2.0' "$(BUILD_MEDIA)" "$(BUILD_PROFILE)" '==================================================' 'Starting to build...' >> $(LOG_FILE)
LOG_INTRO_LOG = printf '%s\n%s\n%s\n%s\nBuild Type: %s | %s\n%s\n\n%s\n\n' '==================================================' 'HaloxOS Compiler, (C) 2026 Svh03ra' '  ----------------------------------------------  ' 'Compiler Version: 2.0' "$(BUILD_MEDIA)" "$(BUILD_PROFILE)" '==================================================' 'Starting to build...' >> $(LOG_FILE)

CONFIG_SCREEN_WIDTH := $(shell sed -n 's/^#define HALOXOS_CONFIG_SCREEN_WIDTH[[:space:]]*//p' src/config/config.h)
CONFIG_SCREEN_HEIGHT := $(shell sed -n 's/^#define HALOXOS_CONFIG_SCREEN_HEIGHT[[:space:]]*//p' src/config/config.h)
CONFIG_DEBUG := $(shell sed -n 's/^#define HALOXOS_CONFIG_DEBUG[[:space:]]*//p' src/config/config.h)
CONFIG_DEV_MODE := $(shell sed -n 's/^#define HALOXOS_CONFIG_DEV_MODE[[:space:]]*//p' src/config/config.h)
CONFIG_SCREEN_DEPTH := $(shell sed -n 's/^#define HALOXOS_CONFIG_SCREEN_BPP[[:space:]]*//p' src/config/config.h)

# The geometry the BOOTLOADER is asked for: exactly the configured resolution.
#
# On a machine whose display mode can be programmed (Bochs/QEMU BGA, VMware
# SVGA, IBM VGA) the kernel sets the configured mode itself a moment later, so
# what GRUB hands over hardly matters. On a bootloader-framebuffer machine -
# real hardware, or a VM whose video device lacks those register interfaces -
# there is no protected-mode VBE call path at all: the mode GRUB sets IS the
# display mode for the whole session, and the desktop is rendered natively at
# 1:1 inside it.
#
# Asking GRUB for the configured geometry therefore makes the DISPLAY exactly
# the configured resolution: the whole screen is desktop, with no letterbox
# border and no scaling. The resolution arrows follow the output's own size, so
# they can still walk through the sizes this display can actually show.
#
# The depth stays the configured one: an 8bpp build must never silently become
# 16bpp (see GFXPAYLOAD below), and a 4bpp build is the classic VGA path, which
# asks GRUB for a text mode and programs mode 12h itself, because it never
# takes a linear framebuffer.
BOOT_SCREEN_WIDTH := $(CONFIG_SCREEN_WIDTH)
BOOT_SCREEN_HEIGHT := $(CONFIG_SCREEN_HEIGHT)

ASFLAGS := -DHALOXOS_BOOT_SCREEN_WIDTH=$(BOOT_SCREEN_WIDTH) -DHALOXOS_BOOT_SCREEN_HEIGHT=$(BOOT_SCREEN_HEIGHT) -DHALOXOS_BOOT_SCREEN_DEPTH=$(CONFIG_SCREEN_DEPTH)

# The default goal must stay `all`, no matter what gets defined after this
# line. GNU make picks the FIRST target it reads as the default goal: rules
# that appear before the first real target silently steal it. The WAD-module
# stamp (added before `all:`, with a fully @-silent recipe) did exactly that -
# a bare `make -j4` then "built" only the stamp, printing nothing and exiting
# 0 as if the whole OS had been built.
.DEFAULT_GOAL := all

# -fdiagnostics-color=always: make does not run gcc on a TTY (output is
# piped through RUN_LOG for the build log), so gcc would otherwise drop
# colors from warnings/errors. 'always' keeps the colored diagnostics the
# terminal expects; RUN_LOG strips the escapes for build_log.txt.
# Optimisation level. -O2 is the default: on this i386 target it compiles
# roughly 40% faster than -O3 and produces smaller code, which is what the
# slow/emulated machines actually benefit from. `make OPT=-O3` opts back
# into the aggressive setting without touching the makefile.
OPT ?= -O2
OPT_LD ?= -O2
CFLAGS := -std=gnu11 $(OPT) -Wall -Wextra -fdiagnostics-color=always -ffreestanding -fno-stack-protector -fno-pic -m32 -march=i386 -fno-asynchronous-unwind-tables -fno-unwind-tables -Ibuild/generated -Isrc/modes/states/graphical/desktop/apps/DOOM/port/include
LDFLAGS := -T linker.ld
# Both profiles keep EBP frame pointers: the crash handler walks the stack
# chain to show a backtrace, and release builds print the raw return
# addresses (their symbol table is intentionally empty). Debug builds also
# embed line info for the BSOD backtrace symbol table and the addr2line
# symbol data generated after the final link.
CFLAGS += -fno-omit-frame-pointer
ifeq ($(CONFIG_DEBUG),1)
# -g1 = line tables only. The crash handler needs file:line (addr2line)
# and the EBP chain needs frame pointers; full -g type/debug info is not
# read anywhere, costs megabytes of object file I/O and slows every
# compile and link. -g1 keeps the whole backtrace feature and builds far
# faster.
CFLAGS += -g1
endif
# Real-hardware bring-up knob: ask the firmware's INT 16h keyboard path even
# when the 8042 has already delivered bytes.  The default (0) only falls back
# when port 0x60 has been silent, so a working PS/2 path never double-reads.
# The forced mode is a test mode only: a firmware INT 16h peek can consume a
# scancode the 8042 path would otherwise have delivered, so it is expected to
# lose keys on a machine whose 8042 works.
BIOS_KBD_FORCE ?= 0
ifneq ($(BIOS_KBD_FORCE),0)
CFLAGS += -DHALOXOS_FORCE_BIOS_KEYBOARD=1
endif
# Diagnostic knob: set to 0 to remove the INT 16h keyboard fallback entirely.
# This isolates a suspected firmware-keyboard/8042 interaction without
# touching the PS/2 polling path.
BIOS_KBD_FALLBACK ?= 1
ifeq ($(BIOS_KBD_FALLBACK),0)
CFLAGS += -DHALOXOS_DISABLE_BIOS_KEYBOARD_FALLBACK=1
endif
# Native xHCI HID keyboard test knob: initialize the driver even when the
# 8042 or firmware INT 16h path looks usable.  The default (0) leaves a
# working legacy path untouched.
XHCI_HID_FORCE ?= 0
ifneq ($(XHCI_HID_FORCE),0)
CFLAGS += -DHALOXOS_FORCE_XHCI_HID=1
endif
HOSTCFLAGS = -std=c11 -O2 -Wall -Wextra -fdiagnostics-color=always $(shell pkg-config --cflags libpng)
HOSTLIBS = $(shell pkg-config --libs libpng)

# OS detection for the run targets: Windows (MinGW/MSYS/Cygwin) or Linux.
# Both paths are supported; the QEMU binary is located per platform.
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
HOST_OS := windows
else ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
HOST_OS := windows
else ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
HOST_OS := windows
else ifeq ($(findstring Windows,$(UNAME_S)),Windows)
HOST_OS := windows
else
HOST_OS := linux
endif

ifeq ($(HOST_OS),windows)
# Locate QEMU on Windows: PATH first, then common install locations
# (qemu.org installer, MSYS2/mingw64, w64devkit). Paths with spaces are
# quoted so the shell loop does not word-split them.
QEMU_BIN := $(shell q=$$(command -v qemu-system-i386 2>/dev/null); \
	if [ -n "$$q" ]; then echo "$$q"; \
	else \
		for c in "C:/Program Files/qemu/qemu-system-i386.exe" \
		         "C:/Program Files (x86)/qemu/qemu-system-i386.exe" \
		         "C:/qemu/qemu-system-i386.exe" \
		         "C:/msys64/mingw64/bin/qemu-system-i386.exe" \
		         "C:/msys64/usr/bin/qemu-system-i386.exe" \
		         "$(USERPROFILE)/qemu/qemu-system-i386.exe"; do \
			if [ -f "$$c" ]; then echo "$$c"; break; fi; \
		done; \
	fi)
else
QEMU_BIN := qemu-system-i386
endif

# qemu-img converts the raw disk image into the native format of the other
# hypervisors. It ships beside qemu-system-i386 in the Windows installer, so
# try that sibling first, then PATH, then the same common install locations.
ifeq ($(HOST_OS),windows)
QEMU_IMG := $(shell \
	if [ -n "$(QEMU_BIN)" ] && [ -x "$(dir $(QEMU_BIN))qemu-img.exe" ]; then \
		echo "$(dir $(QEMU_BIN))qemu-img.exe"; \
	else \
		q=$$(command -v qemu-img 2>/dev/null); \
		if [ -n "$$q" ]; then echo "$$q"; \
		else \
			for c in "C:/Program Files/qemu/qemu-img.exe" \
			         "C:/Program Files (x86)/qemu/qemu-img.exe" \
			         "C:/qemu/qemu-img.exe" \
			         "C:/msys64/mingw64/bin/qemu-img.exe" \
			         "C:/msys64/usr/bin/qemu-img.exe" \
			         "$(USERPROFILE)/qemu/qemu-img.exe"; do \
				if [ -f "$$c" ]; then echo "$$c"; break; fi; \
			done; \
		fi; \
	fi)
else
QEMU_IMG := qemu-img
endif

# ===== debug serial COM1 capture (debug builds only) =====
#
# Every QEMU boot of a built medium (CD-ROM ISO, hard disk, floppy, USB
# stick) captures the full COM1 debug trace into a temporary
# build/bios-serial.log, regenerated fresh on each boot (and wiped by
# `make clean` together with the rest of build/). The log carries the
# whole boot story in order:
#   - the firmware's own debug output (SeaBIOS -> debugcon port 0x402),
#     so the file starts at the very first BIOS step
#   - the zstd loader's pre-compression trace ([LDR] lines, printed
#     before the compressed kernel starts)
#   - the kernel's serial_trace() lines, including the crash dump
#
# The BIOS debugcon backend and the COM1 backend both append to the SAME
# file (append mode is why each launcher removes the log right before
# QEMU starts): the BIOS stream finishes before the loader begins
# writing, so the log reads chronologically.
#
# `make run serial` keeps the live COM1 view on the terminal and tees
# the same stream into the file through the stdio chardev's logfile
# option. Release builds keep the previous behaviour: plain stdio in
# serial mode, no capture otherwise.
SERIAL_LOG := build/bios-serial.log

ifeq ($(CONFIG_DEBUG),1)
ifneq ($(filter serial,$(MAKECMDGOALS)),)
SERIAL_LOG_ARGS := -chardev stdio,id=ser0,logfile=$(SERIAL_LOG),logappend=on -serial chardev:ser0 -monitor none -chardev file,id=biosdbg,path=$(SERIAL_LOG),append=on -device isa-debugcon,iobase=0x402,chardev=biosdbg
else
SERIAL_LOG_ARGS := -chardev file,id=debuglog,path=$(SERIAL_LOG),append=on,mux=on -serial chardev:debuglog -device isa-debugcon,iobase=0x402,chardev=debuglog
endif
SERIAL_LOG_RESET := rm -f $(SERIAL_LOG)
SERIAL_LOG_DONE := $(call LOG_CLEAN_OK,Serial COM1 debug log saved: $(SERIAL_LOG))
else
ifneq ($(filter serial,$(MAKECMDGOALS)),)
SERIAL_LOG_ARGS := -serial stdio -monitor none
endif
SERIAL_LOG_RESET :=
SERIAL_LOG_DONE :=
endif

QEMU_ARGS = -cdrom $(ISO) $(SERIAL_LOG_ARGS)

# DOOM game data disk (raw WAD stamp, same layout as the hard-disk
# build): attached as a second drive so the ISO boot can read the WAD
# through the kernel ATA driver. Only built when a WAD is present.
DOOMDISK := build/doomdata.img

# Testing flag: build the ISO with the loader's "not enough RAM" BOOT
# ERROR screen FORCED at boot, so it can be verified on real hardware
# (which always has far more than 8MB). This only changes the build; no
# emulator is launched. Matches -noram/--noram/-nr/--nr/-nomemory/
# --nomemory/-nm/--nm plus the dashless noram/nr/nomemory/nm spellings.
NORAM_MATCH := -noram --noram -nr --nr -nomemory --nomemory -nm --nm noram nr nomemory nm
ifndef NORAM_TEST
ifeq ($(filter $(NORAM_MATCH),$(MAKECMDGOALS)),)
NORAM_TEST := 0
else
NORAM_TEST := 1
endif
endif
# Stamp that records which mode the loader objects were last built in, so
# switching between `make` and `make noram` always rebuilds the loader.
NORAM_STAMP := build/generated/noram_stamp
ifeq ($(NORAM_TEST),1)
NORAM_STAMP_TEXT := test
else
NORAM_STAMP_TEXT := normal
endif
ARCH_PACKAGES := nasm gcc binutils grub xorriso pkgconf libpng dosfstools parted mtools zstd python3 qemu-system-x86 qemu-img
DEBIAN_PACKAGES := nasm gcc gcc-multilib binutils grub-pc-bin grub-common xorriso pkg-config libpng-dev dosfstools parted mtools zstd python3 qemu-system-x86 qemu-utils

# The hypervisor format targets build the hard disk too, so a one-shot
# `make vmware` (without an explicit `disk` goal) reports the right media.
DISK_FORMAT_GOALS := vmware vmw vmdk qemu qcow qcow2 qem hyper-v hyperv hv vhd virtualbox vb vbox vdi
ifneq ($(filter disk $(DISK_FORMAT_GOALS),$(MAKECMDGOALS)),)
BUILD_MEDIA := Hard Disk
else ifneq ($(filter floppy,$(MAKECMDGOALS)),)
BUILD_MEDIA := Floppy
else ifneq ($(filter usb,$(MAKECMDGOALS)),)
BUILD_MEDIA := USB
else
BUILD_MEDIA := CD-ROM (Default)
endif

ifeq ($(CONFIG_DEV_MODE),1)
ifeq ($(CONFIG_DEBUG),1)
BUILD_PROFILE := Development + Debug
BUILD_PROFILE_COLOR := $(COLOR_RED)
else
BUILD_PROFILE := Development
BUILD_PROFILE_COLOR := $(COLOR_RED)
endif
else ifeq ($(CONFIG_DEBUG),1)
BUILD_PROFILE := Debug
BUILD_PROFILE_COLOR := $(COLOR_ORANGE)
else
BUILD_PROFILE := Release
BUILD_PROFILE_COLOR := $(COLOR_GREEN)
endif

BUILD_ID := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
# Output folder: every finished image is written directly into build/out/,
# so the build tree never accumulates loose .iso/.img/.vmdk copies next to
# the object files. `make out` only reports what is available.
OUT_DIR := build/out

ISO := $(OUT_DIR)/HaloxOS-LiveCD_1.1.iso
DISK := $(OUT_DIR)/HaloxOS-Disk_1.1.img
# A BIOS-bootable USB image, not a dd'd ISO hybrid. A USB stick is a hard
# disk to the firmware; the ISO's first partition starts at LBA 1 and leaves
# no conventional MBR gap, which some BIOSes refuse to boot. This image keeps
# GRUB's boot.img in the MBR and core.img in the 1 MiB gap before a FAT32
# partition, matching the GNU GRUB BIOS installation layout.
USB := $(OUT_DIR)/HaloxOS-USB_1.1.img
USB_PART_START := 2048
USB_PART_END := 126975
USB_PART_SECTORS := 124928
# The USB image also carries the ISO's 512 KiB FAT12 log volume, registered
# as MBR partition 2.  A 64 MiB image is 131072 512-byte sectors.
USB_LOG_START := 126976
USB_LOG_SECTORS := 1024
# Converted copies of the same raw disk image, one per hypervisor format.
# `make disk <format>` builds the raw image and then converts it; the raw
# image itself is never replaced, so the QEMU launchers keep working.
DISK_VMDK  := $(DISK:.img=.vmdk)
DISK_QCOW2 := $(DISK:.img=.qcow2)
DISK_VHD   := $(DISK:.img=.vhd)
DISK_VDI   := $(DISK:.img=.vdi)
FLOPPY := $(OUT_DIR)/HaloxOS-Floppy_1.1.img

# The output folder must exist before the media recipes write into it.
$(OUT_DIR):
	@mkdir -p $(OUT_DIR)
KERNEL := build/kernel.bin
KERNEL_ZST := build/kernel.bin.zst
LOADER := build/loader.elf
# Debug-build crash-handler symbol table (see the rule near $(KERNEL)):
# fixed-size so embedding the data never shifts kernel addresses.
CRASH_SYMBOLS_H := build/generated/crash_symbols.h
CRASH_SYMBOLS_C := build/generated/crash_symbols.c
CRASH_SYMBOLS_OBJ := build/crash_symbols.o
CRASH_SYMBOL_MAX := 400
# Loader-side backtrace table (debug builds). Separate output directory and
# object so the loader's symbols never collide with the kernel's table; the
# generated files are the same format tools/mkcrashtable.py emits.
LOADER_SYMBOLS_DIR := build/generated/loader_symbols
LOADER_SYMBOLS_H := $(LOADER_SYMBOLS_DIR)/crash_symbols.h
LOADER_SYMBOLS_C := $(LOADER_SYMBOLS_DIR)/crash_symbols.c
LOADER_SYMBOLS_OBJ := build/loader_symbols.o
LOADER_SYMBOL_MAX := 512
RAM_REQUIREMENT_H := build/generated/ram_requirement.h
LOADER_OBJS := \
build/loader_boot.o \
build/loader_main.o \
build/loader_zstd.o \
build/loader_shim.o \
$(LOADER_SYMBOLS_OBJ)

LOADER_FLAGS := -std=gnu11 $(OPT_LD) -Wall -Wextra -fdiagnostics-color=always -ffreestanding -fno-stack-protector -fno-pic -m32 -march=i386 -fno-asynchronous-unwind-tables -I$(LOADER_SYMBOLS_DIR) -Iloader/include -Ibuild/generated -DHALOXOS_BOOT_SCREEN_DEPTH=$(CONFIG_SCREEN_DEPTH) -DHALOXOS_BOOT_SCREEN_WIDTH=$(BOOT_SCREEN_WIDTH) -DHALOXOS_BOOT_SCREEN_HEIGHT=$(BOOT_SCREEN_HEIGHT) -DHALOXOS_FORCE_RAM_ERROR=$(NORAM_TEST) -DHALOXOS_LOADER_TRACE=$(CONFIG_DEBUG)
ifeq ($(CONFIG_DEBUG),1)
LOADER_FLAGS += -g -fno-omit-frame-pointer
endif
ifeq ($(CONFIG_SCREEN_DEPTH),4)
LOADER_FLAGS += -DHALOXOS_BOOT_GRAPHICS_EXPECTED=0
else
LOADER_FLAGS += -DHALOXOS_BOOT_GRAPHICS_EXPECTED=1
endif
ZSTD_LEVEL := 12
MODULE_MAGIC := 0x484C585A
DISK_CORE := build/core_disk.img
FLOPPY_CORE := build/core_floppy.img
BUILD_INFO := build/generated/build_info.h
BOOT_DISK_CFG := build/generated/boot_disk.cfg
BOOT_FLOPPY_CFG := build/generated/boot_floppy.cfg
CORE_DISK_CFG := build/generated/core_disk.cfg
CORE_FLOPPY_CFG := build/generated/core_floppy.cfg
DESKTOP_LAYOUT := build/generated/desktop_layout_sector.bin
# Debug serial boot log (src/kernel/debug/bootlog.c): a pre-allocated
# bios-serial.log template the kernel auto-saves the COM1 debug trace
# into. The ISO embeds it in a small FAT12 volume appended after the
# ISO9660 content and registered in the hybrid MBR, so a dd'd USB stick
# shows the file to any host; the disk image keeps it in the existing
# FAT16 partition. No extraction step: the file is just there.
BOOTLOG_FILE_BYTES := 65536
BOOTLOG_FAT_KB := 512
BOOTLOG_FAT_BLOCKS := 256

# The pre-allocated, zero-filled template with its hidden tail stamp.
build/bios-serial.template: tools/mkfatlog.py | build
	@$(call LOG_COMPILE,bios-serial.log template,$@)
	@$(PYTHON) tools/mkfatlog.py $@ $(BOOTLOG_FILE_BYTES) || { $(call LOG_ERROR,Failed to generate $@); exit 1; }
# The caller may point at any legal IWAD explicitly, e.g.
# `make DOOM_WAD=/path/to/DOOM2.WAD`.  Without an override select only
# canonical IWAD filenames; do not glob *.wad, because a PWAD alone is
# not a runnable game.
DOOM_WAD ?= $(firstword $(wildcard DOOM1.WAD DOOM.WAD DOOMU.WAD DOOM2.WAD src/modes/states/graphical/desktop/apps/DOOM/DOOM1.WAD src/modes/states/graphical/desktop/apps/DOOM/DOOM.WAD src/modes/states/graphical/desktop/apps/DOOM/DOOMU.WAD src/modes/states/graphical/desktop/apps/DOOM/DOOM2.WAD))
DOOM_WAD_SECTORS := $(shell [ -n "$(DOOM_WAD)" ] && echo $$((($$(stat -c %s $(DOOM_WAD)) + 511) / 512)))
# Compact raw WAD area is reserved before the FAT partition. The legacy
# 131200/131204 layout is still readable by the kernel for existing images.
DOOM_WAD_END_BYTES := $(shell [ -n "$(DOOM_WAD)" ] && echo $$(( (2052 + $(DOOM_WAD_SECTORS) + 16) * 512 )))
# ISO tail blob: the WAD is appended to the ISO as a raw region of
# 2048-byte CD sectors (one info sector + the WAD), which the kernel
# reads through the ATAPI layer - makes the ISO self-contained.
DOOM_WAD_CD_SECTORS := $(shell [ -n "$(DOOM_WAD)" ] && echo $$(( ($$(stat -c %s $(DOOM_WAD)) + 2047) / 2048 )))
# The WAD multiboot module is the portable boot-media path for DOOM: GRUB loads
# it through the boot filesystem, so USB stick boots do not depend on ATA/ATAPI
# visibility after the kernel starts. The loader relocates it safely in RAM.
# Keep it ON by default for ISO/disk builds; the floppy target explicitly turns
# it off because a 1.44 MB floppy cannot contain a 4 MB WAD module.
DOOM_WAD_MODULE_BOOT ?= 1

# A missing WAD is NOT a build failure.
#
# The OS is complete without it: the DOOM app reports its own "game data not
# found" screen at runtime, and the three agents that ship game data (the
# multiboot module, the ISO tail blob, the second-drive data disk) simply have
# nothing to ship. Detect the absence once, here, so every later decision
# agrees with every other one, and warn once (see build-intro).
#
# Forcing the module OFF is what keeps the default `make` from depending on
# game data: with DOOM_WAD_MODULE_BOOT=0 the module drops out of the ISO's
# prerequisite list AND out of grub.cfg (DOOM_MODULE_CFG is emptied by the
# block below), so GRUB is never asked to load a file that does not exist.
ifeq ($(DOOM_WAD),)
DOOM_WAD_MISSING := 1
DOOM_WAD_MODULE_BOOT := 0
DOOM_WAD_HINT := DOOM1.WAD / DOOM.WAD / DOOMU.WAD / DOOM2.WAD, in the repository root or in src/modes/states/graphical/desktop/apps/DOOM/
else
DOOM_WAD_MISSING := 0
DOOM_WAD_HINT :=
endif

# Multiboot module: GRUB loads the WAD into RAM via BIOS, so the kernel
# reads it straight from memory on ANY boot media (USB stick, CD, disk)
# - no ATA/ATAPI needed on real hardware. The loader relocates it above
# the kernel memory span before decompressing (WAD_MODULE_RELOC_ADDR).
DOOM_WAD_MODULE := build/doom.wadmodule
ifeq ($(DOOM_WAD_MODULE_BOOT),1)
# The module is optional: on a machine at the 5 MB boot floor GRUB cannot
# stage a 4 MB module and prints "error: out of memory." before continuing.
# Keep the console terminal active around it: hiding that message behind a
# serial-only window made a failed module load look exactly like a boot hang
# on machines with no serial port.
DOOM_MODULE_CFG := '    module /boot/doom.wadmodule'
DOOM_WAD_MODULE_DEP := build/isodir/boot/doom.wadmodule
# The disk image copies the module straight from build/.  It must be a
# prerequisite, not just an `[ -f ]` test in the recipe: after `make clean`
# the module does not exist yet, and grub.cfg still references it.
DOOM_WAD_MODULE_IMAGE_DEP := $(DOOM_WAD_MODULE)
else
DOOM_MODULE_CFG :=
DOOM_WAD_MODULE_DEP :=
DOOM_WAD_MODULE_IMAGE_DEP :=
endif

# Stamp that records whether the last grub menu generation included the DOOM
# WAD module line.
#
# The module line depends on whether a WAD FILE exists, and a WAD appearing or
# disappearing touches no source file - so without this stamp a grub.cfg
# generated by an earlier WAD-present build would keep its
# `module /boot/doom.wadmodule` line, and GRUB would then refuse to boot that
# image ("file not found") even though the build itself succeeded. Same
# FORCE + text-compare trick as NORAM_STAMP/DOOM_WAD_EMBED_STAMP: the recipe
# runs on every make invocation but only bumps the mtime when the state
# actually flipped, which is what forces grub.cfg - and through it the media
# image - to be regenerated. It is defined HERE because make expands the
# prerequisite lists of the menu rules below when it reads them.
DOOM_WAD_MODULE_STAMP := build/generated/doom_wad_module_stamp
ifeq ($(DOOM_WAD_MODULE_BOOT),1)
DOOM_WAD_MODULE_STAMP_TEXT := module
else
DOOM_WAD_MODULE_STAMP_TEXT := plain
endif

$(DOOM_WAD_MODULE_STAMP): FORCE | build/generated
	@mkdir -p build/generated
	@if [ -f $@ ] && [ "$$(cat $@ 2>/dev/null)" = "$(DOOM_WAD_MODULE_STAMP_TEXT)" ]; then exit 0; fi; \
	printf '%s' '$(DOOM_WAD_MODULE_STAMP_TEXT)' > $@.tmp || { $(call LOG_ERROR,Failed to write $@); exit 1; }; \
	mv -f $@.tmp $@ || { $(call LOG_ERROR,Failed to update $@); exit 1; }

# Kernel-embedded WAD: DOOM1.WAD is baked into the kernel image as a
# data array (generated C source in its own object), so the game data
# ships with the kernel on EVERY boot media with zero bus I/O. This is
# the primary WAD source; the multiboot module and ATA/ATAPI stamps are
# fallbacks. Set DOOM_EMBED_WAD=0 to build without the embedded image
# (kernel ~4 MB smaller; the app then falls back to the module/media).
# Keep the default boot image compatible with the project's 8 MB RAM target.
# Embedding the 4 MB DOOM WAD inflates the kernel image and raises the
# boot-time RAM floor to the 22 MB span handled by the optional large-memory
# configuration below. DOOM can still load its WAD from the boot media.
DOOM_EMBED_WAD ?= 0

DOOM_WAD_DATA_SRC := src/modes/states/graphical/desktop/apps/DOOM/port/doom_wad_data.c
DOOM_WAD_DATA_GEN := $(DOOM_WAD_DATA_SRC).generated
DOOM_WAD_DATA_C := build/generated/doom_wad_data.c
DOOM_WAD_DATA_OBJ := build/doom_wad_data.o
# Whether the WAD is REALLY embedded in this build: DOOM_EMBED_WAD=1
# AND a WAD file is present. When true, the kernel image grows to ~11.2
# MB, so the loader and the kernel move every fixed address that lives
# in the kernel's shadow (zstd scratch, loader info block, DOOM zone,
# WAD module relocation target) above the image span, and the loader
# RAM floor rises from 8 MB to 22 MB.
ifneq ($(DOOM_WAD),)
DOOM_WAD_EMBED_STATE := $(shell if [ "$(DOOM_EMBED_WAD)" = "1" ]; then echo 1; else echo 0; fi)
else
DOOM_WAD_EMBED_STATE := 0
endif
ifeq ($(DOOM_WAD_EMBED_STATE),1)
DOOM_EMBED_DEFS := -DDOOM_WAD_EMBED_BUILD -DHALOXOS_KERNEL_IMAGE_SPAN
else
DOOM_EMBED_DEFS :=
endif
# Appended here (not inline in the := above): CFLAGS/LOADER_FLAGS are
# defined EARLIER in the file, so an inline reference would expand to
# empty before DOOM_EMBED_DEFS is computed. += defers the expansion.
CFLAGS += $(DOOM_EMBED_DEFS)
LOADER_FLAGS += $(DOOM_EMBED_DEFS)
GRUB_BOOT_IMG := /usr/lib/grub/i386-pc/boot.img
# Keep the partition table start and the mtools filesystem byte offset identical.
# LBA 2052 is reserved for the compact raw DOOM WAD region, so the FAT partition
# intentionally starts later (8 MiB) and never overlaps that raw data.
DISK_PART_START := 16384
DISK_PART_OFFSET := 8388608
FLOPPY_PART_START := 400
FLOPPY_PART_OFFSET := 204800
DESKTOP_LAYOUT_LBA := 1536
DOOM_WAD_INFO_LBA := 2048
DOOM_WAD_DATA_LBA := 2052
KERNEL_OBJS := \
build/boot.o \
build/realmode.o \
build/interrupts.o \
$(CRASH_SYMBOLS_OBJ) \
build/kernel.o \
$(DOOM_WAD_DATA_OBJ) \
build/login_asset.o \
build/theme1_asset.o \
build/theme2_asset.o \
build/user_frame_asset.o \
build/notepad_icon_asset.o \
build/terminal_icon_asset.o \
build/game_icon_asset.o \
build/demo_icon_asset.o \
build/program_icon_asset.o \
build/settings_icon_asset.o \
build/explorer_icon_asset.o \
build/taskmgr_icon_asset.o \
build/mines_icon_asset.o \
build/snake_icon_asset.o \
build/guessnum_icon_asset.o \
build/paint_icon_asset.o \
	build/power_icon_asset.o \
	build/box3d_icon_asset.o \
	build/firecracker_icon_asset.o \
	build/doom_icon_asset.o \
	build/coinrun_icon_asset.o \
	build/stress_icon_asset.o \
	build/coin_spr_player_asset.o \
	build/coin_spr_player_flip_asset.o \
	build/coin_spr_skull_asset.o \
	build/coin_spr_brick_asset.o \
	build/coin_spr_coin1_asset.o \
	build/coin_spr_coin1_flip_asset.o \
	build/coin_spr_coin2_asset.o \
	build/coin_spr_coin2_flip_asset.o \
	build/coin_spr_coin3_asset.o \
	build/coin_spr_coin3_flip_asset.o \
	build/coin_spr_coin4_asset.o \
	build/coin_spr_1up_asset.o \
	build/coin_spr_hud_lives_asset.o \
	build/coin_spr_hud_clock_asset.o \

KERNEL_ENTRY := src/kernel/system/kernel.c
KERNEL_FRAGMENTS := $(shell find src/driver src/kernel src/modes -name '*.c' ! -path '$(KERNEL_ENTRY)' | sort)
KERNEL_HEADERS := src/config/config.h
KERNEL_SRC := $(KERNEL_ENTRY) $(KERNEL_FRAGMENTS) $(KERNEL_HEADERS)

.PHONY: all clean run run-iso run-disk run-floppy run-usb run-usb-iso serial capture disk floppy usb out build-intro install-deps FORCE
all: build-intro check-build install-deps $(ISO)
	@$(call LOG_OK,Build completed successfully!)

disk: build-intro check-build install-deps $(DISK)

usb: build-intro check-build install-deps $(USB)

floppy: build-intro check-build install-deps
	@$(call LOG_WARNING,Floppy target: kernel-embedded WAD disabled so the kernel fits a 1.44 MB floppy)
	@$(MAKE) --no-print-directory DOOM_EMBED_WAD=0 DOOM_WAD_MODULE_BOOT=0 $(FLOPPY)

build-intro:
	@$(call LOG_INTRO)
	@if [ "$(DOOM_WAD_MISSING)" = "1" ]; then \
		$(call LOG_WARNING,DOOM WAD not found - building the OS without DOOM game data); \
		$(call LOG_DEP,Searched for: $(DOOM_WAD_HINT)); \
		$(call LOG_DEP,DOOM will show its own game-data-not-found screen. Everything else builds normally.); \
	fi

install-deps:
	@missing=0; \
	command -v nasm >/dev/null 2>&1 || missing=1; \
	command -v gcc >/dev/null 2>&1 || missing=1; \
	command -v ld >/dev/null 2>&1 || missing=1; \
	command -v grub-mkrescue >/dev/null 2>&1 || missing=1; \
	command -v grub-mkimage >/dev/null 2>&1 || missing=1; \
	command -v grub-file >/dev/null 2>&1 || missing=1; \
	command -v xorriso >/dev/null 2>&1 || missing=1; \
	if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists libpng; then missing=1; fi; \
	command -v mkfs.fat >/dev/null 2>&1 || missing=1; \
	command -v parted >/dev/null 2>&1 || missing=1; \
	command -v mmd >/dev/null 2>&1 || missing=1; \
 	command -v mcopy >/dev/null 2>&1 || missing=1; \
	command -v zstd >/dev/null 2>&1 || missing=1; \
	command -v $(PYTHON) >/dev/null 2>&1 || missing=1; \
	command -v qemu-system-i386 >/dev/null 2>&1 || missing=1; \
	command -v qemu-img >/dev/null 2>&1 || missing=1; \
	if [ $$missing -eq 1 ]; then \
		$(call LOG_DEP,Before starting build installing required packages.); \
		$(call LOG_DEP,[#] Checking build dependencies... Let's get some orderin'); \
		if [ "$$(id -u)" -eq 0 ]; then sudo_cmd=""; else sudo_cmd="sudo"; fi; \
		if command -v apt-get >/dev/null 2>&1; then \
			$(call LOG_DEP,[#] Detected Debian/Ubuntu/WSL. Installing with apt-get...); \
			$$sudo_cmd apt-get update || { $(call LOG_ERROR,apt-get update failed); exit 1; }; \
			$$sudo_cmd apt-get install -y $(DEBIAN_PACKAGES) || { $(call LOG_ERROR,apt-get dependency install failed); exit 1; }; \
		elif command -v pacman >/dev/null 2>&1; then \
			$(call LOG_DEP,[#] Detected Arch Linux. Installing with pacman...); \
			$$sudo_cmd pacman -Sy --needed --noconfirm $(ARCH_PACKAGES) || { $(call LOG_ERROR,pacman dependency install failed); exit 1; }; \
		else \
			$(call LOG_ERROR,Unsupported package manager. Please install one of these package sets manually.); \
			$(call LOG_DEP,Debian/Ubuntu/WSL: $(DEBIAN_PACKAGES)); \
			$(call LOG_DEP,Arch Linux: $(ARCH_PACKAGES)); \
			exit 1; \
		fi; \
	fi

check-build:
	@if [ -f "$(ISO)" ]; then \
		$(call LOG_WARNING,You have already built exists!); \
	fi

# Re-sync the output folder from whatever this tree has already produced,
# without rebuilding anything.  The media rules copy their own image as they
# finish; this target is for trees that built several formats over time.
out:
	@mkdir -p $(OUT_DIR)
	@count=0; \
	for f in $(ISO) $(DISK) $(USB) $(FLOPPY) $(DISK_VMDK) $(DISK_QCOW2) $(DISK_VHD) $(DISK_VDI); do \
		if [ -f "$$f" ]; then \
			count=$$((count + 1)); \
		fi; \
	done; \
	if [ $$count -eq 0 ]; then \
		$(call LOG_WARNING,No images to publish yet - build one medium first); \
	else \
		$(call LOG_OK,$$count image(s) available in $(OUT_DIR)/); \
	fi

build:
	@mkdir -p build

build/tools:
	@mkdir -p build/tools

build/isodir/boot/grub:
	@mkdir -p build/isodir/boot/grub

build/generated:
	@mkdir -p build/generated

FORCE:

# Written only when its content changes: the header carries the git hash
# into the kernel, so rewriting it on every build would force a full
# amalgamation recompile even when nothing (not even the commit) changed.
# Guarding the write keeps an up-to-date build at a couple of seconds.
$(BUILD_INFO): FORCE | build/generated
	@new_text=$$(printf '%s\n' "#ifndef HALOXOS_BUILD_TEXT" "#define HALOXOS_BUILD_TEXT \"Build: '$(BUILD_ID)'\"" "#endif"); \
	if [ -f $@ ] && [ "$$(cat $@)" = "$$new_text" ]; then :; \
	else \
		$(call LOG_COMPILE,$(BUILD_ID),$@); \
		printf '%s\n' "$$new_text" > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }; \
	fi

# gfxpayload is the ONLY way a bootloader-framebuffer machine (real hardware,
# a VM without the BGA/SVGA register interfaces) gets its resolution: the
# kernel has no protected-mode VBE call path, so whatever GRUB sets is what
# the desktop renders at - natively at 1:1, since the canvas follows the
# output mode. GRUB picks the first mode the firmware reports, so the
# configured geometry is requested first and a ladder of standard sizes
# follows it: a machine that cannot do the configured mode still boots a real
# framebuffer instead of dropping back to text mode or the 4bpp VGA path.
GFXPAYLOAD_LADDER_8 := 1920x1200x8,1920x1080x8,1680x1050x8,1600x1200x8,1600x900x8,1440x900x8,1400x1050x8,1366x768x8,1280x1024x8,1280x800x8,1280x720x8,1152x864x8,1024x768x8,800x600x8,640x480x8
GFXPAYLOAD_LADDER_16 := 1920x1200x32,1920x1200x16,1920x1080x32,1920x1080x16,1680x1050x32,1680x1050x16,1600x1200x32,1600x1200x16,1600x900x32,1600x900x16,1440x900x32,1440x900x16,1400x1050x32,1400x1050x16,1366x768x32,1366x768x16,1280x1024x32,1280x1024x16,1280x800x16,1280x720x32,1280x720x16,1152x864x16,1024x768x32,1024x768x16,800x600x32,800x600x16,640x480x32,640x480x16,640x480x15,640x480x8

ifeq ($(CONFIG_SCREEN_DEPTH),4)
GFXPAYLOAD := text
else ifeq ($(CONFIG_SCREEN_DEPTH),8)
# Request the configured geometry in real 8bpp / 256-colour mode (see the
# BOOT_SCREEN_* note above).
# Keep only same-depth fallbacks so an 8bpp build never silently becomes 16bpp.
GFXPAYLOAD := $(BOOT_SCREEN_WIDTH)x$(BOOT_SCREEN_HEIGHT)x8,$(GFXPAYLOAD_LADDER_8)
else
# Request the configured geometry in true 16bpp first.  15bpp is a compatibility
# fallback for older VBE implementations; 8bpp is the final colour fallback.
# No 4bpp fallback is requested here.
GFXPAYLOAD := $(BOOT_SCREEN_WIDTH)x$(BOOT_SCREEN_HEIGHT)x16,$(BOOT_SCREEN_WIDTH)x$(BOOT_SCREEN_HEIGHT)x15,$(BOOT_SCREEN_WIDTH)x$(BOOT_SCREEN_HEIGHT)x8,$(GFXPAYLOAD_LADDER_16)
endif

# GRUB always auto-boots the sole HaloxOS entry (timeout=0); the menu is not
# shown during normal startup. Debug builds still print phase markers and
# mirror them to COM1 before doing any video-BIOS work. Previously
# `insmod all_video` ran before terminal_output/menu setup, so a VBE/SeaVGABIOS
# problem could look exactly like a hard hang at "Welcome to GRUB!".
# For i386-pc BIOS, VBE is the backend the Multiboot framebuffer handoff needs;
# do not probe every unrelated graphics backend through `all_video`.
# Native 4-bpp VGA builds do not need VBE at all.
ifeq ($(CONFIG_SCREEN_DEPTH),4)
GRUB_VIDEO_SETUP :=
else
GRUB_VIDEO_SETUP := '    insmod vbe'
endif
ifeq ($(CONFIG_DEBUG),1)
GRUB_TIMEOUT := 0
GRUB_TRACE_TOP := 'echo "[GRUB] configuration loaded"'
GRUB_TRACE_ENTRY := '    echo "[GRUB] boot entry selected"'
# Mirror GRUB checkpoints to COM1 for the existing QEMU/hardware logger.
# Keep console output enabled so a missing physical UART never hides them.
GRUB_SERIAL_SETUP := 'insmod serial' 'serial --unit=0 --speed=115200' 'terminal_input console serial' 'terminal_output console serial'
GRUB_CORE_SERIAL_SETUP := 'insmod serial' 'serial --unit=0 --speed=115200' 'terminal_input console serial' 'terminal_output console serial'
GRUB_CORE_SERIAL_MODULE := serial
ifeq ($(CONFIG_SCREEN_DEPTH),4)
GRUB_TRACE_VBE :=
else
GRUB_TRACE_VBE := '    echo "[GRUB] VBE module loaded"'
endif
GRUB_TRACE_MODE := '    echo "[GRUB] gfxpayload set; next: multiboot loader"'
GRUB_TRACE_LOADER := '    echo "[GRUB] loader accepted; next: load kernel module"'
GRUB_TRACE_MODULES := '    echo "[GRUB] modules loaded; transferring control"'
GRUB_CORE_TRACE_START := 'echo "[GRUB-CORE] embedded config entered"'
GRUB_CORE_TRACE_SEARCH := 'echo "[GRUB-CORE] boot filesystem search completed"'
else
GRUB_TIMEOUT := 0
GRUB_TRACE_TOP :=
GRUB_TRACE_ENTRY :=
GRUB_SERIAL_SETUP :=
GRUB_CORE_SERIAL_SETUP :=
GRUB_CORE_SERIAL_MODULE :=
GRUB_TRACE_VBE :=
GRUB_TRACE_MODE :=
GRUB_TRACE_LOADER :=
GRUB_TRACE_MODULES :=
GRUB_CORE_TRACE_START :=
GRUB_CORE_TRACE_SEARCH :=
endif

$(BOOT_DISK_CFG): src/config/config.h $(DOOM_WAD_MODULE_STAMP) Makefile | build/generated
	@$(call LOG_COMPILE,boot disk menu,$@)
	@printf '%s\n' 'set timeout_style=hidden' 'set timeout=$(GRUB_TIMEOUT)' 'set default=0' 'terminal_output console' $(GRUB_TRACE_TOP) $(GRUB_SERIAL_SETUP) 'menuentry "HaloxOS!" {' $(GRUB_TRACE_ENTRY) $(GRUB_VIDEO_SETUP) $(GRUB_TRACE_VBE) '    set gfxpayload=$(GFXPAYLOAD)' $(GRUB_TRACE_MODE) '    multiboot /boot/loader.elf halox.boot=$$root' $(GRUB_TRACE_LOADER) '    module /boot/kernel.bin.zst' $(DOOM_MODULE_CFG) $(GRUB_TRACE_MODULES) '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(BOOT_FLOPPY_CFG): src/config/config.h Makefile | build/generated
	@$(call LOG_COMPILE,boot floppy menu,$@)
	@printf '%s\n' 'set timeout_style=hidden' 'set timeout=$(GRUB_TIMEOUT)' 'set default=0' 'terminal_output console' $(GRUB_TRACE_TOP) $(GRUB_SERIAL_SETUP) 'menuentry "HaloxOS!" {' $(GRUB_TRACE_ENTRY) $(GRUB_VIDEO_SETUP) $(GRUB_TRACE_VBE) '    set gfxpayload=$(GFXPAYLOAD)' $(GRUB_TRACE_MODE) '    multiboot /boot/loader.elf halox.boot=$$root' $(GRUB_TRACE_LOADER) '    module /boot/kernel.bin.zst' $(GRUB_TRACE_MODULES) '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(CORE_DISK_CFG): FORCE | build/generated
	@$(call LOG_COMPILE,core disk menu,$@)
	@printf '%s\n' 'terminal_output console' $(GRUB_CORE_SERIAL_SETUP) $(GRUB_CORE_TRACE_START) 'search --no-floppy --file --set=root /boot/grub/grub.cfg' $(GRUB_CORE_TRACE_SEARCH) 'set prefix=($$root)/boot/grub' 'configfile /boot/grub/grub.cfg' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(CORE_FLOPPY_CFG): FORCE | build/generated
	@$(call LOG_COMPILE,core floppy menu,$@)
	@printf '%s\n' 'terminal_output console' $(GRUB_CORE_SERIAL_SETUP) $(GRUB_CORE_TRACE_START) 'search --no-floppy --file --set=root /boot/grub/grub.cfg' $(GRUB_CORE_TRACE_SEARCH) 'set prefix=($$root)/boot/grub' 'configfile /boot/grub/grub.cfg' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(DESKTOP_LAYOUT): | build/generated
	@$(call LOG_COMPILE,desktop layout,$@)
	@$(PYTHON) -c "import struct; magic=0x484C5850; positions=[(18,24),(18,104),(18,184),(96,24),(96,104),(96,184),(174,24),(174,104),(174,184),(252,24),(252,104)]; payload=[p[0] for p in positions] + [p[1] for p in positions]; checksum=magic; [globals().__setitem__('checksum', checksum ^ v) for v in payload]; data=struct.pack('<II' + 'i' * len(payload), magic, checksum, *payload); open('$(DESKTOP_LAYOUT)', 'wb').write(data + b'\\x00' * (512 - len(data)))" || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

build/tools/png2indexed: tools/png2indexed.c | build/tools
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(HOSTCC) $(HOSTCFLAGS) -o $@ $< $(HOSTLIBS)) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

# Full-screen backgrounds: after the v2 RGB565 conversion, repack them as
# 4bpp indexed when they use at most 16 colours (they are dithered from a
# handful of colours, so this is lossless). It cuts ~1.4 MB of kernel RAM
# across login + theme1 + theme2; any asset that does not qualify is left
# as v2 automatically by the tool.
build/login.bin: src/modes/states/graphical/login/ui/login.png build/tools/png2indexed tools/mkpacked4.py | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }
	@$(PYTHON) tools/mkpacked4.py $@ || { $(call LOG_ERROR,Failed to pack $@); exit 1; }

build/theme1.bin: src/modes/states/graphical/desktop/ui/backgrounds/theme1.png build/tools/png2indexed tools/mkpacked4.py | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }
	@$(PYTHON) tools/mkpacked4.py $@ || { $(call LOG_ERROR,Failed to pack $@); exit 1; }

build/theme2.bin: src/modes/states/graphical/desktop/ui/backgrounds/theme2.png build/tools/png2indexed tools/mkpacked4.py | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }
	@$(PYTHON) tools/mkpacked4.py $@ || { $(call LOG_ERROR,Failed to pack $@); exit 1; }

build/user_frame.bin: src/modes/states/graphical/login/ui/user-frame.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/notepad_icon.bin: src/modes/states/graphical/desktop/apps/notepad/notepad.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/terminal_icon.bin: src/modes/states/graphical/desktop/apps/terminal/terminal.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/game_icon.bin: src/modes/states/graphical/desktop/apps/game_center/game.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/demo_icon.bin: src/modes/states/graphical/desktop/apps/demo_center/demo.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/program_icon.bin: src/modes/states/graphical/desktop/ui/icons/program.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/settings_icon.bin: src/modes/states/graphical/desktop/apps/settings/settings.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/explorer_icon.bin: src/modes/states/graphical/desktop/apps/explorer/explorer.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/taskmgr_icon.bin: src/modes/states/graphical/desktop/apps/task_manager/taskmgr.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/mines_icon.bin: src/modes/states/graphical/desktop/apps/mines/minesw.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/snake_icon.bin: src/modes/states/graphical/desktop/apps/snake/snake.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/guessnum_icon.bin: src/modes/states/graphical/desktop/apps/guess_number/guessnum.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/paint_icon.bin: src/modes/states/graphical/desktop/apps/paint/paint.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/power_icon.bin: src/modes/states/graphical/desktop/apps/power/power.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/box3d_icon.bin: src/modes/states/graphical/desktop/apps/3DBox/appicon.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/firecracker_icon.bin: src/modes/states/graphical/desktop/apps/firecracker/appicon.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/doom_icon.bin: src/modes/states/graphical/desktop/apps/DOOM/appicon.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/login_asset.o: build/login.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/theme1_asset.o: build/theme1.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/theme2_asset.o: build/theme2.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/user_frame_asset.o: build/user_frame.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/notepad_icon_asset.o: build/notepad_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/terminal_icon_asset.o: build/terminal_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/game_icon_asset.o: build/game_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/demo_icon_asset.o: build/demo_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/program_icon_asset.o: build/program_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/settings_icon_asset.o: build/settings_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/explorer_icon_asset.o: build/explorer_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/taskmgr_icon_asset.o: build/taskmgr_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/mines_icon_asset.o: build/mines_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/snake_icon_asset.o: build/snake_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/guessnum_icon_asset.o: build/guessnum_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/paint_icon_asset.o: build/paint_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/power_icon_asset.o: build/power_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/box3d_icon_asset.o: build/box3d_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/firecracker_icon_asset.o: build/firecracker_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/doom_icon_asset.o: build/doom_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

# Coin Run! app icon (the 16x16 HUD lives sprite scaled 2x to 32x32) and
# its game sprites. The sprites keep the original dimensions so the ported
# collision boxes line up with the art.
build/coinrun_icon.bin: src/modes/states/graphical/desktop/apps/coin_game/coinrun.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/coin_spr_%.bin: src/modes/states/graphical/desktop/apps/coin_game/sprites/%.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/coinrun_icon_asset.o: build/coinrun_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/coin_spr_%_asset.o: build/coin_spr_%.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/stress_icon.bin: src/modes/states/graphical/desktop/apps/stress/stress.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/stress_icon_asset.o: build/stress_icon.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/boot.o: src/kernel/system/boot.asm Makefile | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(AS) $(ASFLAGS) -f elf32 -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

# The real-mode bridge for BIOS calls (VBE mode setting).  It is plain 32-bit
# protected-mode code plus a 16-bit stub that is copied into low memory at
# runtime, so it needs no bootloader flags - just the assembler.
build/realmode.o: src/kernel/system/realmode.asm Makefile | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(AS) -f elf32 -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

build/interrupts.o: src/kernel/system/interrupts.asm | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(AS) -f elf32 -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

# ===== kernel-embedded DOOM WAD =====

# The 4 MB WAD cannot live in the kernel amalgamation object (it would
# grow build/kernel.o and every debug pass-2 relink); it is a separate
# object with a generated C source instead of ld -b binary so the
# byte-array symbol carries an explicit size the reader can trust.

# Stamp that records whether the last WAD data generation embedded the
# WAD, so switching between `make` (embedded) and `make floppy`
# (DOOM_EMBED_WAD=0, kernel small enough for 1.44 MB) always regenerates
# build/generated/doom_wad_data.c. Like NORAM_STAMP: the recipe runs on
# every make invocation but only rewrites (bumping the mtime) when the
# embed state actually changed, which then triggers regeneration.
DOOM_WAD_EMBED_STAMP := build/generated/doom_wad_embed_stamp
ifeq ($(DOOM_WAD_EMBED_STATE),1)
DOOM_WAD_EMBED_STAMP_TEXT := embed
else
DOOM_WAD_EMBED_STAMP_TEXT := plain
endif

$(DOOM_WAD_EMBED_STAMP): FORCE | build/generated
	@mkdir -p build/generated
	@if [ -f $@ ] && [ "$$(cat $@ 2>/dev/null)" = "$(DOOM_WAD_EMBED_STAMP_TEXT)" ]; then exit 0; fi; \
	printf '%s' '$(DOOM_WAD_EMBED_STAMP_TEXT)' > $@.tmp || { $(call LOG_ERROR,Failed to write $@); exit 1; }; \
	mv -f $@.tmp $@ || { $(call LOG_ERROR,Failed to update $@); exit 1; }

$(DOOM_WAD_DATA_C): $(DOOM_WAD_DATA_SRC) $(DOOM_WAD) $(DOOM_WAD_EMBED_STAMP) | build/generated
	@$(call LOG_COMPILE,DOOM WAD data,$@)
	@if [ "$(DOOM_EMBED_WAD)" = "1" ] && [ -n "$(DOOM_WAD)" ]; then \
		python3 tools/mkwaddata.py "$(DOOM_WAD)" "$(DOOM_WAD_DATA_SRC)" $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }; \
	else \
		cp $(DOOM_WAD_DATA_SRC) $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }; \
	fi

$(DOOM_WAD_DATA_OBJ): $(DOOM_WAD_DATA_C) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(CC) $(CFLAGS) -Isrc/modes/states/graphical/desktop/apps/DOOM/port -c -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

build/kernel.o: $(KERNEL_SRC) $(BUILD_INFO) $(CRASH_SYMBOLS_H) $(DOOM_WAD_DATA_C) $(RAM_REQUIREMENT_H) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(CC) $(CFLAGS) -c -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

# ===== zstd boot chain =====

build/loader_boot.o: loader/boot.asm src/config/config.h Makefile | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(AS) $(ASFLAGS) -f elf32 -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

# The kernel link's second pass rewrites ram_requirement.h from the real
# __bss_end and relinks.  The loader embeds the same header, so it must
# not compile until that pass is done: otherwise a parallel first build
# can bake in the pre-link stub (a too-small RAM gate) and only the next
# `make` - after the header is final - produces a correct loader.  The
# order-only dependency serialises the two without making the loader
# relink every time the kernel does.
build/loader_main.o: loader/main.c loader/include/string.h src/config/config.h $(NORAM_STAMP) $(DOOM_WAD_DATA_C) $(RAM_REQUIREMENT_H) $(LOADER_SYMBOLS_H) | build $(KERNEL)
	@$(call LOG_COMPILE,$<,$@)
	@if [ ! -f $(RAM_REQUIREMENT_H) ]; then $(MAKE) --no-print-directory $(RAM_REQUIREMENT_H) || exit 1; fi
	@$(call RUN_LOG,$(CC) $(LOADER_FLAGS) -c -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

# Generate the loader's RAM requirement from the linked kernel's actual
# memory span. AUTO-FIT scheme:
#   span_end   = __bss_end  (highest byte the boot chain needs)
#   info_addr  = span_end rounded up to 64 KiB + one 64 KiB guard block
#                (the loader handoff block lives HERE, so it is always
#                above the real image, never above a guessed size)
#   gate_bytes = info_addr + one more 64 KiB (stack/mmap breathing room)
#   req_mb     = ceil(gate_bytes / 1 MiB)  -> what the BOOT ERROR screen shows
# The zstd scratch block is placed by the loader itself, so it adds nothing
# to the gate. With the current span this yields a 5 MB gate: 5 MB machines
# boot.
# The embedded-WAD span build keeps its dedicated 22 MB layout. There is no
# hardcoded floor: the gate is whatever the real image needs, so the boot
# requirement always shrinks/grows with the kernel automatically.
# ram_requirement.h: generates stub on first run (no KERNEL), then real values
# after KERNEL exists. The KERNEL recipe explicitly invokes this target for
# the real generation pass (two-pass build like crash_symbols).
$(RAM_REQUIREMENT_H): Makefile tools/gen_rr.sh | build/generated
	@if [ ! -f $(KERNEL) ] || ! nm $(KERNEL) 2>/dev/null | grep -q ' B __bss_end$$'; then \
		$(call LOG_COMPILE,ram requirement stub,$@); \
		{ \
			echo '#ifndef HALOXOS_RAM_REQUIREMENT_H'; \
			echo '#define HALOXOS_RAM_REQUIREMENT_H'; \
			echo ''; \
			echo '/* Stub - real values generated after first kernel link */'; \
			if [ "$(DOOM_WAD_EMBED_STATE)" = "1" ]; then \
				echo '#define HALOXOS_KERNEL_MEMORY_BYTES 0x1500000u'; \
				echo '#define HALOXOS_LOADER_INFO_ADDR 0x1500000u'; \
				echo '#define HALOXOS_KERNEL_RAM_REQUIRED_MB 22u'; \
				echo '#define HALOXOS_KERNEL_RAM_REQUIRED_BYTES 0x1600000u'; \
				echo '#define HALOXOS_KERNEL_RAM_GATE_BYTES 0x1600000u'; \
			else \
				echo '#define HALOXOS_KERNEL_MEMORY_BYTES 0x460000u'; \
				echo '#define HALOXOS_LOADER_INFO_ADDR 0x470000u'; \
				echo '#define HALOXOS_KERNEL_RAM_REQUIRED_MB 5u'; \
				echo '#define HALOXOS_KERNEL_RAM_REQUIRED_BYTES 0x500000u'; \
				echo '#define HALOXOS_KERNEL_RAM_GATE_BYTES 0x480000u'; \
			fi; \
			echo ''; \
			echo '#endif'; \
		} > $@.stub.tmp || { $(call LOG_ERROR,Failed to create stub $@); exit 1; }; \
		mv -f $@.stub.tmp $@ || { $(call LOG_ERROR,Failed to install stub $@); exit 1; }; \
	else \
		bash tools/gen_rr.sh $(KERNEL) $@ $(DOOM_WAD_EMBED_STATE); \
	fi

# The stamp recipe must run on every make invocation (like BUILD_INFO) so a
# mode switch is detected even though the file always exists; it only
# rewrites the file - and thus changes its mtime - when the mode actually
# changed, which then triggers the loader_main.o rebuild.
$(NORAM_STAMP): FORCE | build/generated
	@mkdir -p build/generated
	@if [ -f $@ ] && [ "$$(cat $@ 2>/dev/null)" = "$(NORAM_STAMP_TEXT)" ]; then exit 0; fi; \
	printf '%s' '$(NORAM_STAMP_TEXT)' > $@.tmp || { $(call LOG_ERROR,Failed to write $@); exit 1; }; \
	mv -f $@.tmp $@ || { $(call LOG_ERROR,Failed to update $@); exit 1; }

build/loader_zstd.o: loader/zstd_all.c $(wildcard third_party/zstd-1.5.7/lib/**/*.c) | build
	@$(call LOG_COMPILE,zstd 1.5.7 decompressor,$@)
	@$(call RUN_LOG,$(CC) $(LOADER_FLAGS) -c -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

build/loader_shim.o: loader/shim.c loader/include/string.h | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(CC) $(LOADER_FLAGS) -c -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

# Loader backtrace symbol table. Same fixed-size trick as the kernel's
# crash table (see CRASH_SYMBOLS_*): the loader is linked once with the
# previous/empty table, the table is regenerated from that just-linked ELF,
# and only this object is recompiled before the final link. Only text
# symbols are needed for return-address lookup, so data symbols cannot
# shadow a code frame.
# Makefile is a prerequisite because LOADER_SYMBOL_MAX lives here: the
# header's CRASH_SYMBOL_MAX must match the table the data pass emits.
$(LOADER_SYMBOLS_H): Makefile | build/generated
	@if [ ! -f $@ ] || ! grep -q 'CRASH_SYMBOL_MAX $(LOADER_SYMBOL_MAX)$$' $@; then \
		$(call RUN_LOG,$(PYTHON) tools/mkcrashtable.py /nonexistent $(LOADER_SYMBOLS_DIR) --empty --max $(LOADER_SYMBOL_MAX) --text) || { $(call LOG_ERROR,Failed to create the loader symbol table); exit 1; }; \
	fi

$(LOADER_SYMBOLS_C): $(LOADER_SYMBOLS_H) FORCE | build/generated
	@if [ "$(CONFIG_DEBUG)" != "1" ] || [ ! -f $(LOADER) ]; then \
		$(call RUN_LOG,$(PYTHON) tools/mkcrashtable.py /nonexistent $(LOADER_SYMBOLS_DIR) --empty --max $(LOADER_SYMBOL_MAX) --text) || { $(call LOG_ERROR,Failed to create the loader symbol table); exit 1; }; \
	else \
		$(call RUN_LOG,$(PYTHON) tools/mkcrashtable.py $(LOADER) $(LOADER_SYMBOLS_DIR) --max $(LOADER_SYMBOL_MAX) --text) || { $(call LOG_ERROR,Failed to create the loader symbol table); exit 1; }; \
	fi

$(LOADER_SYMBOLS_OBJ): $(LOADER_SYMBOLS_C) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(CC) $(LOADER_FLAGS) -c -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

$(LOADER): $(LOADER_OBJS) loader/linker.ld
	@$(call LOG_COMPILE,loader objects,$@)
	@$(call RUN_LOG,$(LD) -m elf_i386 -T loader/linker.ld -o $@ $(LOADER_OBJS)) || { $(call LOG_ERROR,Failed to link $@); exit 1; }
	@grub-file --is-x86-multiboot $@ || { $(call LOG_ERROR,$@ has no valid Multiboot-1 header in GRUB's scan window); exit 1; }
ifeq ($(CONFIG_DEBUG),1)
	@# Pass 2: refresh the table from the just-linked loader. .text comes
	@# before .rodata in loader/linker.ld, so replacing the table strings
	@# cannot move a code address; the relinked frames stay valid.
	@$(MAKE) --no-print-directory $(LOADER_SYMBOLS_C)
	@$(MAKE) --no-print-directory $(LOADER_SYMBOLS_OBJ)
	@$(call RUN_LOG,$(LD) -m elf_i386 -T loader/linker.ld -o $@ $(LOADER_OBJS)) || { $(call LOG_ERROR,Failed to link $@); exit 1; }
	@grub-file --is-x86-multiboot $@ || { $(call LOG_ERROR,$@ has no valid Multiboot-1 header in GRUB's scan window); exit 1; }
endif

# Flat binary kernel image: file offset 0 == link address 0x200000, so the
# loader can decompress it straight into place and jump to the entry VMA.
# Compressing the ELF instead would ship ELF headers/offsets the loader
# cannot relocate, and the entry would land on garbage.
KERNEL_FLAT := build/kernel.flat
KERNEL_BASE := 0x200000

$(KERNEL_FLAT): $(KERNEL) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(OBJCOPY) -O binary $(KERNEL) $@) || { $(call LOG_ERROR,Failed to flatten $< to $@); exit 1; }

$(KERNEL_ZST): $(KERNEL_FLAT) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,zstd -$(ZSTD_LEVEL) -T0 -f -q -o $@.frame $<) || { $(call LOG_ERROR,Failed to compress $< with zstd); exit 1; }
	@entry_addr=$$(nm $(KERNEL) | awk '/ T start$$/ {print "0x"$$1}'); \
	if [ -z "$$entry_addr" ]; then $(call LOG_ERROR,kernel start symbol not found); exit 1; fi; \
	$(PYTHON) tools/mkzstmodule.py $< $@.frame $@ $(MODULE_MAGIC) $$entry_addr || { $(call LOG_ERROR,Failed to package $@); exit 1; }
	@rm -f $@.frame

$(DISK_CORE): $(CORE_DISK_CFG) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_DISK_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal echo configfile multiboot search search_fs_file vbe $(GRUB_CORE_SERIAL_MODULE)) || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(FLOPPY_CORE): $(CORE_FLOPPY_CFG) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_FLOPPY_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal echo configfile multiboot search search_fs_file vbe $(GRUB_CORE_SERIAL_MODULE)) || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

# Symbol table for the debug-build crash-handler backtrace.
#
# The kernel embeds a FIXED-SIZE table (CRASH_SYMBOL_MAX entries, unused
# slots zeroed) from its own object, so filling in the real symbols never
# moves a kernel address. That is what makes a single-pass build possible:
#
#   pass 1: link the kernel with the previous (or empty) table -> addresses
#   pass 2: one nm + ONE addr2line for the whole table -> crash_symbols.c,
#           recompile just this object (~0.2 s) and relink. Layout is
#           identical because the table always has CRASH_SYMBOL_MAX slots.
#
# The amalgamation is therefore compiled once, and the old per-symbol
# addr2line loop (400 process spawns, ~1 minute) is gone.

# Header: stable declarations, written once per tree. Also creates an
# initial empty fixed-size crash_symbols.c so a fresh tree links.
$(CRASH_SYMBOLS_H): | build/generated
	@if [ ! -f $@ ]; then \
		$(call RUN_LOG,$(PYTHON) tools/mkcrashtable.py /nonexistent build/generated --empty --max $(CRASH_SYMBOL_MAX)) || { $(call LOG_ERROR,Failed to create the crash symbol table); exit 1; }; \
	fi

# The header rule below creates both files (an initial empty table), so
# crash_symbols.c must not be generated in parallel with it: two
# mkcrashtable.py processes writing the same .tmp files is a race even
# though each final replace is atomic.
$(CRASH_SYMBOLS_C): $(CRASH_SYMBOLS_H) FORCE | build/generated
	@if [ "$(CONFIG_DEBUG)" != "1" ] || [ ! -f $(KERNEL) ]; then \
		$(call RUN_LOG,$(PYTHON) tools/mkcrashtable.py /nonexistent build/generated --empty --max $(CRASH_SYMBOL_MAX)) || { $(call LOG_ERROR,Failed to create the crash symbol table); exit 1; }; \
	else \
		$(call RUN_LOG,$(PYTHON) tools/mkcrashtable.py $(KERNEL) build/generated --max $(CRASH_SYMBOL_MAX)) || { $(call LOG_ERROR,Failed to create the crash symbol table); exit 1; }; \
	fi

$(CRASH_SYMBOLS_OBJ): $(CRASH_SYMBOLS_C) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(CC) $(CFLAGS) -c -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

$(KERNEL): $(KERNEL_OBJS)
	@$(call LOG_COMPILE,kernel objects,$@)
	@warn_log=$$(mktemp); \
	if $(LD) -m elf_i386 $(LDFLAGS) -o $@ $(KERNEL_OBJS) 2>$$warn_log; then \
		if [ -s $$warn_log ]; then \
			cat $$warn_log >> $(LOG_FILE); \
			while IFS= read -r warning_line; do $(call LOG_WARNING,$$warning_line); done < $$warn_log; \
		fi; \
		rm -f $$warn_log; \
	else \
		cat $$warn_log >> $(LOG_FILE); \
		while IFS= read -r error_line; do $(call LOG_ERROR,$$error_line); done < $$warn_log; \
		rm -f $$warn_log; \
		$(call LOG_ERROR,Failed to link $@); \
		exit 1; \
	fi
	@# Pass 2: generate the real ram_requirement.h from the linked kernel,
	@# rebuild kernel.o and relink. The values come from the kernel's
	@# actual __bss_end symbol.
	@#
	@# The generator is invoked directly rather than through its target:
	@# after a clean build the header ALREADY exists - it is the pre-link
	@# stub kernel.o had to compile against - and it is newer than the
	@# rule's prerequisites, so make would call the target up to date and
	@# the kernel would keep the stub's gate forever. That is what made a
	@# low-memory machine fail even though the real span fit it.
	@# gen_rr.sh content-compares, so kernel.o is recompiled (and relinked)
	@# only when the real span actually differs from the stub.
	@bash tools/gen_rr.sh $(KERNEL) $(RAM_REQUIREMENT_H) $(DOOM_WAD_EMBED_STATE) || { $(call LOG_ERROR,Failed to generate $(RAM_REQUIREMENT_H)); exit 1; }
	@$(MAKE) --no-print-directory build/kernel.o
	@$(call RUN_LOG,$(LD) -m elf_i386 $(LDFLAGS) -o $@ $(KERNEL_OBJS)) || { $(call LOG_ERROR,Failed to link $@); exit 1; }
ifeq ($(CONFIG_DEBUG),1)
	@# Pass 3: refresh the symbol table from the just-linked kernel,
	@# rebuild only that object, relink. The table always has
	@# CRASH_SYMBOL_MAX slots, so the layout is byte-identical to pass 1
	@# and the amalgamation never needs a second compile.
	@$(MAKE) --no-print-directory $(CRASH_SYMBOLS_C)
	@$(MAKE) --no-print-directory $(CRASH_SYMBOLS_OBJ)
	@$(call RUN_LOG,$(LD) -m elf_i386 $(LDFLAGS) -o $@ $(KERNEL_OBJS)) || { $(call LOG_ERROR,Failed to link $@); exit 1; }
endif

build/isodir/boot/loader.elf: $(LOADER) | build/isodir/boot/grub
	@$(call LOG_COMPILE,$<,$@)
	@# A debug build links the loader with full DWARF for the crash-symbol
	@# table (about 0.9 MiB). GRUB reads that whole file into its heap while
	@# loading the multiboot segments, and on a 5 MB machine the extra 0.9 MiB
	@# is what pushes it over the edge: 'error: out of memory.' and no kernel
	@# module. The symbols live in the generated table compiled into the ELF,
	@# not in the debug sections, so strip those for the boot medium and keep
	@# the unstripped build/loader.elf for symbol tables.
	@$(OBJCOPY) --strip-debug $(LOADER) $@ || { $(call LOG_ERROR,Failed to strip $(LOADER) for the ISO); exit 1; }

build/isodir/boot/kernel.bin.zst: $(KERNEL_ZST) | build/isodir/boot/grub
	@$(call LOG_COMPILE,$<,$@)
	@cp $(KERNEL_ZST) $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }

# WAD multiboot module: GRUB loads it via BIOS, so the kernel finds the
# game data in RAM on any boot media (USB stick, CD, disk).
# The guard, the work and the [OK] line live in ONE shell (a single
# backslash-joined recipe line) on purpose. Split into separate lines, a
# missing WAD would only end the guard's own shell - make would then run the
# remaining lines, write a 4-byte stub and fail on the stat of an empty path.
build/doom.wadmodule: $(DOOM_WAD) Makefile | build
	@if [ -z "$(DOOM_WAD)" ]; then \
		$(call LOG_WARNING,DOOM WAD not found - skipping the DOOM multiboot module); \
		exit 0; \
	fi; \
	$(call LOG_COMPILE,$(DOOM_WAD),WAD multiboot module $@); \
	printf 'DWAD' > $@ || exit 1; \
	python3 -c "import struct; open('$@','ab').write(struct.pack('<III', $$(stat -c %s $(DOOM_WAD)), 0, 0))" || exit 1; \
	cat $(DOOM_WAD) >> $@ || exit 1; \
	$(call LOG_OK,WAD multiboot module ready: $@)

build/isodir/boot/doom.wadmodule: build/doom.wadmodule | build/isodir/boot/grub
	@if [ ! -f $< ]; then \
		$(call LOG_WARNING,No WAD module to stage - the ISO ships without DOOM game data); \
		exit 0; \
	fi; \
	$(call LOG_COMPILE,$<,$@); \
	cp $< $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }

build/isodir/boot/grub/grub.cfg: src/config/config.h $(DOOM_WAD_MODULE_DEP) $(DOOM_WAD_MODULE_STAMP) Makefile | build/isodir/boot/grub
	@$(call LOG_COMPILE,boot menu,$@)
	@printf '%s\n' 'set timeout_style=hidden' 'set timeout=$(GRUB_TIMEOUT)' 'set default=0' 'terminal_output console' $(GRUB_TRACE_TOP) $(GRUB_SERIAL_SETUP) 'menuentry "HaloxOS!" {' $(GRUB_TRACE_ENTRY) $(GRUB_VIDEO_SETUP) $(GRUB_TRACE_VBE) '    set gfxpayload=$(GFXPAYLOAD)' $(GRUB_TRACE_MODE) '    insmod gzio' '    multiboot /boot/loader.elf halox.boot=$$root' $(GRUB_TRACE_LOADER) '    module /boot/kernel.bin.zst' $(DOOM_MODULE_CFG) $(GRUB_TRACE_MODULES) '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

# Kernel size report: prints the actual kernel image/memory numbers in
# the same blue info colour as the rest of the build log, right before the
# Build Timelapse line. Uses stat/nm only (tools already required).
#  - kernel bytes:      size of kernel.flat (what gets decompressed)
#  - compressed bytes:  size of kernel.bin.zst (what ships on the media)
#  - memory bytes:      highest physical byte the boot chain occupies:
#                       __bss_end counted from address 0, because GRUB, the
#                       loader and the compressed module live below the
#                       kernel's 0x200000 load base too
define KERNEL_SIZE_REPORT
	flat_kb=$$(stat -c '%s' $(KERNEL_FLAT)); \
	zst_kb=$$(stat -c '%s' $(KERNEL_ZST)); \
	mem_end=$$(nm $(KERNEL) | awk '/ B __bss_end$$/ {print "0x"$$1}'); \
	mem_bytes=$$(python3 -c "print(int('$$mem_end', 16))"); \
	flat_mb=$$(python3 -c "print('%.5f' % ($$flat_kb/1048576))"); \
	zst_kb_h=$$(python3 -c "print('%.5f' % ($$zst_kb/1024))"); \
	ratio=$$(python3 -c "print('%.5f' % ($$zst_kb*100/$$flat_kb))"); \
	mem_mb=$$(python3 -c "print('%.5f' % ($$mem_end/1048576))"); \
	printf '%b\n%b%s%b\n%b%s%b\n' '\033[0m' '\033[92m' "** Total calculated in kernel bytes: $$flat_mb MB (Compressed: $$zst_kb_h KB, $$ratio% compressed ratio)" '\033[0m' '\033[92m' "** Total calculated in kernel memory bytes: $$mem_mb MB" '\033[0m'; \
	if [ "$(DOOM_WAD_EMBED_STATE)" = "1" ]; then \
		ram_limit=22.0; ram_note='embedded-WAD span build: 22 MB RAM floor'; \
	else \
		ram_limit=$$(python3 -c "print(int(open('build/generated/ram_requirement.h').read().split('HALOXOS_KERNEL_RAM_REQUIRED_MB ')[1].split('u')[0]))"); ram_note="$${ram_limit} MB boot floor"; \
	fi; \
	if python3 -c "import sys; sys.exit(0 if $$mem_mb < $$ram_limit else 1)"; then \
		mem_left=$$(python3 -c "print('%.2f' % ($$ram_limit - $$mem_mb))"); \
		if python3 -c "import sys; sys.exit(0 if $$mem_left <= 0.5 else 1)"; then \
			printf '%b[WARNING!]%b %bOnly %s MB of headroom below the %s.%b\n' '$(COLOR_ORANGE)' '$(COLOR_RESET)' '$(COLOR_YELLOW)' "$$mem_left" "$$ram_note" '$(COLOR_RESET)'; \
		else \
			printf '%b[OK]%b %b%s MB of headroom below the %s%b\n' '$(COLOR_GREEN)' '$(COLOR_RESET)' '$(COLOR_BLUE)' "$$mem_left" "$$ram_note" '$(COLOR_RESET)'; \
		fi; \
	else \
		printf '%b[WARNING!]%b %bKernel size has EXCEEDED the %s!%b\n' '$(COLOR_ORANGE)' '$(COLOR_RESET)' '$(COLOR_YELLOW)' "$$ram_note" '$(COLOR_RESET)'; \
	fi
endef

# Same single-shell rule as the WAD module above: a missing WAD must skip the
# WHOLE disk, not just the guard line (the lines after it would stat an empty
# path and fail the build).
$(DOOMDISK): $(DOOM_WAD) Makefile | build
	@if [ -z "$(DOOM_WAD)" ]; then \
		$(call LOG_WARNING,DOOM WAD not found - skipping the DOOM game-data disk); \
		exit 0; \
	fi; \
	$(call LOG_COMPILE,$(DOOM_WAD),DOOM data disk (compact raw LBA $(DOOM_WAD_DATA_LBA))); \
	truncate -s $$(( ($(DOOM_WAD_DATA_LBA) + $(DOOM_WAD_SECTORS) + 16) * 512 )) $@ || { $(call LOG_ERROR,Failed to create $@); exit 1; }; \
	printf 'DWAD' > build/doom_wad_info.bin || exit 1; \
	python3 -c "import struct; open('build/doom_wad_info.bin','ab').write(struct.pack('<III', $$(stat -c %s $(DOOM_WAD)), $(DOOM_WAD_DATA_LBA), 0))" || exit 1; \
	dd if=build/doom_wad_info.bin of=$@ bs=512 seek=$(DOOM_WAD_INFO_LBA) conv=notrunc status=none || { $(call LOG_ERROR,Failed to write WAD info into $@); exit 1; }; \
	dd if=$(DOOM_WAD) of=$@ bs=512 seek=$(DOOM_WAD_DATA_LBA) conv=notrunc status=none || { $(call LOG_ERROR,Failed to write WAD into $@); exit 1; }; \
	$(call LOG_OK,DOOM data disk ready: $@)

.PHONY: doomdisk
doomdisk: | build
	@if [ -z "$(DOOM_WAD)" ]; then \
		$(call LOG_WARNING,DOOM1.WAD not found - DOOM app will show the missing-WAD error screen); \
	else \
		$(MAKE) --no-print-directory $(DOOMDISK); \
	fi

# Modules embedded in the ISO's El Torito GRUB core. A stock
# grub-mkrescue core carries only biosdisk and iso9660, then loads
# normal.mod and every other module from /boot/grub/i386-pc on the ISO
# through the firmware disk stack. On some real machines that second-stage
# load never completes and boot stops right after "Welcome to GRUB!".
# Embedding every module the core config and grub.cfg use makes GRUB
# self-contained, so only the boot payloads (loader, kernel, WAD) are read
# from the ISO. The part_* list matches the insmod lines grub-mkrescue
# writes into its own core config.
#
# grub-mkrescue must be pointed at the i386-pc platform directory (-d).
# Without it, it scans every platform under /usr/lib/grub and tries to
# build EFI cores as well, applying this same --modules list to each one.
# BIOS-only modules (biosdisk, vbe) do not exist for x86_64-efi, so a host
# with grub-efi-amd64-bin installed fails with:
#   grub-mkrescue: error: cannot open '.../x86_64-efi/biosdisk.mod'
# HaloxOS boots through BIOS El Torito, so building only i386-pc is what
# the ISO actually needs.
ISO_GRUB_DIR := /usr/lib/grub/i386-pc
ISO_GRUB_MODULES := biosdisk iso9660 fat normal echo configfile multiboot \
	search search_fs_file vbe gzio \
	part_acorn part_amiga part_apple part_bsd part_dfly part_dvh \
	part_gpt part_msdos part_plan part_sun part_sunpc \
	$(GRUB_CORE_SERIAL_MODULE)

$(ISO): build/isodir/boot/loader.elf build/isodir/boot/kernel.bin.zst $(DOOM_WAD_MODULE_DEP) build/isodir/boot/grub/grub.cfg $(DOOM_WAD) build/bios-serial.template tools/fatpatch.py Makefile | $(OUT_DIR)
	@$(call LOG_COMPILE,build/isodir,$@)
	@if [ "$(DOOM_WAD_MODULE_BOOT)" != "1" ]; then rm -f build/isodir/boot/doom.wadmodule; fi
	@$(call RUN_LOG,grub-mkrescue -d $(ISO_GRUB_DIR) --modules='$(ISO_GRUB_MODULES)' -o $@ build/isodir) || { $(call LOG_ERROR,Failed to generate $@); exit 1; }
	@# ISO tail, in order: [pad to 1 MiB alignment][embedded FAT12
	@# volume carrying the pre-allocated bios-serial.log][DOOM WAD blob
	@# + info when a WAD is present]. The FAT volume is registered as
	@# MBR partition entry 2, so a stick the ISO is dd'd onto shows
	@# bios-serial.log to any host with no extraction step; the kernel
	@# locates it at boot through the MBR and writes it via BIOS INT 13h.
	@iso_end=$$(( ($$(stat -c %s $@) + 2047) / 2048 )); \
	fat_blk=$$(( ( (iso_end + 511) / 512 ) * 512 )); \
	wad_blk=$$(( fat_blk + $(BOOTLOG_FAT_BLOCKS) )); \
	if [ -n "$(DOOM_WAD)" ]; then \
		tail_blks=$$(( wad_blk + $(DOOM_WAD_CD_SECTORS) + 1 )); \
	else \
		tail_blks=$$wad_blk; \
	fi; \
	truncate -s $$(( tail_blks * 2048 )) $@ || { $(call LOG_ERROR,Failed to reserve the boot-log volume in $@); exit 1; }; \
	$(call LOG_COMPILE,bios-serial.log volume,embedded FAT12 at block $$fat_blk); \
	truncate -s $(BOOTLOG_FAT_KB)K build/bootlog-fat.img || exit 1; \
	mkfs.fat -F 12 -n HALOXLOG build/bootlog-fat.img >/dev/null 2>&1 || { $(call LOG_ERROR,Failed to format the bios-serial.log volume); exit 1; }; \
	mcopy -i build/bootlog-fat.img build/bios-serial.template ::/bios-serial.log || { $(call LOG_ERROR,Failed to place bios-serial.log in its volume); exit 1; }; \
	$(PYTHON) tools/fatpatch.py zero-size build/bootlog-fat.img 0 || { $(call LOG_ERROR,Failed to prepare the bios-serial.log volume); exit 1; }; \
	dd if=build/bootlog-fat.img of=$@ bs=2048 seek=$$fat_blk conv=notrunc status=none || { $(call LOG_ERROR,Failed to embed the bios-serial.log volume into $@); exit 1; }; \
	$(PYTHON) tools/fatpatch.py mbr $@ $$(( fat_blk * 4 )) $$(( $(BOOTLOG_FAT_KB) * 2 )) || { $(call LOG_ERROR,Failed to register the boot-log volume in the MBR); exit 1; }; \
	if [ -n "$(DOOM_WAD)" ]; then \
		$(call LOG_COMPILE,$(DOOM_WAD),DOOM WAD blob (ISO tail, 2048B sectors)); \
		printf 'DWAD' > build/doom_wad_info_cd.bin || exit 1; \
		python3 -c "import struct; open('build/doom_wad_info_cd.bin','ab').write(struct.pack('<III', $$(stat -c %s $(DOOM_WAD)), $$wad_blk, 0))" || exit 1; \
		dd if=$(DOOM_WAD) of=$@ bs=2048 seek=$$wad_blk conv=notrunc status=none || exit 1; \
		dd if=build/doom_wad_info_cd.bin of=$@ bs=2048 seek=$$((wad_blk + $(DOOM_WAD_CD_SECTORS))) conv=notrunc status=none || exit 1; \
	else \
		$(call LOG_WARNING,DOOM1.WAD not found - DOOM app will show the missing-WAD error screen); \
	fi
	@$(call PUBLISH_OUT,$(ISO))
	@$(KERNEL_SIZE_REPORT)
	@$(call LOG_OK,Build Finished! $(ISO))

$(DISK): $(LOADER) $(KERNEL_ZST) $(DISK_CORE) $(BOOT_DISK_CFG) $(DESKTOP_LAYOUT) $(DOOM_WAD) $(DOOM_WAD_MODULE_IMAGE_DEP) build/bios-serial.template tools/fatpatch.py Makefile | build $(OUT_DIR)
	@$(call LOG_COMPILE,Empty disk image,$@)
	@truncate -s 64M $@ || { $(call LOG_ERROR,Failed to create $@); exit 1; }
	@$(call LOG_COMPILE,$@,msdos partition table)
	@warn_log=$$(mktemp); \
	if parted -s $@ unit s mklabel msdos mkpart primary $(DISK_PART_START)s 100% >$$warn_log 2>&1; then \
		if [ -s $$warn_log ]; then \
			while IFS= read -r warning_line; do $(call LOG_WARNING,$$warning_line); done < $$warn_log; \
		fi; \
		rm -f $$warn_log; \
	else \
		while IFS= read -r error_line; do $(call LOG_ERROR,$$error_line); done < $$warn_log; \
		rm -f $$warn_log; \
		$(call LOG_ERROR,Failed to partition $@); \
		exit 1; \
	fi
	@$(call LOG_COMPILE,$@,FAT16 filesystem)
	@mkfs.fat -F 16 --offset=$(DISK_PART_START) -h $(DISK_PART_START) -n HALOXOSHD $@ >/dev/null 2>&1 || { $(call LOG_ERROR,Failed to format $@); exit 1; }
	@$(call LOG_COMPILE,boot directories,$@)
	@mmd -i $@@@$(DISK_PART_OFFSET) ::/boot ::/boot/grub || { $(call LOG_ERROR,Failed to create boot directories in $@); exit 1; }
	@$(call LOG_COMPILE,$(BOOT_DISK_CFG),$@)
	@mcopy -i $@@@$(DISK_PART_OFFSET) $(BOOT_DISK_CFG) ::/boot/grub/grub.cfg || { $(call LOG_ERROR,Failed to copy $(BOOT_DISK_CFG) into $@); exit 1; }
	@$(call LOG_COMPILE,$(LOADER),$@)
	@# Same reason as the ISO copy: GRUB reads the whole ELF file into its
	@# heap, so the DWARF sections only cost boot RAM on a small machine.
	@$(OBJCOPY) --strip-debug $(LOADER) build/loader.disk.elf || { $(call LOG_ERROR,Failed to strip $(LOADER) for the disk); exit 1; }
	@mcopy -i $@@@$(DISK_PART_OFFSET) build/loader.disk.elf ::/boot/loader.elf || { $(call LOG_ERROR,Failed to copy $(LOADER) into $@); exit 1; }
	@rm -f build/loader.disk.elf
	@$(call LOG_COMPILE,$(KERNEL_ZST),$@)
	@mcopy -i $@@@$(DISK_PART_OFFSET) $(KERNEL_ZST) ::/boot/kernel.bin.zst || { $(call LOG_ERROR,Failed to copy $(KERNEL_ZST) into $@); exit 1; }
	@if [ "$(DOOM_WAD_MODULE_BOOT)" = "1" ] && [ -f "$(DOOM_WAD_MODULE)" ]; then \
		$(call LOG_COMPILE,$(DOOM_WAD_MODULE),$@); \
		mcopy -i $@@@$(DISK_PART_OFFSET) $(DOOM_WAD_MODULE) ::/boot/doom.wadmodule || { $(call LOG_ERROR,Failed to copy $(DOOM_WAD_MODULE) into $@); exit 1; }; \
	fi
	@$(call LOG_COMPILE,$(GRUB_BOOT_IMG),$@)
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=446 count=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot sector into $@); exit 1; }
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=1 skip=510 seek=510 count=2 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot signature into $@); exit 1; }
	@$(call LOG_COMPILE,$(DISK_CORE),$@)
	@dd if=$(DISK_CORE) of=$@ bs=512 seek=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write $(DISK_CORE) into $@); exit 1; }
	@$(call LOG_COMPILE,$(DESKTOP_LAYOUT),$@)
	@dd if=$(DESKTOP_LAYOUT) of=$@ bs=512 seek=$(DESKTOP_LAYOUT_LBA) conv=notrunc status=none || { $(call LOG_ERROR,Failed to write $(DESKTOP_LAYOUT) into $@); exit 1; }
	@# bios-serial.log: the pre-allocated template inside the FAT16
	@# partition, its directory size zeroed so a fresh disk shows an
	@# empty file the kernel then grows as it auto-saves the trace.
	@$(call LOG_COMPILE,bios-serial.log volume,FAT16 partition)
	@mcopy -i $@@@$(DISK_PART_OFFSET) build/bios-serial.template ::/bios-serial.log || { $(call LOG_ERROR,Failed to place bios-serial.log into $@); exit 1; }
	@$(PYTHON) tools/fatpatch.py zero-size $@ $(DISK_PART_OFFSET) || { $(call LOG_ERROR,Failed to prepare bios-serial.log in $@); exit 1; }
	@if [ -n "$(DOOM_WAD)" ]; then \
		$(call LOG_COMPILE,$(DOOM_WAD),DOOM game data (compact raw LBA $(DOOM_WAD_DATA_LBA))); \
		if [ $$(stat -c %s $@) -lt $(DOOM_WAD_END_BYTES) ]; then truncate -s $(DOOM_WAD_END_BYTES) $@ || exit 1; fi; \
		printf 'DWAD' > build/doom_wad_info.bin || exit 1; \
		python3 -c "import struct; open('build/doom_wad_info.bin','ab').write(struct.pack('<III', $$(stat -c %s $(DOOM_WAD)), $(DOOM_WAD_DATA_LBA), 0))" || exit 1; \
		dd if=build/doom_wad_info.bin of=$@ bs=512 seek=$(DOOM_WAD_INFO_LBA) conv=notrunc status=none || exit 1; \
		dd if=$(DOOM_WAD) of=$@ bs=512 seek=$(DOOM_WAD_DATA_LBA) conv=notrunc status=none || exit 1; \
	else \
		$(call LOG_WARNING,DOOM1.WAD not found - DOOM app will show the missing-WAD error screen); \
	fi
	@$(call PUBLISH_OUT,$(DISK))
	@$(KERNEL_SIZE_REPORT)
	@$(call LOG_OK,Disk Image Finished! $(DISK))

# ===== BIOS-bootable USB image =====
#
# A USB stick is presented to the firmware as a hard disk. The ISO is a
# hybrid image whose first partition begins at LBA 1 (there is no ordinary
# MBR gap); some BIOSes refuse that layout and stop at "GRUB loading".
# This image uses the conventional BIOS layout instead:
#   LBA 0        GRUB boot.img + an active MBR partition entry
#   LBA 1..      Grub core.img in the 1 MiB MBR gap
#   LBA 2048     FAT32 partition with /boot/grub, loader, kernel and WAD
# The core image is the same one used by the hard-disk target and embeds all
# modules needed to read the FAT partition, so GRUB does not need a
# second-stage module load from the boot medium.
$(USB): $(LOADER) $(KERNEL_ZST) $(DISK_CORE) $(BOOT_DISK_CFG) $(DOOM_WAD_MODULE_IMAGE_DEP) build/bios-serial.template tools/fatpatch.py Makefile | build $(OUT_DIR)
	@$(call LOG_COMPILE,USB image,$@)
	@truncate -s 64M $@ || { $(call LOG_ERROR,Failed to create $@); exit 1; }
	@parted -s $@ unit s mklabel msdos mkpart primary fat32 $(USB_PART_START)s $(USB_PART_END)s || { $(call LOG_ERROR,Failed to partition $@); exit 1; }
	@parted -s $@ set 1 boot on || { $(call LOG_ERROR,Failed to mark the USB partition bootable); exit 1; }
	@$(call LOG_COMPILE,$@,FAT32 filesystem)
	@rm -f build/usb-part.img
	@truncate -s $$(( $(USB_PART_SECTORS) * 512 )) build/usb-part.img || { $(call LOG_ERROR,Failed to create the USB FAT32 partition); exit 1; }
	@mkfs.fat -F 32 -h $(USB_PART_START) -n HALOXOSUSB build/usb-part.img >/dev/null 2>&1 || { $(call LOG_ERROR,Failed to format the USB FAT32 partition); exit 1; }
	@$(call LOG_COMPILE,boot directories,$@)
	@mmd -i build/usb-part.img ::/boot ::/boot/grub || { $(call LOG_ERROR,Failed to create boot directories in $@); exit 1; }
	@mcopy -i build/usb-part.img $(BOOT_DISK_CFG) ::/boot/grub/grub.cfg || { $(call LOG_ERROR,Failed to copy $(BOOT_DISK_CFG) into $@); exit 1; }
	@$(call LOG_COMPILE,$(LOADER),$@)
	@$(OBJCOPY) --strip-debug $(LOADER) build/loader.usb.elf || { $(call LOG_ERROR,Failed to strip $(LOADER) for USB); exit 1; }
	@mcopy -i build/usb-part.img build/loader.usb.elf ::/boot/loader.elf || { $(call LOG_ERROR,Failed to copy $(LOADER) into $@); exit 1; }
	@rm -f build/loader.usb.elf
	@$(call LOG_COMPILE,$(KERNEL_ZST),$@)
	@mcopy -i build/usb-part.img $(KERNEL_ZST) ::/boot/kernel.bin.zst || { $(call LOG_ERROR,Failed to copy $(KERNEL_ZST) into $@); exit 1; }
	@if [ "$(DOOM_WAD_MODULE_BOOT)" = "1" ] && [ -f "$(DOOM_WAD_MODULE)" ]; then \
		$(call LOG_COMPILE,$(DOOM_WAD_MODULE),$@); \
		mcopy -i build/usb-part.img $(DOOM_WAD_MODULE) ::/boot/doom.wadmodule || { $(call LOG_ERROR,Failed to copy $(DOOM_WAD_MODULE) into $@); exit 1; }; \
	fi
	@$(call LOG_COMPILE,$(GRUB_BOOT_IMG),$@)
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=446 count=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write the USB boot sector); exit 1; }
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=1 skip=510 seek=510 count=2 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write the USB boot signature); exit 1; }
	@$(call LOG_COMPILE,$(DISK_CORE),$@)
	@dd if=$(DISK_CORE) of=$@ bs=512 seek=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write the USB GRUB core); exit 1; }
	@dd if=build/usb-part.img of=$@ bs=512 seek=$(USB_PART_START) conv=notrunc status=none || { $(call LOG_ERROR,Failed to write the USB FAT32 partition); exit 1; }
	@$(call LOG_COMPILE,bios-serial.log volume,USB partition 2)
	@rm -f build/bootlog-usb-fat.img
	@truncate -s $(BOOTLOG_FAT_KB)K build/bootlog-usb-fat.img || { $(call LOG_ERROR,Failed to create the USB boot-log volume); exit 1; }
	@mkfs.fat -F 12 -n HALOXLOG build/bootlog-usb-fat.img >/dev/null 2>&1 || { $(call LOG_ERROR,Failed to format the USB boot-log volume); exit 1; }
	@mcopy -i build/bootlog-usb-fat.img build/bios-serial.template ::/bios-serial.log || { $(call LOG_ERROR,Failed to place bios-serial.log in the USB image); exit 1; }
	@$(PYTHON) tools/fatpatch.py zero-size build/bootlog-usb-fat.img 0 || { $(call LOG_ERROR,Failed to prepare bios-serial.log in the USB image); exit 1; }
	@dd if=build/bootlog-usb-fat.img of=$@ bs=512 seek=$(USB_LOG_START) conv=notrunc status=none || { $(call LOG_ERROR,Failed to embed the USB boot-log volume); exit 1; }
	@$(PYTHON) tools/fatpatch.py mbr $@ $(USB_LOG_START) $(USB_LOG_SECTORS) || { $(call LOG_ERROR,Failed to register the USB boot-log volume); exit 1; }
	@$(call PUBLISH_OUT,$(USB))
	@$(KERNEL_SIZE_REPORT)
	@$(call LOG_OK,USB Image Finished! $(USB))

# ===== native disk images for other hypervisors =====
#
# `make disk` leaves a raw image. The targets below convert that same image
# with qemu-img (installed with QEMU) into the format each hypervisor
# expects, keeping the raw image untouched:
#
#   make disk vmware       -> build/out/HaloxOS-Disk_1.1.vmdk  (VMware Workstation/Player, ESXi)
#   make disk qemu         -> build/out/HaloxOS-Disk_1.1.qcow2 (QEMU/KVM, Proxmox)
#   make disk hyper-v      -> build/out/HaloxOS-Disk_1.1.vhd   (Hyper-V Gen 1, Virtual PC)
#   make disk virtualbox   -> build/out/HaloxOS-Disk_1.1.vdi   (VirtualBox)
#
# Short spellings: vmw/vmdk, qcow/qcow2/qem, hv/hyperv/vhd, vb/vbox/vdi.
# Each format target depends on `disk`, so `make vmware` works as a one-shot
# build+convert too. The conversion writes <output>.tmp and renames it only
# after qemu-img succeeds, so a failed conversion never leaves a half image
# behind.
#
# The macro takes the output file as its last argument. The alias recipes
# below call it directly instead of invoking a sub-make: `$(DISK)` depends on
# the FORCE-driven core image, so a sub-make would rebuild the whole raw disk
# a second time after the parent `disk` target had just built it.
define CONVERT_DISK
if ! command -v "$(QEMU_IMG)" >/dev/null 2>&1 && [ ! -x "$(QEMU_IMG)" ]; then \
	$(call LOG_ERROR,qemu-img was not found - it is needed to create $(1) disk images); \
	$(call LOG_DEP,Debian/Ubuntu/WSL: sudo apt-get install qemu-utils); \
	$(call LOG_DEP,Arch Linux: sudo pacman -S qemu-img); \
	$(call LOG_DEP,Windows: install QEMU from https://qemu.weilnetz.de/w64/); \
	exit 1; \
fi; \
$(call LOG_COMPILE,$(DISK),$(5)); \
rm -f $(5).tmp; \
if ! "$(QEMU_IMG)" convert -f raw -O $(2) $(3) $(DISK) $(5).tmp; then \
	rm -f $(5).tmp; \
	$(call LOG_ERROR,Failed to convert $(DISK) to $(5)); \
	exit 1; \
fi; \
mv -f $(5).tmp $(5) || { $(call LOG_ERROR,Failed to install $(5)); exit 1; }; \
$(call PUBLISH_OUT,$(5)); \
$(call LOG_OK,$(4) image ready: $(5))
endef

$(DISK_VMDK): $(DISK) Makefile
	@$(call CONVERT_DISK,VMware,vmdk,-o subformat=monolithicSparse,VMware VMDK,$@)

$(DISK_QCOW2): $(DISK) Makefile
	@$(call CONVERT_DISK,QEMU,qcow2,,QEMU qcow2,$@)

$(DISK_VHD): $(DISK) Makefile
	@$(call CONVERT_DISK,Hyper-V,vpc,-o subformat=dynamic,Hyper-V VHD,$@)

$(DISK_VDI): $(DISK) Makefile
	@$(call CONVERT_DISK,VirtualBox,vdi,,VirtualBox VDI,$@)

.PHONY: vmware vmw vmdk qemu qcow qcow2 qem hyper-v hyperv hv vhd virtualbox vb vbox vdi
vmware vmw vmdk: disk
	@$(call CONVERT_DISK,VMware,vmdk,-o subformat=monolithicSparse,VMware VMDK,$(DISK_VMDK))

qemu qcow qcow2 qem: disk
	@$(call CONVERT_DISK,QEMU,qcow2,,QEMU qcow2,$(DISK_QCOW2))

hyper-v hyperv hv vhd: disk
	@$(call CONVERT_DISK,Hyper-V,vpc,-o subformat=dynamic,Hyper-V VHD,$(DISK_VHD))

virtualbox vb vbox vdi: disk
	@$(call CONVERT_DISK,VirtualBox,vdi,,VirtualBox VDI,$(DISK_VDI))

$(FLOPPY): $(LOADER) $(KERNEL_ZST) $(FLOPPY_CORE) $(BOOT_FLOPPY_CFG) | build $(OUT_DIR)
	@core_sectors=$$((($$(stat -c %s $(FLOPPY_CORE)) + 511) / 512)); \
	if [ $$core_sectors -ge $(FLOPPY_PART_START) ]; then \
		$(call LOG_ERROR,Floppy core image is too large: $$core_sectors sectors); \
		exit 1; \
	fi
	@$(call LOG_COMPILE,Empty floppy image,$@)
	@truncate -s 1474560 $@ || { $(call LOG_ERROR,Failed to create $@); exit 1; }
	@$(call LOG_COMPILE,$@,msdos partition table)
	@warn_log=$$(mktemp); \
	if parted -s $@ unit s mklabel msdos mkpart primary $(FLOPPY_PART_START)s 2879s >$$warn_log 2>&1; then \
		if [ -s $$warn_log ]; then \
			while IFS= read -r warning_line; do $(call LOG_WARNING,$$warning_line); done < $$warn_log; \
		fi; \
		rm -f $$warn_log; \
	else \
		while IFS= read -r error_line; do $(call LOG_ERROR,$$error_line); done < $$warn_log; \
		rm -f $$warn_log; \
		$(call LOG_ERROR,Failed to partition $@); \
		exit 1; \
	fi
	@$(call LOG_COMPILE,$@,FAT12 filesystem)
	@mkfs.fat -F 12 --offset=$(FLOPPY_PART_START) -h $(FLOPPY_PART_START) -n HALOXOSFD $@ >/dev/null 2>&1 || { $(call LOG_ERROR,Failed to format $@); exit 1; }
	@$(call LOG_COMPILE,boot directories,$@)
	@mmd -i $@@@$(FLOPPY_PART_OFFSET) ::/boot ::/boot/grub || { $(call LOG_ERROR,Failed to create boot directories in $@); exit 1; }
	@$(call LOG_COMPILE,$(BOOT_FLOPPY_CFG),$@)
	@mcopy -i $@@@$(FLOPPY_PART_OFFSET) $(BOOT_FLOPPY_CFG) ::/boot/grub/grub.cfg || { $(call LOG_ERROR,Failed to copy $(BOOT_FLOPPY_CFG) into $@); exit 1; }
	@$(call LOG_COMPILE,$(LOADER),$@)
	@# A debug build links the loader with full DWARF for the crash-symbol
	@# table (about 0.9 MiB). That table is already baked into the ELF; the
	@# debug sections are not needed at boot, and the FAT12 partition is only
	@# about 1.26 MiB. Strip a copy for the floppy so loader + kernel still
	@# fit; the unstripped build/loader.elf stays available for symbol tables.
	@$(OBJCOPY) --strip-debug $(LOADER) build/loader.floppy.elf || { $(call LOG_ERROR,Failed to strip $(LOADER) for the floppy); exit 1; }
	@mcopy -i $@@@$(FLOPPY_PART_OFFSET) build/loader.floppy.elf ::/boot/loader.elf || { $(call LOG_ERROR,Failed to copy $(LOADER) into $@); exit 1; }
	@$(call LOG_COMPILE,$(KERNEL_ZST),$@)
	@mcopy -i $@@@$(FLOPPY_PART_OFFSET) $(KERNEL_ZST) ::/boot/kernel.bin.zst || { $(call LOG_ERROR,Failed to copy $(KERNEL_ZST) into $@); exit 1; }
	@rm -f build/loader.floppy.elf
	@$(call LOG_COMPILE,$(GRUB_BOOT_IMG),$@)
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=446 count=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot sector into $@); exit 1; }
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=1 skip=510 seek=510 count=2 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot signature into $@); exit 1; }
	@$(call LOG_COMPILE,$(FLOPPY_CORE),$@)
	@dd if=$(FLOPPY_CORE) of=$@ bs=512 seek=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write $(FLOPPY_CORE) into $@); exit 1; }
	@$(call PUBLISH_OUT,$(FLOPPY))
	@$(KERNEL_SIZE_REPORT)
	@$(call LOG_OK,Floppy Image Finished! $(FLOPPY))

# Explicit media launchers. These are useful for testing each WAD path
# independently instead of hiding a disk-read problem behind the normal
# all-in-one runner.
run-iso: $(ISO)
	@$(SERIAL_LOG_RESET)
	@$(QEMU_BIN) -cdrom $(ISO) $(SERIAL_LOG_ARGS) -m 16 -vga std
	@$(SERIAL_LOG_DONE)

run-disk: $(DISK)
	@$(SERIAL_LOG_RESET)
	@$(QEMU_BIN) -drive file=$(DISK),format=raw,if=ide,index=0,media=disk -boot c $(SERIAL_LOG_ARGS) -m 16 -vga std
	@$(SERIAL_LOG_DONE)

# A 1.44 MB floppy cannot contain a 4.0 MB DOOM WAD. Keep the boot floppy
# unchanged and attach the compact WAD disk as a second IDE device.
ifeq ($(DOOM_WAD),)
# Without a WAD there is no second-drive image to attach, and QEMU would
# refuse the run over a missing file - so the floppy boots on its own.
RUN_FLOPPY_DOOM_DEP :=
RUN_FLOPPY_DOOM_ARGS :=
else
RUN_FLOPPY_DOOM_DEP := $(DOOMDISK)
RUN_FLOPPY_DOOM_ARGS := -drive file=$(DOOMDISK),format=raw,if=ide,index=1,media=disk
endif

run-floppy: $(FLOPPY) $(RUN_FLOPPY_DOOM_DEP)
	@$(SERIAL_LOG_RESET)
	@$(QEMU_BIN) -fda $(FLOPPY) $(RUN_FLOPPY_DOOM_ARGS) -boot a $(SERIAL_LOG_ARGS) -m 16 -vga std
	@$(SERIAL_LOG_DONE)

# USB stick boot: boot the dedicated BIOS USB image through EHCI, the same
# path a real USB stick uses. `run-usb-iso` keeps the older ISO-hybrid view.
# Attach it to EHCI, not the default UHCI controller: QEMU's UHCI model
# times out on bulk transfers (the same failure mode as `-usb`), which
# hides real boot problems behind emulation noise.  Real machines use
# EHCI/xHCI for USB 2.0/3.0 sticks.
run-usb: $(USB)
	@$(SERIAL_LOG_RESET)
	@$(QEMU_BIN) -device usb-ehci,id=ehci -drive if=none,id=usbmem,format=raw,file=$(USB) -device usb-storage,drive=usbmem,bus=ehci.0,bootindex=0 $(SERIAL_LOG_ARGS) -m 16 -vga std
	@$(SERIAL_LOG_DONE)

# Keep the ISO hybrid path available for checking the CD/USB-hybrid image.
run-usb-iso: $(ISO)
	@$(SERIAL_LOG_RESET)
	@$(QEMU_BIN) -device usb-ehci,id=ehci -drive if=none,id=usbmem,format=raw,file=$(ISO) -device usb-storage,drive=usbmem,bus=ehci.0,bootindex=0 $(SERIAL_LOG_ARGS) -m 16 -vga std
	@$(SERIAL_LOG_DONE)

# Interactive QEMU launcher: asks for memory size and VGA type, then runs
# the built ISO. Works on both Windows (MSYS2/MinGW shells with QEMU from
# qemu.org or w64devkit) and Linux. Attaches the DOOM data disk as a
# second IDE drive when DOOM1.WAD is present (raw WAD stamp, see
# DOOMDISK above).
run: $(ISO)
ifeq ($(HOST_OS),windows)
	@if [ -z "$(QEMU_BIN)" ]; then \
		$(call LOG_ERROR,QEMU was not found on this Windows system); \
		$(call LOG_DEP,Install QEMU from https://qemu.weilnetz.de/w64/ then retry); \
		exit 1; \
	fi
else
	@if ! command -v $(QEMU_BIN) >/dev/null 2>&1; then \
		$(call LOG_ERROR,qemu-system-i386 is not installed); \
		$(call LOG_DEP,Install it with your package manager (e.g. qemu-system-x86)); \
		exit 1; \
	fi
endif
	@$(call LOG_DEP,Detected OS: $(HOST_OS) | QEMU: $(QEMU_BIN))
	@$(SERIAL_LOG_RESET)
	@if [ -n "$(DOOM_WAD)" ]; then \
		$(MAKE) --no-print-directory $(DOOMDISK) >/dev/null 2>&1 || true; \
	fi
	@if [ -n "$(DOOM_WAD)" ] && [ -f $(DOOMDISK) ]; then \
		doom_drive="-drive file=$(DOOMDISK),format=raw,if=ide,index=1,media=disk"; \
		$(call LOG_DEP,DOOM data disk attached: $(DOOMDISK) (primary slave)); \
	else \
		doom_drive=""; \
		$(call LOG_DEP,DOOM1.WAD not found - DOOM app will show the missing-WAD error screen); \
	fi; \
	printf 'Set to Memory Size [default: 16]: '; \
	read mem_input || mem_input=""; \
	mem="$${mem_input:-16}"; \
	case "$$mem" in \
		''|*[!0-9]*) $(call LOG_ERROR,Invalid memory size: $$mem); exit 1;; \
	esac; \
	if [ "$$mem" -lt 1 ] || [ "$$mem" -gt 2048 ]; then \
		$(call LOG_ERROR,Memory size must be between 1 and 2048 MB); exit 1; \
	fi; \
	printf 'Set to VGA Type [std, qxl, vmware, virtio, cirrus, none] [default: std]: '; \
	read vga_input || vga_input=""; \
	vga="$${vga_input:-std}"; \
	case "$$vga" in \
		std|qxl|vmware|virtio|cirrus|none) ;; \
		*) $(call LOG_ERROR,Unknown VGA type: $$vga); \
		   printf '%b%s%b\n' '\033[94m' 'Types: std, qxl, vmware, virtio, cirrus, none' '\033[0m'; exit 1;; \
	esac; \
	$(call LOG_DEP,Launching QEMU: $$mem MB RAM, VGA $$vga); \
	"$(QEMU_BIN)" $(QEMU_ARGS) $$doom_drive -m $$mem -vga $$vga
	@$(SERIAL_LOG_DONE)

# Low-RAM test build: rebuilds the ISO with the loader's "not enough RAM"
# BOOT ERROR screen forced at boot (fake RAM shortage), so it can be
# verified by booting the ISO on any machine - including real hardware
# with plenty of RAM. No emulator is launched. GNU make cannot have a
# target name starting with '-', so the dash spellings need a `--`
# separator: `make -- -noram`, `make -- --noram`, `make -- -nr`,
# `make -- -nomemory`, `make -- -nm`. The dashless spellings
# `make noram`, `make nr`, `make nomemory`, `make nm` work directly.
-noram --noram -nr --nr -nomemory --nomemory -nm --nm noram nr nomemory nm:
	@$(call LOG_WARNING,Test build: BOOT ERROR RAM screen forced in the loader)
	@$(MAKE) --no-print-directory NORAM_TEST=1 $(ISO)

serial:
	@:

# Real-hardware counterpart of the QEMU serial capture: logs the target
# machine's physical COM1 to the same build/bios-serial.log. The OS
# cannot write the log to its own boot medium (no USB mass-storage stack,
# read-only CDs, raw-sector-only ATA), so a second machine catches the
# trace over a null-modem cable or a USB-serial adapter instead:
#
#   make capture PORT=/dev/ttyUSB0      (Linux capture machine)
#   make capture PORT=COM3             (Windows capture machine)
#
# Works for a target booted from any medium (USB stick, real disk,
# floppy, CD): the serial trace is identical, only the capture side
# writes the file. BAUD=... overrides for slower UARTs. Ctrl+C stops the
# capture and reports where the log landed.
BAUD ?= 115200
capture:
	@if [ -z "$(PORT)" ]; then \
		$(call LOG_ERROR,PORT is required: make capture PORT=/dev/ttyUSB0 or PORT=COM3); \
		exit 1; \
	fi
	@mkdir -p build
	@$(PYTHON) tools/serial-capture.py "$(PORT)" --baud $(BAUD) --out $(SERIAL_LOG)

clean:
	@# Windows tools (editors, MCP servers, shells) can keep an open handle
	@# on the build directory itself. Remove every artifact, then try the
	@# now-empty directory; only fail if something is actually left behind.
	@rm -rf build 2>/dev/null || true
	@rmdir build 2>/dev/null || true
	@if [ -e build ] && { [ ! -d build ] || [ -n "$$(ls -A build 2>/dev/null)" ]; }; then \
		printf '%b[ERROR!]%b Failed to remove build artifacts\n' '\033[31m' '\033[0m'; \
		exit 1; \
	fi
	@$(call LOG_CLEAN_OK,Clean Done!)


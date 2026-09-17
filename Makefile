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
$(shell : > $(LOG_FILE))

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
MAKEFLAGS += -j$(JOBS)

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
ASFLAGS := -DHALOXOS_BOOT_SCREEN_WIDTH=$(CONFIG_SCREEN_WIDTH) -DHALOXOS_BOOT_SCREEN_HEIGHT=$(CONFIG_SCREEN_HEIGHT) -DHALOXOS_BOOT_SCREEN_DEPTH=$(CONFIG_SCREEN_DEPTH)
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
# Debug builds keep EBP frame pointers so the crash handler can walk the
# stack chain, embed debug line info for the BSOD backtrace symbol table,
# and get the addr2line symbol data generated after the final link.
# Release builds omit the frame pointer: one less register spill per call
# in the rendering hot paths.
ifeq ($(CONFIG_DEBUG),1)
# -g1 = line tables only. The crash handler needs file:line (addr2line)
# and the EBP chain needs frame pointers; full -g type/debug info is not
# read anywhere, costs megabytes of object file I/O and slows every
# compile and link. -g1 keeps the whole backtrace feature and builds far
# faster.
CFLAGS += -fno-omit-frame-pointer -g1
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

QEMU_ARGS = -cdrom $(ISO)
ifneq ($(filter serial,$(MAKECMDGOALS)),)
QEMU_ARGS += -serial stdio -monitor none
endif

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
ARCH_PACKAGES := nasm gcc binutils grub xorriso pkgconf libpng dosfstools parted mtools zstd python3 qemu-system-x86
DEBIAN_PACKAGES := nasm gcc gcc-multilib binutils grub-pc-bin grub-common xorriso pkg-config libpng-dev dosfstools parted mtools zstd python3 qemu-system-x86

ifneq ($(filter disk,$(MAKECMDGOALS)),)
BUILD_MEDIA := Hard Disk
else ifneq ($(filter floppy,$(MAKECMDGOALS)),)
BUILD_MEDIA := Floppy
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
ISO := build/HaloxOS-LiveCD_DEV.iso
DISK := build/HaloxOS-Disk_DEV.img
FLOPPY := build/HaloxOS-Floppy_DEV.img
KERNEL := build/kernel.bin
KERNEL_ZST := build/kernel.bin.zst
LOADER := build/loader.elf
# Debug-build crash-handler symbol table (see the rule near $(KERNEL)):
# fixed-size so embedding the data never shifts kernel addresses.
CRASH_SYMBOLS_H := build/generated/crash_symbols.h
CRASH_SYMBOLS_C := build/generated/crash_symbols.c
CRASH_SYMBOLS_OBJ := build/crash_symbols.o
CRASH_SYMBOL_MAX := 400
RAM_REQUIREMENT_H := build/generated/ram_requirement.h
LOADER_OBJS := \
build/loader_boot.o \
build/loader_main.o \
build/loader_zstd.o \
build/loader_shim.o

LOADER_FLAGS := -std=gnu11 $(OPT_LD) -Wall -Wextra -fdiagnostics-color=always -ffreestanding -fno-stack-protector -fno-pic -m32 -march=i386 -fno-asynchronous-unwind-tables -Iloader/include -Ibuild/generated -DHALOXOS_BOOT_SCREEN_DEPTH=$(CONFIG_SCREEN_DEPTH) -DHALOXOS_BOOT_SCREEN_WIDTH=$(CONFIG_SCREEN_WIDTH) -DHALOXOS_BOOT_SCREEN_HEIGHT=$(CONFIG_SCREEN_HEIGHT) -DHALOXOS_FORCE_RAM_ERROR=$(NORAM_TEST)
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
# it through the boot filesystem, so USB/Ventoy boots do not depend on ATA/ATAPI
# visibility after the kernel starts. The loader relocates it safely in RAM.
# Keep it ON by default for ISO/disk builds; the floppy target explicitly turns
# it off because a 1.44 MB floppy cannot contain a 4 MB WAD module.
DOOM_WAD_MODULE_BOOT ?= 1
# Multiboot module: GRUB loads the WAD into RAM via BIOS, so the kernel
# reads it straight from memory on ANY boot media (USB/Ventoy, CD, disk)
# - no ATA/ATAPI needed on real hardware. The loader relocates it above
# the kernel memory span before decompressing (WAD_MODULE_RELOC_ADDR).
DOOM_WAD_MODULE := build/doom.wadmodule
ifeq ($(DOOM_WAD_MODULE_BOOT),1)
DOOM_MODULE_CFG := '    module /boot/doom.wadmodule'
DOOM_WAD_MODULE_DEP := build/isodir/boot/doom.wadmodule
else
DOOM_MODULE_CFG :=
DOOM_WAD_MODULE_DEP :=
endif

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
build/program_icon_asset.o \
build/settings_icon_asset.o \
build/explorer_icon_asset.o \
build/taskmgr_icon_asset.o \
build/mines_icon_asset.o \
build/snake_icon_asset.o \
build/guessnum_icon_asset.o \
build/paint_icon_asset.o \
	build/power_icon_asset.o \
	build/run_icon_asset.o \
	build/box3d_icon_asset.o \
	build/firecracker_icon_asset.o \
	build/doom_icon_asset.o \
	build/run_player_asset.o \
	build/run_player_died_asset.o \
build/run_player16_asset.o \
build/run_coin1_asset.o \
build/run_coin2_asset.o \
build/run_coin3_asset.o \
build/run_coin4_asset.o \
build/run_brick_asset.o \
build/run_skull_asset.o \

KERNEL_ENTRY := src/kernel/system/kernel.c
KERNEL_FRAGMENTS := $(shell find src/driver src/kernel src/modes -name '*.c' ! -path '$(KERNEL_ENTRY)' | sort)
KERNEL_HEADERS := src/config/config.h
KERNEL_SRC := $(KERNEL_ENTRY) $(KERNEL_FRAGMENTS) $(KERNEL_HEADERS)

.PHONY: all clean run run-iso run-disk run-floppy serial disk floppy build-intro install-deps FORCE
all: build-intro check-build install-deps $(ISO)
	@$(call LOG_OK,Build completed successfully!)

disk: build-intro check-build install-deps $(DISK)

floppy: build-intro check-build install-deps
	@$(call LOG_WARNING,Floppy target: kernel-embedded WAD disabled so the kernel fits a 1.44 MB floppy)
	@$(MAKE) --no-print-directory DOOM_EMBED_WAD=0 DOOM_WAD_MODULE_BOOT=0 $(FLOPPY)

build-intro:
	@$(call LOG_INTRO)

install-deps:
	@missing=0; \
	command -v nasm >/dev/null 2>&1 || missing=1; \
	command -v gcc >/dev/null 2>&1 || missing=1; \
	command -v ld >/dev/null 2>&1 || missing=1; \
	command -v grub-mkrescue >/dev/null 2>&1 || missing=1; \
	command -v grub-mkimage >/dev/null 2>&1 || missing=1; \
	command -v xorriso >/dev/null 2>&1 || missing=1; \
	if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists libpng; then missing=1; fi; \
	command -v mkfs.fat >/dev/null 2>&1 || missing=1; \
	command -v parted >/dev/null 2>&1 || missing=1; \
	command -v mmd >/dev/null 2>&1 || missing=1; \
 	command -v mcopy >/dev/null 2>&1 || missing=1; \
	command -v zstd >/dev/null 2>&1 || missing=1; \
	command -v $(PYTHON) >/dev/null 2>&1 || missing=1; \
	command -v qemu-system-i386 >/dev/null 2>&1 || missing=1; \
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

ifeq ($(CONFIG_SCREEN_DEPTH),4)
GFXPAYLOAD := text
else ifeq ($(CONFIG_SCREEN_DEPTH),8)
# Request the configured geometry in real 8bpp / 256-colour mode.
# Keep only same-depth fallbacks so an 8bpp build never silently becomes 16bpp.
GFXPAYLOAD := $(CONFIG_SCREEN_WIDTH)x$(CONFIG_SCREEN_HEIGHT)x8
else
# Request the configured geometry in true 16bpp first.  15bpp is a
# compatibility fallback for older VBE implementations; 8bpp is the final
# colour fallback.  No 4bpp fallback is requested here.
GFXPAYLOAD := $(CONFIG_SCREEN_WIDTH)x$(CONFIG_SCREEN_HEIGHT)x16,$(CONFIG_SCREEN_WIDTH)x$(CONFIG_SCREEN_HEIGHT)x15,$(CONFIG_SCREEN_WIDTH)x$(CONFIG_SCREEN_HEIGHT)x8
endif

$(BOOT_DISK_CFG): src/config/config.h | build/generated
	@$(call LOG_COMPILE,boot disk menu,$@)
	@printf '%s\n' 'set timeout=0' 'set default=0' 'insmod all_video' 'set gfxpayload=$(GFXPAYLOAD)' 'terminal_output console' 'menuentry "HaloxOS!" {' '    multiboot /boot/loader.elf' '    module /boot/kernel.bin.zst' $(DOOM_MODULE_CFG) '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(BOOT_FLOPPY_CFG): src/config/config.h | build/generated
	@$(call LOG_COMPILE,boot floppy menu,$@)
	@printf '%s\n' 'set timeout=0' 'set default=0' 'insmod all_video' 'set gfxpayload=$(GFXPAYLOAD)' 'terminal_output console' 'menuentry "HaloxOS!" {' '    multiboot /boot/loader.elf' '    module /boot/kernel.bin.zst' '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(CORE_DISK_CFG): FORCE | build/generated
	@$(call LOG_COMPILE,core disk menu,$@)
	@printf '%s\n' 'search --file /boot/grub/grub.cfg --set=root' 'set prefix=($$root)/boot/grub' 'configfile /boot/grub/grub.cfg' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(CORE_FLOPPY_CFG): FORCE | build/generated
	@$(call LOG_COMPILE,core floppy menu,$@)
	@printf '%s\n' 'search --file /boot/grub/grub.cfg --set=root' 'set prefix=($$root)/boot/grub' 'configfile /boot/grub/grub.cfg' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(DESKTOP_LAYOUT): | build/generated
	@$(call LOG_COMPILE,desktop layout,$@)
	@$(PYTHON) -c "import struct; magic=0x484C5850; positions=[(18,24),(18,104),(18,184),(96,24),(96,104),(96,184),(174,24),(174,104),(174,184),(252,24),(252,104)]; checksum=magic; payload=[]; [payload.extend(p) for p in positions]; [globals().__setitem__('checksum', checksum ^ v) for v in payload]; data=struct.pack('<II' + 'i' * len(payload), magic, checksum, *payload); open('$(DESKTOP_LAYOUT)', 'wb').write(data + b'\\x00' * (512 - len(data)))" || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

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

build/run_icon.bin: src/modes/states/graphical/desktop/apps/runGame/appicon.png build/tools/png2indexed | build
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

build/run_player.bin: src/modes/states/graphical/desktop/apps/runGame/spr/player-32x.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/run_player_died.bin: src/modes/states/graphical/desktop/apps/runGame/spr/player_died-32x.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/run_player16.bin: src/modes/states/graphical/desktop/apps/runGame/spr/player.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/run_coin1.bin: src/modes/states/graphical/desktop/apps/runGame/spr/coin.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/run_coin2.bin: src/modes/states/graphical/desktop/apps/runGame/spr/coin2.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/run_coin3.bin: src/modes/states/graphical/desktop/apps/runGame/spr/coin3.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/run_coin4.bin: src/modes/states/graphical/desktop/apps/runGame/spr/coin4.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/run_brick.bin: src/modes/states/graphical/desktop/apps/runGame/spr/brick.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/run_skull.bin: src/modes/states/graphical/desktop/apps/runGame/spr/skull-48x.png build/tools/png2indexed | build
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

build/run_icon_asset.o: build/run_icon.bin
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

build/run_player_asset.o: build/run_player.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/run_player_died_asset.o: build/run_player_died.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/run_player16_asset.o: build/run_player16.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/run_coin1_asset.o: build/run_coin1.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/run_coin2_asset.o: build/run_coin2.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/run_coin3_asset.o: build/run_coin3.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/run_coin4_asset.o: build/run_coin4.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/run_brick_asset.o: build/run_brick.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/run_skull_asset.o: build/run_skull.bin
	@$(call LOG_COMPILE,$<,$@)
	@$(LD) -m elf_i386 -r -b binary -o $@ $< || { $(call LOG_ERROR,Failed to package $< to $@); exit 1; }

build/boot.o: src/kernel/system/boot.asm | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(AS) $(ASFLAGS) -f elf32 -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

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

build/loader_boot.o: loader/boot.asm src/config/config.h | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,$(AS) $(ASFLAGS) -f elf32 -o $@ $<) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

build/loader_main.o: loader/main.c loader/include/string.h src/config/config.h $(NORAM_STAMP) $(DOOM_WAD_DATA_C) $(RAM_REQUIREMENT_H) | build
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
# With span_end ~5.34 MB this yields a 6 MB gate: nominal 6 MB machines boot.
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
				echo '#define HALOXOS_KERNEL_MEMORY_BYTES 0x580000u'; \
				echo '#define HALOXOS_LOADER_INFO_ADDR 0x590000u'; \
				echo '#define HALOXOS_KERNEL_RAM_REQUIRED_MB 6u'; \
				echo '#define HALOXOS_KERNEL_RAM_REQUIRED_BYTES 0x600000u'; \
				echo '#define HALOXOS_KERNEL_RAM_GATE_BYTES 0x600000u'; \
			fi; \
			echo ''; \
			echo '#endif'; \
		} > $@ || { $(call LOG_ERROR,Failed to create stub $@); exit 1; }; \
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

$(LOADER): $(LOADER_OBJS) loader/linker.ld
	@$(call LOG_COMPILE,loader objects,$@)
	@$(call RUN_LOG,$(LD) -m elf_i386 -T loader/linker.ld -o $@ $(LOADER_OBJS)) || { $(call LOG_ERROR,Failed to link $@); exit 1; }

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
	@$(call RUN_LOG,grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_DISK_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal configfile multiboot search search_fs_file vbe all_video) || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(FLOPPY_CORE): $(CORE_FLOPPY_CFG) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(call RUN_LOG,grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_FLOPPY_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal configfile multiboot search search_fs_file vbe all_video) || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

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

$(CRASH_SYMBOLS_C): FORCE | build/generated
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
	@# 6 MB machine fail even though the real span only needs 5.5 MB.
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
	@cp $(LOADER) $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }

build/isodir/boot/kernel.bin.zst: $(KERNEL_ZST) | build/isodir/boot/grub
	@$(call LOG_COMPILE,$<,$@)
	@cp $(KERNEL_ZST) $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }

# WAD multiboot module: GRUB loads it via BIOS, so the kernel finds the
# game data in RAM on any boot media (USB/Ventoy, CD, disk).
build/doom.wadmodule: $(DOOM_WAD) Makefile | build
	@if [ -z "$(DOOM_WAD)" ]; then \
		$(call LOG_ERROR,DOOM1.WAD not found); exit 1; \
	fi
	@$(call LOG_COMPILE,$(DOOM_WAD),WAD multiboot module $@)
	@printf 'DWAD' > $@ || exit 1; \
	python3 -c "import struct; open('$@','ab').write(struct.pack('<III', $$(stat -c %s $(DOOM_WAD)), 0, 0))" || exit 1; \
	cat $(DOOM_WAD) >> $@
	@$(call LOG_OK,WAD multiboot module ready: $@)

build/isodir/boot/doom.wadmodule: build/doom.wadmodule | build/isodir/boot/grub
	@$(call LOG_COMPILE,$<,$@)
	@cp $< $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }

build/isodir/boot/grub/grub.cfg: src/config/config.h $(DOOM_WAD_MODULE_DEP) | build/isodir/boot/grub
	@$(call LOG_COMPILE,boot menu,$@)
	@printf '%s\n' 'set timeout=0' 'set default=0' 'insmod all_video' 'set gfxpayload=$(GFXPAYLOAD)' 'terminal_output console' 'menuentry "HaloxOS!" {' '    insmod gzio' '    multiboot /boot/loader.elf' '    module /boot/kernel.bin.zst' $(DOOM_MODULE_CFG) '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

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
		ram_limit=$$(python3 -c "print(int(open('build/generated/ram_requirement.h').read().split('HALOXOS_KERNEL_RAM_REQUIRED_MB ')[1].split('u')[0]))"); ram_note="$${ram_limit} MB auto-fit boot floor (generated from the kernel span)"; \
	fi; \
	if python3 -c "import sys; sys.exit(0 if $$mem_mb < $$ram_limit else 1)"; then \
		mem_left=$$(python3 -c "print('%.2f' % ($$ram_limit - $$mem_mb))"); \
		if python3 -c "import sys; sys.exit(0 if $$mem_left <= 1.0 else 1)"; then \
			printf '%b[WARNING!]%b %bOnly %s MB of headroom below the %s - close it up before adding anything heavy.%b\n' '$(COLOR_ORANGE)' '$(COLOR_RESET)' '$(COLOR_YELLOW)' "$$mem_left" "$$ram_note" '$(COLOR_RESET)'; \
		else \
			printf '%b[OK]%b %b%s MB of headroom below the %s%b\n' '$(COLOR_GREEN)' '$(COLOR_RESET)' '$(COLOR_BLUE)' "$$mem_left" "$$ram_note" '$(COLOR_RESET)'; \
		fi; \
	else \
		printf '%b[WARNING!]%b %bKernel size has EXCEEDED the %s!%b\n' '$(COLOR_ORANGE)' '$(COLOR_RESET)' '$(COLOR_YELLOW)' "$$ram_note" '$(COLOR_RESET)'; \
	fi
endef

$(DOOMDISK): $(DOOM_WAD) Makefile | build
	@if [ -z "$(DOOM_WAD)" ]; then \
		$(call LOG_ERROR,DOOM1.WAD not found); exit 1; \
	fi
	@$(call LOG_COMPILE,$(DOOM_WAD),DOOM data disk (compact raw LBA $(DOOM_WAD_DATA_LBA)))
	@truncate -s $$(( ($(DOOM_WAD_DATA_LBA) + $(DOOM_WAD_SECTORS) + 16) * 512 )) $@ || { $(call LOG_ERROR,Failed to create $@); exit 1; }
	@printf 'DWAD' > build/doom_wad_info.bin || exit 1
	@python3 -c "import struct; open('build/doom_wad_info.bin','ab').write(struct.pack('<III', $$(stat -c %s $(DOOM_WAD)), $(DOOM_WAD_DATA_LBA), 0))" || exit 1
	@dd if=build/doom_wad_info.bin of=$@ bs=512 seek=$(DOOM_WAD_INFO_LBA) conv=notrunc status=none || { $(call LOG_ERROR,Failed to write WAD info into $@); exit 1; }
	@dd if=$(DOOM_WAD) of=$@ bs=512 seek=$(DOOM_WAD_DATA_LBA) conv=notrunc status=none || { $(call LOG_ERROR,Failed to write WAD into $@); exit 1; }
	@$(call LOG_OK,DOOM data disk ready: $@)

.PHONY: doomdisk
doomdisk: | build
	@if [ -z "$(DOOM_WAD)" ]; then \
		$(call LOG_WARNING,DOOM1.WAD not found - DOOM app will show the missing-WAD error screen); \
	else \
		$(MAKE) --no-print-directory $(DOOMDISK); \
	fi

$(ISO): build/isodir/boot/loader.elf build/isodir/boot/kernel.bin.zst $(DOOM_WAD_MODULE_DEP) build/isodir/boot/grub/grub.cfg $(DOOM_WAD) Makefile
	@$(call LOG_COMPILE,build/isodir,$@)
	@if [ "$(DOOM_WAD_MODULE_BOOT)" != "1" ]; then rm -f build/isodir/boot/doom.wadmodule; fi
	@$(call RUN_LOG,grub-mkrescue -o $@ build/isodir) || { $(call LOG_ERROR,Failed to generate $@); exit 1; }
	@if [ -n "$(DOOM_WAD)" ]; then \
		$(call LOG_COMPILE,$(DOOM_WAD),DOOM WAD blob (ISO tail, 2048B sectors)); \
		iso_end=$$(( ($$(stat -c %s $@) + 2047) / 2048 )); \
		truncate -s $$(( (iso_end + $(DOOM_WAD_CD_SECTORS) + 1) * 2048 )) $@ || exit 1; \
		printf 'DWAD' > build/doom_wad_info_cd.bin || exit 1; \
		python3 -c "import struct; open('build/doom_wad_info_cd.bin','ab').write(struct.pack('<III', $$(stat -c %s $(DOOM_WAD)), $$iso_end, 0))" || exit 1; \
		dd if=$(DOOM_WAD) of=$@ bs=2048 seek=$$iso_end conv=notrunc status=none || exit 1; \
		dd if=build/doom_wad_info_cd.bin of=$@ bs=2048 seek=$$((iso_end + $(DOOM_WAD_CD_SECTORS))) conv=notrunc status=none || exit 1; \
	else \
		$(call LOG_WARNING,DOOM1.WAD not found - DOOM app will show the missing-WAD error screen); \
	fi
	@$(KERNEL_SIZE_REPORT)
	@$(call LOG_OK,Build Finished!)

$(DISK): $(LOADER) $(KERNEL_ZST) $(DISK_CORE) $(BOOT_DISK_CFG) $(DESKTOP_LAYOUT) $(DOOM_WAD) Makefile | build
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
	@mcopy -i $@@@$(DISK_PART_OFFSET) $(LOADER) ::/boot/loader.elf || { $(call LOG_ERROR,Failed to copy $(LOADER) into $@); exit 1; }
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
	@$(KERNEL_SIZE_REPORT)
	@$(call LOG_OK,Disk Image Finished!)

$(FLOPPY): $(LOADER) $(KERNEL_ZST) $(FLOPPY_CORE) $(BOOT_FLOPPY_CFG) | build
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
	@mcopy -i $@@@$(FLOPPY_PART_OFFSET) $(LOADER) ::/boot/loader.elf || { $(call LOG_ERROR,Failed to copy $(LOADER) into $@); exit 1; }
	@$(call LOG_COMPILE,$(KERNEL_ZST),$@)
	@mcopy -i $@@@$(FLOPPY_PART_OFFSET) $(KERNEL_ZST) ::/boot/kernel.bin.zst || { $(call LOG_ERROR,Failed to copy $(KERNEL_ZST) into $@); exit 1; }
	@$(call LOG_COMPILE,$(GRUB_BOOT_IMG),$@)
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=446 count=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot sector into $@); exit 1; }
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=1 skip=510 seek=510 count=2 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot signature into $@); exit 1; }
	@$(call LOG_COMPILE,$(FLOPPY_CORE),$@)
	@dd if=$(FLOPPY_CORE) of=$@ bs=512 seek=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write $(FLOPPY_CORE) into $@); exit 1; }
	@$(KERNEL_SIZE_REPORT)
	@$(call LOG_OK,Floppy Image Finished!)

# Explicit media launchers. These are useful for testing each WAD path
# independently instead of hiding a disk-read problem behind the normal
# all-in-one runner.
run-iso: $(ISO)
	@$(QEMU_BIN) -cdrom $(ISO) -m 16 -vga std

run-disk: $(DISK)
	@$(QEMU_BIN) -drive file=$(DISK),format=raw,if=ide,index=0,media=disk -boot c -m 16 -vga std

# A 1.44 MB floppy cannot contain a 4.0 MB DOOM WAD. Keep the boot floppy
# unchanged and attach the compact WAD disk as a second IDE device.
run-floppy: $(FLOPPY) $(DOOMDISK)
	@$(QEMU_BIN) -fda $(FLOPPY) -drive file=$(DOOMDISK),format=raw,if=ide,index=1,media=disk -boot a -m 16 -vga std

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

clean:
	@rm -rf build || { printf '%b[ERROR!]%b Failed to remove build artifacts\n' '\033[31m' '\033[0m'; exit 1; }
	@$(call LOG_CLEAN_OK,Clean Done!)


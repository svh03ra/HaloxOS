# Copyright Svh03ra (C) 2026, All rights reserved
# This repository is licensed under the GNU General Public License.

AS := nasm
CC := gcc
LD := ld
OBJCOPY := objcopy
HOSTCC := gcc
PYTHON := python3
BUILD_STARTED := $(shell date +%s)

COLOR_RESET := \033[0m
COLOR_GREEN := \033[92m
COLOR_YELLOW := \033[33m
COLOR_BLUE := \033[94m
COLOR_RED := \033[31m
COLOR_ORANGE := \033[38;5;208m
COLOR_WHITE := \033[97m

LOG_COMPILE = printf '%b[Compiling...]%b %b%s%b %b%s%b\n' '$(COLOR_GREEN)' '$(COLOR_RESET)' '$(COLOR_YELLOW)' "$(1)" '$(COLOR_RESET)' '$(COLOR_BLUE)' "$(2)" '$(COLOR_RESET)'
LOG_DEP = printf '%b%s%b\n' '$(COLOR_BLUE)' "$(1)" '$(COLOR_RESET)'
LOG_ERROR = printf '%b[ERROR!]%b %b%s%b\n' '$(COLOR_RED)' '$(COLOR_RESET)' '$(COLOR_WHITE)' "$(1)" '$(COLOR_RESET)'
LOG_WARNING = printf '%b[WARNING!]%b %b%s%b\n' '$(COLOR_ORANGE)' '$(COLOR_RESET)' '$(COLOR_WHITE)' "$(1)" '$(COLOR_RESET)'
LOG_OK = elapsed_secs=$$(($$(date +%s) - $(BUILD_STARTED))); elapsed=$$(printf '%02d:%02d:%02d' $$((elapsed_secs / 3600)) $$(((elapsed_secs % 3600) / 60)) $$((elapsed_secs % 60))); printf '\n%bBuild Timelapse: %s%b\n%b[OK]%b %b%s%b\n' '$(COLOR_BLUE)' "$$elapsed" '$(COLOR_RESET)' '$(COLOR_GREEN)' '$(COLOR_RESET)' '$(COLOR_WHITE)' "$(1)" '$(COLOR_RESET)'
LOG_CLEAN_OK = printf '%b[OK]%b %b%s%b\n' '$(COLOR_GREEN)' '$(COLOR_RESET)' '$(COLOR_WHITE)' "$(1)" '$(COLOR_RESET)'
LOG_INTRO = printf '%s\n%s\n%s\n%s\nBuild Type: %s | %b%s%b\n%s\n\n%s\n\n' '==================================================' 'HaloxOS Compiler, (C) 2026 Svh03ra' '  ----------------------------------------------  ' 'Compiler Version: 2.0' "$(BUILD_MEDIA)" '$(BUILD_PROFILE_COLOR)' "$(BUILD_PROFILE)" '$(COLOR_RESET)' '==================================================' 'Starting to build...'

CONFIG_SCREEN_WIDTH := $(shell sed -n 's/^#define HALOXOS_CONFIG_SCREEN_WIDTH[[:space:]]*//p' src/config/config.h)
CONFIG_SCREEN_HEIGHT := $(shell sed -n 's/^#define HALOXOS_CONFIG_SCREEN_HEIGHT[[:space:]]*//p' src/config/config.h)
CONFIG_DEBUG := $(shell sed -n 's/^#define HALOXOS_CONFIG_DEBUG[[:space:]]*//p' src/config/config.h)
CONFIG_DEV_MODE := $(shell sed -n 's/^#define HALOXOS_CONFIG_DEV_MODE[[:space:]]*//p' src/config/config.h)
CONFIG_SCREEN_DEPTH := $(shell sed -n 's/^#define HALOXOS_CONFIG_SCREEN_BPP[[:space:]]*//p' src/config/config.h)
ASFLAGS := -DHALOXOS_BOOT_SCREEN_WIDTH=$(CONFIG_SCREEN_WIDTH) -DHALOXOS_BOOT_SCREEN_HEIGHT=$(CONFIG_SCREEN_HEIGHT) -DHALOXOS_BOOT_SCREEN_DEPTH=$(CONFIG_SCREEN_DEPTH)
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -m32 -march=i386 -Ibuild/generated
LDFLAGS := -T linker.ld
HOSTCFLAGS = -std=c11 -O2 -Wall -Wextra $(shell pkg-config --cflags libpng)
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
ARCH_PACKAGES := nasm gcc binutils grub xorriso pkgconf libpng dosfstools parted mtools gzip zstd python3 qemu-system-x86
DEBIAN_PACKAGES := nasm gcc gcc-multilib binutils grub-pc-bin grub-common xorriso pkg-config libpng-dev dosfstools parted mtools gzip zstd python3 qemu-system-x86

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
KERNEL_GZ := build/kernel.bin.gz
KERNEL_ZST := build/kernel.bin.zst
LOADER := build/loader.elf
LOADER_OBJS := \
build/loader_boot.o \
build/loader_main.o \
build/loader_zstd.o \
build/loader_shim.o

LOADER_FLAGS := -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -m32 -march=i386 -fno-asynchronous-unwind-tables -Iloader/include -DHALOXOS_BOOT_SCREEN_DEPTH=$(CONFIG_SCREEN_DEPTH) -DHALOXOS_BOOT_SCREEN_WIDTH=$(CONFIG_SCREEN_WIDTH) -DHALOXOS_BOOT_SCREEN_HEIGHT=$(CONFIG_SCREEN_HEIGHT) -DHALOXOS_FORCE_RAM_ERROR=$(NORAM_TEST)
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
GRUB_BOOT_IMG := /usr/lib/grub/i386-pc/boot.img
DISK_PART_START := 2048
DISK_PART_OFFSET := 1048576
FLOPPY_PART_START := 400
FLOPPY_PART_OFFSET := 204800
DESKTOP_LAYOUT_LBA := 1536
KERNEL_OBJS := \
build/boot.o \
build/interrupts.o \
build/kernel.o \
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

KERNEL_ENTRY := src/kernel/system/kernel.c
KERNEL_FRAGMENTS := $(shell find src/driver src/kernel src/modes -name '*.c' ! -path '$(KERNEL_ENTRY)' | sort)
KERNEL_HEADERS := src/config/config.h
KERNEL_SRC := $(KERNEL_ENTRY) $(KERNEL_FRAGMENTS) $(KERNEL_HEADERS)

.PHONY: all clean run serial disk floppy build-intro install-deps FORCE
all: build-intro check-build install-deps $(ISO)

disk: build-intro check-build install-deps $(DISK)

floppy: build-intro check-build install-deps $(FLOPPY)

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
	command -v gzip >/dev/null 2>&1 || missing=1; \
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

$(BUILD_INFO): FORCE | build/generated
	@$(call LOG_COMPILE,$(BUILD_ID),$@)
	@printf '%s\n' "#ifndef HALOXOS_BUILD_TEXT" "#define HALOXOS_BUILD_TEXT \"Build: '$(BUILD_ID)'\"" "#endif" > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

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
	@printf '%s\n' 'set timeout=0' 'set default=0' 'insmod all_video' 'set gfxpayload=$(GFXPAYLOAD)' 'terminal_output console' 'menuentry "HaloxOS!" {' '    multiboot /boot/loader.elf' '    module /boot/kernel.bin.zst' '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

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
	@$(HOSTCC) $(HOSTCFLAGS) -o $@ $< $(HOSTLIBS) || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

build/login.bin: src/modes/states/graphical/login/ui/login.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/theme1.bin: src/modes/states/graphical/desktop/ui/backgrounds/theme1.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

build/theme2.bin: src/modes/states/graphical/desktop/ui/backgrounds/theme2.png build/tools/png2indexed | build
	@$(call LOG_COMPILE,$<,$@)
	@build/tools/png2indexed $< $@ || { $(call LOG_ERROR,Failed to convert $< to $@); exit 1; }

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

build/boot.o: src/kernel/system/boot.asm | build
	@$(call LOG_COMPILE,$<,$@)
	@$(AS) $(ASFLAGS) -f elf32 -o $@ $< || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

build/interrupts.o: src/kernel/system/interrupts.asm | build
	@$(call LOG_COMPILE,$<,$@)
	@$(AS) -f elf32 -o $@ $< || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

build/kernel.o: $(KERNEL_SRC) $(BUILD_INFO) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(CC) $(CFLAGS) -c -o $@ $< || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

$(KERNEL_GZ): $(KERNEL) | build
	@$(call LOG_COMPILE,$<,$@)
	@gzip -n -9 -c $< > $@ || { $(call LOG_ERROR,Failed to compress $< to $@); exit 1; }

# ===== zstd boot chain =====

build/loader_boot.o: loader/boot.asm src/config/config.h | build
	@$(call LOG_COMPILE,$<,$@)
	@$(AS) $(ASFLAGS) -f elf32 -o $@ $< || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

build/loader_main.o: loader/main.c loader/include/string.h src/config/config.h $(NORAM_STAMP) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(CC) $(LOADER_FLAGS) -c -o $@ $< || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

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
	@$(CC) $(LOADER_FLAGS) -c -o $@ $< || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

build/loader_shim.o: loader/shim.c loader/include/string.h | build
	@$(call LOG_COMPILE,$<,$@)
	@$(CC) $(LOADER_FLAGS) -c -o $@ $< || { $(call LOG_ERROR,Failed to compile $< to $@); exit 1; }

$(LOADER): $(LOADER_OBJS) loader/linker.ld
	@$(call LOG_COMPILE,loader objects,$@)
	@$(LD) -m elf_i386 -T loader/linker.ld -o $@ $(LOADER_OBJS) || { $(call LOG_ERROR,Failed to link $@); exit 1; }

# Flat binary kernel image: file offset 0 == link address 0x200000, so the
# loader can decompress it straight into place and jump to the entry VMA.
# Compressing the ELF instead would ship ELF headers/offsets the loader
# cannot relocate, and the entry would land on garbage.
KERNEL_FLAT := build/kernel.flat
KERNEL_BASE := 0x200000

$(KERNEL_FLAT): $(KERNEL) | build
	@$(call LOG_COMPILE,$<,$@)
	@$(OBJCOPY) -O binary $(KERNEL) $@ || { $(call LOG_ERROR,Failed to flatten $< to $@); exit 1; }

$(KERNEL_ZST): $(KERNEL_FLAT) | build
	@$(call LOG_COMPILE,$<,$@)
	@zstd -$(ZSTD_LEVEL) -T0 -f -q -o $@.frame $< || { $(call LOG_ERROR,Failed to compress $< with zstd); exit 1; }
	@entry_addr=$$(nm $(KERNEL) | awk '/ T start$$/ {print "0x"$$1}'); \
	if [ -z "$$entry_addr" ]; then $(call LOG_ERROR,kernel start symbol not found); exit 1; fi; \
	$(PYTHON) tools/mkzstmodule.py $< $@.frame $@ $(MODULE_MAGIC) $$entry_addr || { $(call LOG_ERROR,Failed to package $@); exit 1; }
	@rm -f $@.frame

$(DISK_CORE): $(CORE_DISK_CFG) | build
	@$(call LOG_COMPILE,$<,$@)
	@grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_DISK_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal configfile multiboot search search_fs_file vbe all_video || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(FLOPPY_CORE): $(CORE_FLOPPY_CFG) | build
	@$(call LOG_COMPILE,$<,$@)
	@grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_FLOPPY_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal configfile multiboot search search_fs_file vbe all_video || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(KERNEL): $(KERNEL_OBJS)
	@$(call LOG_COMPILE,kernel objects,$@)
	@warn_log=$$(mktemp); \
	if $(LD) -m elf_i386 $(LDFLAGS) -o $@ $(KERNEL_OBJS) 2>$$warn_log; then \
		if [ -s $$warn_log ]; then \
			while IFS= read -r warning_line; do $(call LOG_WARNING,$$warning_line); done < $$warn_log; \
		fi; \
		rm -f $$warn_log; \
	else \
		while IFS= read -r error_line; do $(call LOG_ERROR,$$error_line); done < $$warn_log; \
		rm -f $$warn_log; \
		$(call LOG_ERROR,Failed to link $@); \
		exit 1; \
	fi

build/isodir/boot/loader.elf: $(LOADER) | build/isodir/boot/grub
	@$(call LOG_COMPILE,$<,$@)
	@cp $(LOADER) $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }

build/isodir/boot/kernel.bin.zst: $(KERNEL_ZST) | build/isodir/boot/grub
	@$(call LOG_COMPILE,$<,$@)
	@cp $(KERNEL_ZST) $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }

build/isodir/boot/grub/grub.cfg: src/config/config.h | build/isodir/boot/grub
	@$(call LOG_COMPILE,boot menu,$@)
	@printf '%s\n' 'set timeout=0' 'set default=0' 'insmod all_video' 'set gfxpayload=$(GFXPAYLOAD)' 'terminal_output console' 'menuentry "HaloxOS!" {' '    insmod gzio' '    multiboot /boot/loader.elf' '    module /boot/kernel.bin.zst' '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(ISO): build/isodir/boot/loader.elf build/isodir/boot/kernel.bin.zst build/isodir/boot/grub/grub.cfg
	@$(call LOG_COMPILE,build/isodir,$@)
	@grub-mkrescue -o $@ build/isodir >/dev/null 2>&1 || { $(call LOG_ERROR,Failed to generate $@); exit 1; }
	@$(call LOG_OK,Build Finished!)

$(DISK): $(LOADER) $(KERNEL_ZST) $(DISK_CORE) $(BOOT_DISK_CFG) $(DESKTOP_LAYOUT) | build
	@$(call LOG_COMPILE,Empty disk image,$@)
	@truncate -s 64M $@ || { $(call LOG_ERROR,Failed to create $@); exit 1; }
	@$(call LOG_COMPILE,$@,msdos partition table)
	@warn_log=$$(mktemp); \
	if parted -s $@ unit s mklabel msdos mkpart primary 2048s 100% >$$warn_log 2>&1; then \
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
	@$(call LOG_COMPILE,$(GRUB_BOOT_IMG),$@)
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=446 count=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot sector into $@); exit 1; }
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=1 skip=510 seek=510 count=2 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot signature into $@); exit 1; }
	@$(call LOG_COMPILE,$(DISK_CORE),$@)
	@dd if=$(DISK_CORE) of=$@ bs=512 seek=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write $(DISK_CORE) into $@); exit 1; }
	@$(call LOG_COMPILE,$(DESKTOP_LAYOUT),$@)
	@dd if=$(DESKTOP_LAYOUT) of=$@ bs=512 seek=$(DESKTOP_LAYOUT_LBA) conv=notrunc status=none || { $(call LOG_ERROR,Failed to write $(DESKTOP_LAYOUT) into $@); exit 1; }
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
	@$(call LOG_OK,Floppy Image Finished!)

# Interactive QEMU launcher: asks for memory size and VGA type, then runs
# the built ISO. Works on both Windows (MSYS2/MinGW shells with QEMU from
# qemu.org or w64devkit) and Linux.
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
	@printf 'Set to Memory Size [default: 16]: '; \
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
	"$(QEMU_BIN)" $(QEMU_ARGS) -m $$mem -vga $$vga

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
	@rm -rf build || { $(call LOG_ERROR,Failed to remove build artifacts); exit 1; }
	@$(call LOG_CLEAN_OK,Clean Done!)

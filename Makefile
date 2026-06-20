# Copyright Svh03ra (C) 2026, All rights reserved
# This repository is licensed under the GNU General Public License.

AS := nasm
CC := gcc
LD := ld
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
CONFIG_SCREEN_DEPTH := 0
ASFLAGS := -DHALOXOS_BOOT_SCREEN_WIDTH=$(CONFIG_SCREEN_WIDTH) -DHALOXOS_BOOT_SCREEN_HEIGHT=$(CONFIG_SCREEN_HEIGHT) -DHALOXOS_BOOT_SCREEN_DEPTH=$(CONFIG_SCREEN_DEPTH)
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -m32 -march=i386 -Ibuild/generated
LDFLAGS := -T linker.ld
HOSTCFLAGS = -std=c11 -O2 -Wall -Wextra $(shell pkg-config --cflags libpng)
HOSTLIBS = $(shell pkg-config --libs libpng)
QEMU_ARGS = -cdrom $(ISO) -m 128 -vga std
ifneq ($(filter serial,$(MAKECMDGOALS)),)
QEMU_ARGS += -serial stdio -monitor none
endif
ARCH_PACKAGES := nasm gcc binutils grub xorriso pkgconf libpng dosfstools parted mtools gzip python3 qemu-system-x86
DEBIAN_PACKAGES := nasm gcc gcc-multilib binutils grub-pc-bin grub-common xorriso pkg-config libpng-dev dosfstools parted mtools gzip python3 qemu-system-x86

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
ISO := build/os.iso
DISK := build/haloxos-disk.img
FLOPPY := build/haloxos-floppy.img
KERNEL := build/kernel.bin
KERNEL_GZ := build/kernel.bin.gz
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

$(BOOT_DISK_CFG): FORCE | build/generated
	@$(call LOG_COMPILE,boot disk menu,$@)
	@printf '%s\n' 'set timeout=0' 'set default=0' 'terminal_output console' 'menuentry "HaloxOS!" {' '    multiboot /boot/kernel.bin.gz' '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(BOOT_FLOPPY_CFG): FORCE | build/generated
	@$(call LOG_COMPILE,boot floppy menu,$@)
	@printf '%s\n' 'set timeout=0' 'set default=0' 'terminal_output console' 'menuentry "HaloxOS!" {' '    multiboot /boot/kernel.bin.gz' '    boot' '}' > $@ || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

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

$(DISK_CORE): $(CORE_DISK_CFG) | build
	@$(call LOG_COMPILE,$<,$@)
	@grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_DISK_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal configfile multiboot gzio search search_fs_file || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

$(FLOPPY_CORE): $(CORE_FLOPPY_CFG) | build
	@$(call LOG_COMPILE,$<,$@)
	@grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_FLOPPY_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal configfile multiboot gzio search search_fs_file || { $(call LOG_ERROR,Failed to generate $@); exit 1; }

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

build/isodir/boot/kernel.bin: $(KERNEL) | build/isodir/boot/grub
	@$(call LOG_COMPILE,$<,$@)
	@cp $(KERNEL) $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }

build/isodir/boot/grub/grub.cfg: grub/grub.cfg | build/isodir/boot/grub
	@$(call LOG_COMPILE,$<,$@)
	@cp $< $@ || { $(call LOG_ERROR,Failed to copy $< to $@); exit 1; }

$(ISO): build/isodir/boot/kernel.bin build/isodir/boot/grub/grub.cfg
	@$(call LOG_COMPILE,build/isodir,$@)
	@grub-mkrescue -o $@ build/isodir >/dev/null 2>&1 || { $(call LOG_ERROR,Failed to generate $@); exit 1; }
	@$(call LOG_OK,Build Finished!)

$(DISK): $(KERNEL_GZ) $(DISK_CORE) $(BOOT_DISK_CFG) $(DESKTOP_LAYOUT) | build
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
	@$(call LOG_COMPILE,$(KERNEL_GZ),$@)
	@mcopy -i $@@@$(DISK_PART_OFFSET) $(KERNEL_GZ) ::/boot/kernel.bin.gz || { $(call LOG_ERROR,Failed to copy $(KERNEL_GZ) into $@); exit 1; }
	@$(call LOG_COMPILE,$(GRUB_BOOT_IMG),$@)
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=446 count=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot sector into $@); exit 1; }
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=1 skip=510 seek=510 count=2 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot signature into $@); exit 1; }
	@$(call LOG_COMPILE,$(DISK_CORE),$@)
	@dd if=$(DISK_CORE) of=$@ bs=512 seek=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write $(DISK_CORE) into $@); exit 1; }
	@$(call LOG_COMPILE,$(DESKTOP_LAYOUT),$@)
	@dd if=$(DESKTOP_LAYOUT) of=$@ bs=512 seek=$(DESKTOP_LAYOUT_LBA) conv=notrunc status=none || { $(call LOG_ERROR,Failed to write $(DESKTOP_LAYOUT) into $@); exit 1; }
	@$(call LOG_OK,Disk Image Finished!)

$(FLOPPY): $(KERNEL_GZ) $(FLOPPY_CORE) $(BOOT_FLOPPY_CFG) | build
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
	@$(call LOG_COMPILE,$(KERNEL_GZ),$@)
	@mcopy -i $@@@$(FLOPPY_PART_OFFSET) $(KERNEL_GZ) ::/boot/kernel.bin.gz || { $(call LOG_ERROR,Failed to copy $(KERNEL_GZ) into $@); exit 1; }
	@$(call LOG_COMPILE,$(GRUB_BOOT_IMG),$@)
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=446 count=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot sector into $@); exit 1; }
	@dd if=$(GRUB_BOOT_IMG) of=$@ bs=1 skip=510 seek=510 count=2 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write boot signature into $@); exit 1; }
	@$(call LOG_COMPILE,$(FLOPPY_CORE),$@)
	@dd if=$(FLOPPY_CORE) of=$@ bs=512 seek=1 conv=notrunc status=none || { $(call LOG_ERROR,Failed to write $(FLOPPY_CORE) into $@); exit 1; }
	@$(call LOG_OK,Floppy Image Finished!)

run: $(ISO)
	qemu-system-i386 $(QEMU_ARGS)

serial:
	@:

clean:
	@rm -rf build || { $(call LOG_ERROR,Failed to remove build artifacts); exit 1; }
	@$(call LOG_CLEAN_OK,Clean Done!)

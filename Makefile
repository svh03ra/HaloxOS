# Copyright Svh03ra (C) 2026, All rights reserved
# This repository is licensed under the GNU General Public License.

AS := nasm
CC := gcc
LD := ld
HOSTCC := gcc

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -m32 -march=i386 -Ibuild/generated
LDFLAGS := -T linker.ld
HOSTCFLAGS := -std=c11 -O2 -Wall -Wextra $(shell pkg-config --cflags libpng)
HOSTLIBS := $(shell pkg-config --libs libpng)

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
build/desktop_asset.o \
build/user_frame_asset.o \
build/notepad_icon_asset.o \
build/terminal_icon_asset.o \
build/game_icon_asset.o \
build/program_icon_asset.o \
build/settings_icon_asset.o \

.PHONY: all clean run disk floppy install-deps FORCE
all: check-build install-deps $(ISO)

disk: check-build install-deps $(DISK)

floppy: check-build install-deps $(FLOPPY)

install-deps:
	@missing=0; \
	command -v nasm >/dev/null 2>&1 || missing=1; \
	command -v gcc >/dev/null 2>&1 || missing=1; \
	command -v ld >/dev/null 2>&1 || missing=1; \
	command -v grub-mkrescue >/dev/null 2>&1 || missing=1; \
	command -v grub-mkimage >/dev/null 2>&1 || missing=1; \
	command -v xorriso >/dev/null 2>&1 || missing=1; \
	command -v pkg-config >/dev/null 2>&1 || missing=1; \
	pkg-config --exists libpng || missing=1; \
	command -v mkfs.fat >/dev/null 2>&1 || missing=1; \
	command -v parted >/dev/null 2>&1 || missing=1; \
	command -v mmd >/dev/null 2>&1 || missing=1; \
	command -v mcopy >/dev/null 2>&1 || missing=1; \
	command -v gzip >/dev/null 2>&1 || missing=1; \
	command -v qemu-system-i386 >/dev/null 2>&1 || missing=1; \
	if [ $$missing -eq 1 ]; then \
		echo "Before starting build, Installing some important packages:"; \
		echo "[#] Checking build dependencies... Let's get some orderin'"; \
		command -v nasm >/dev/null 2>&1 || sudo pacman -Sy --noconfirm nasm; \
		command -v gcc >/dev/null 2>&1 || sudo pacman -Sy --noconfirm gcc; \
		command -v ld >/dev/null 2>&1 || sudo pacman -Sy --noconfirm binutils; \
		command -v grub-mkrescue >/dev/null 2>&1 || sudo pacman -Sy --noconfirm grub; \
		command -v grub-mkimage >/dev/null 2>&1 || sudo pacman -Sy --noconfirm grub; \
		command -v xorriso >/dev/null 2>&1 || sudo pacman -Sy --noconfirm xorriso; \
		command -v pkg-config >/dev/null 2>&1 || sudo pacman -Sy --noconfirm pkgconf; \
		pkg-config --exists libpng || sudo pacman -Sy --noconfirm libpng; \
		command -v mkfs.fat >/dev/null 2>&1 || sudo pacman -Sy --noconfirm dosfstools; \
		command -v parted >/dev/null 2>&1 || sudo pacman -Sy --noconfirm parted; \
		command -v mmd >/dev/null 2>&1 || sudo pacman -Sy --noconfirm mtools; \
		command -v mcopy >/dev/null 2>&1 || sudo pacman -Sy --noconfirm mtools; \
		command -v gzip >/dev/null 2>&1 || sudo pacman -Sy --noconfirm gzip; \
		command -v qemu-system-i386 >/dev/null 2>&1 || sudo pacman -Sy --noconfirm qemu; \
	fi

check-build:
	@if [ -f "$(ISO)" ]; then \
		echo "You have already built exists!"; \
	fi

build:
	mkdir -p build

build/tools:
	mkdir -p build/tools

build/isodir/boot/grub:
	mkdir -p build/isodir/boot/grub

build/generated:
	mkdir -p build/generated

FORCE:

$(BUILD_INFO): FORCE | build/generated
	printf '%s\n' "#ifndef HALOXOS_BUILD_TEXT" "#define HALOXOS_BUILD_TEXT \"Build: '$(BUILD_ID)'\"" "#endif" > $@

$(BOOT_DISK_CFG): FORCE | build/generated
	printf '%s\n' 'set timeout=0' 'set default=0' 'terminal_output console' 'menuentry "HaloxOS!" {' '    multiboot /boot/kernel.bin.gz' '    boot' '}' > $@

$(BOOT_FLOPPY_CFG): FORCE | build/generated
	printf '%s\n' 'set timeout=0' 'set default=0' 'terminal_output console' 'menuentry "HaloxOS!" {' '    multiboot /boot/kernel.bin.gz' '    boot' '}' > $@

$(CORE_DISK_CFG): FORCE | build/generated
	printf '%s\n' 'search --file /boot/grub/grub.cfg --set=root' 'set prefix=($$root)/boot/grub' 'configfile /boot/grub/grub.cfg' > $@

$(CORE_FLOPPY_CFG): FORCE | build/generated
	printf '%s\n' 'search --file /boot/grub/grub.cfg --set=root' 'set prefix=($$root)/boot/grub' 'configfile /boot/grub/grub.cfg' > $@

$(DESKTOP_LAYOUT): | build/generated
	python -c "import struct; magic=0x484C5850; positions=[(18,24),(18,104)]; checksum=magic ^ positions[0][0] ^ positions[0][1] ^ positions[1][0] ^ positions[1][1]; data=struct.pack('<IIiiii', magic, checksum, positions[0][0], positions[0][1], positions[1][0], positions[1][1]); open('$(DESKTOP_LAYOUT)', 'wb').write(data + b'\\x00' * (512 - len(data)))"

build/tools/png2indexed: tools/png2indexed.c | build/tools
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $< $(HOSTLIBS)

build/login.bin: src/bg/login.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/desktop.bin: src/bg/desktop.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/user_frame.bin: src/item/user/user-frame.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/notepad_icon.bin: src/item/notepad.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/terminal_icon.bin: src/item/terminal.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/game_icon.bin: src/item/game.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/program_icon.bin: src/item/program.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/settings_icon.bin: src/item/settings.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/login_asset.o: build/login.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/desktop_asset.o: build/desktop.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/user_frame_asset.o: build/user_frame.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/notepad_icon_asset.o: build/notepad_icon.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/terminal_icon_asset.o: build/terminal_icon.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/game_icon_asset.o: build/game_icon.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/program_icon_asset.o: build/program_icon.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/settings_icon_asset.o: build/settings_icon.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/boot.o: src/boot.asm | build
	$(AS) -f elf32 -o $@ $<

build/interrupts.o: src/interrupts.asm | build
	$(AS) -f elf32 -o $@ $<

build/kernel.o: src/kernel.c $(BUILD_INFO) | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL_GZ): $(KERNEL) | build
	gzip -n -9 -c $< > $@

$(DISK_CORE): $(CORE_DISK_CFG) | build
	grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_DISK_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal configfile multiboot gzio search search_fs_file

$(FLOPPY_CORE): $(CORE_FLOPPY_CFG) | build
	grub-mkimage -O i386-pc -o $@ -d /usr/lib/grub/i386-pc -c $(CORE_FLOPPY_CFG) -p '(,msdos1)/boot/grub' biosdisk part_msdos fat normal configfile multiboot gzio search search_fs_file

$(KERNEL): $(KERNEL_OBJS)
	$(LD) -m elf_i386 $(LDFLAGS) -o $@ $(KERNEL_OBJS)

build/isodir/boot/kernel.bin: $(KERNEL) | build/isodir/boot/grub
	cp $(KERNEL) $@

build/isodir/boot/grub/grub.cfg: grub/grub.cfg | build/isodir/boot/grub
	cp $< $@

$(ISO): build/isodir/boot/kernel.bin build/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/isodir >/dev/null 2>&1
	@echo -e "\nBuild Finished!"

$(DISK): $(KERNEL_GZ) $(DISK_CORE) $(BOOT_DISK_CFG) $(DESKTOP_LAYOUT) | build
	truncate -s 64M $@
	parted -s $@ unit s mklabel msdos mkpart primary 2048s 100%
	mkfs.fat -F 16 --offset=$(DISK_PART_START) -h $(DISK_PART_START) -n HALOXOSHD $@ >/dev/null 2>&1
	mmd -i $@@@$(DISK_PART_OFFSET) ::/boot ::/boot/grub
	mcopy -i $@@@$(DISK_PART_OFFSET) $(BOOT_DISK_CFG) ::/boot/grub/grub.cfg
	mcopy -i $@@@$(DISK_PART_OFFSET) $(KERNEL_GZ) ::/boot/kernel.bin.gz
	dd if=$(GRUB_BOOT_IMG) of=$@ bs=446 count=1 conv=notrunc status=none
	dd if=$(GRUB_BOOT_IMG) of=$@ bs=1 skip=510 seek=510 count=2 conv=notrunc status=none
	dd if=$(DISK_CORE) of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(DESKTOP_LAYOUT) of=$@ bs=512 seek=$(DESKTOP_LAYOUT_LBA) conv=notrunc status=none
	@echo -e "\nDisk Image Finished!"

$(FLOPPY): $(KERNEL_GZ) $(FLOPPY_CORE) $(BOOT_FLOPPY_CFG) | build
	@core_sectors=$$((($$(stat -c %s $(FLOPPY_CORE)) + 511) / 512)); \
	if [ $$core_sectors -ge $(FLOPPY_PART_START) ]; then \
		echo "Floppy core image is too large ($$core_sectors sectors)."; \
		exit 1; \
	fi
	truncate -s 1474560 $@
	parted -s $@ unit s mklabel msdos mkpart primary $(FLOPPY_PART_START)s 2879s
	mkfs.fat -F 12 --offset=$(FLOPPY_PART_START) -h $(FLOPPY_PART_START) -n HALOXOSFD $@ >/dev/null 2>&1
	mmd -i $@@@$(FLOPPY_PART_OFFSET) ::/boot ::/boot/grub
	mcopy -i $@@@$(FLOPPY_PART_OFFSET) $(BOOT_FLOPPY_CFG) ::/boot/grub/grub.cfg
	mcopy -i $@@@$(FLOPPY_PART_OFFSET) $(KERNEL_GZ) ::/boot/kernel.bin.gz
	dd if=$(GRUB_BOOT_IMG) of=$@ bs=446 count=1 conv=notrunc status=none
	dd if=$(GRUB_BOOT_IMG) of=$@ bs=1 skip=510 seek=510 count=2 conv=notrunc status=none
	dd if=$(FLOPPY_CORE) of=$@ bs=512 seek=1 conv=notrunc status=none
	@echo -e "\nFloppy Image Finished!"

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -m 128 -vga std

clean:
	rm -rf build
	@echo -e "\nClean Done!"

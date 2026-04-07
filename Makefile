#	HaloxOS - Version 1.01 Beta!
#	Copyright Svh03ra (C) 2026, All rights reserved.
#	Source File: Makefile, build's ready of all options.
#	Build: 7th April 2026

#	Made in AI used: GPT-5.4 for Visual Code Editor at Codex.


#	This repository is licensed under the GNU General Public License.
#		--------------------------------------------------------------
#	Free to use, modify, or create your own fork,
#	provided that you agree to and comply with the terms of the license.

AS := nasm
CC := gcc
LD := ld
HOSTCC := gcc

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -m32 -march=i386
LDFLAGS := -T linker.ld
HOSTCFLAGS := -std=c11 -O2 -Wall -Wextra $(shell pkg-config --cflags libpng)
HOSTLIBS := $(shell pkg-config --libs libpng)

ISO := build/os.iso
KERNEL := build/kernel.bin
KERNEL_OBJS := \
build/boot.o \
build/interrupts.o \
build/kernel.o \
build/login_asset.o \
build/desktop_asset.o \

.PHONY: all clean run install-deps
all: check-build install-deps $(ISO)

install-deps:
	@missing=0; \
	command -v nasm >/dev/null 2>&1 || missing=1; \
	command -v gcc >/dev/null 2>&1 || missing=1; \
	command -v ld >/dev/null 2>&1 || missing=1; \
	command -v grub-mkrescue >/dev/null 2>&1 || missing=1; \
	command -v xorriso >/dev/null 2>&1 || missing=1; \
	command -v pkg-config >/dev/null 2>&1 || missing=1; \
	pkg-config --exists libpng || missing=1; \
	command -v qemu-system-i386 >/dev/null 2>&1 || missing=1; \
	if [ $$missing -eq 1 ]; then \
		echo "Before starting build, Installing some important packages:"; \
		echo "[#] Checking build dependencies... Let's get some orderin'"; \
		command -v nasm >/dev/null 2>&1 || sudo pacman -Sy --noconfirm nasm; \
		command -v gcc >/dev/null 2>&1 || sudo pacman -Sy --noconfirm gcc; \
		command -v ld >/dev/null 2>&1 || sudo pacman -Sy --noconfirm binutils; \
		command -v grub-mkrescue >/dev/null 2>&1 || sudo pacman -Sy --noconfirm grub; \
		command -v xorriso >/dev/null 2>&1 || sudo pacman -Sy --noconfirm xorriso; \
		command -v pkg-config >/dev/null 2>&1 || sudo pacman -Sy --noconfirm pkgconf; \
		pkg-config --exists libpng || sudo pacman -Sy --noconfirm libpng; \
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

build/tools/png2indexed: tools/png2indexed.c | build/tools
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $< $(HOSTLIBS)

build/login.bin: src/bg/login.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/desktop.bin: src/bg/desktop.png build/tools/png2indexed | build
	build/tools/png2indexed $< $@

build/login_asset.o: build/login.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/desktop_asset.o: build/desktop.bin
	$(LD) -m elf_i386 -r -b binary -o $@ $<

build/boot.o: src/boot.asm | build
	$(AS) -f elf32 -o $@ $<

build/interrupts.o: src/interrupts.asm | build
	$(AS) -f elf32 -o $@ $<

build/kernel.o: src/kernel.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL): $(KERNEL_OBJS)
	$(LD) -m elf_i386 $(LDFLAGS) -o $@ $(KERNEL_OBJS)

build/isodir/boot/kernel.bin: $(KERNEL) | build/isodir/boot/grub
	cp $(KERNEL) $@

build/isodir/boot/grub/grub.cfg: grub/grub.cfg | build/isodir/boot/grub
	cp $< $@

$(ISO): build/isodir/boot/kernel.bin build/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/isodir >/dev/null 2>&1
	@echo -e "\nBuild Finished!"

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -m 128 -vga std

clean:
	rm -rf build
	@echo -e "\nClean Done!"

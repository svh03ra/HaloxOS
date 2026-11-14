# HaloxOS
_100% **REAL** OS, Built from **SCRATCH**, Made with **AI**:_

<img width="640" height="480" alt="Test-2026-06-04-07-09-04" src="https://github.com/user-attachments/assets/d2a1409c-045a-4636-842a-33d38c8c0d8e" />

_<sup>(Graphical Demonstration included)</sup>_

## ❓ What's that?
My AI-powered hobby OS aims to be **fully implemented**, stability compatible with ancient machines!
I’ve been handling things carefully and delicately for progress... So what’s going to be build lads?

This was included as part of the Kernel Compression feature, powered by [Zstandard (ZSTD)](https://github.com/facebook/zstd) technology.

_For Version 1.1_: please check the [ZSTD license](https://github.com/facebook/zstd/blob/dev/LICENSE) which I used.


## 🧭 Source Layout
The source tree is arranged around the OS parts that own the behavior:

```
src/
├── config/                 # build-time toggles for debug and default video mode
├── driver/                 # video, input, and other hardware drivers
├── kernel/                 # system core and debug support
└── modes/states/
    ├── boot/               # boot menu and boot terminal state
    └── graphical/
        ├── login/ui/       # graphical login UI assets and code
        └── desktop/
            ├── apps/       # desktop app UI/handling ownership
            └── ui/         # desktop shell, menus, overlays, icons, backgrounds
```

`src/kernel/system/kernel.c` keeps the shared kernel types and state, then includes the split implementation fragments from the ownership folders. This keeps the original base-code behavior while letting driver, kernel, boot, login, desktop UI, and app code live on their new sides.

Build-time defaults live in `src/config/config.h`, including debug mode, default screen resolution, and BPP.

## 📝 System Requirements
Let’s take a look at some requirements you may need for _pinchy salt_!

**Minimum Requirements:**
- **CPU:** `Intel 486` or Fewer
- **RAM:** `5 MB` _(Required to boot from the GRUB bootloader)_
- **VRAM:** `2 MB` or Fewer
- **Run as Boot:** Check the image type commands below to find out why.
- **Architecture:** `32-Bit` _(x86)_ Only, x64 can run as well.

**If you have fewer specifications, you can run it at the same time!**

## 📦 Build Instructions
First of all, you need to understand what you will do in order to follow the instructions such like following a plan on paper:

- **Be quick reminder, this will required for any 🐧 Linux OS only!**
#
**1.** Get to Download Repository: `git clone https://github.com/svh03ra/HaloxOS.git` from a terminal application.

**2.** In the terminal, type `cd HaloxOS` to enter the directory then run `make` to build it.

**3.** Have Fun!

‼️ **Keep in Mind:** Remember, if you don’t have some of the required packages by simply run `make` command for the first time may install them automatically.

**Unboxing the Packages:**
```
- nasm
- gcc
- gcc-multilib
- binutils
- grub-pc-bin
- grub-common
- xorriso
- mtools
- dosfstools
- parted
- zstd
- python3
- pkg-config
- libpng-dev
- qemu-system-x86 (i386)
- qemu-utils (qemu-img, for the converted disk targets)
```

- **If you want to get a latest build from the development progress, you need to compile yourself.**

#

### `>_` Makefile Commands:
**Look for the information to see how it works:**

**- Supports all of these storage media for make with them:**
- Required for `make` command to compile the full build for CD-ROM build _(default opinion)_.
- Required for `make disk` command to compile the full build for Hard Disk build.
- Required for `make floppy` command to compile the full build for Floppy Disk build.
- Required for `make usb` command to compile the full build for USB build to run a physical USB stick.

_This build outputs the generated image files to the `build/out` folder ready to be served._

**- Quick Targets:**
- For using `make run` command to test via QEMU to ensure everything is OK.
- For use the `make run serial` command for debugging:

_Remember to remind this, make sure the `HALOXOS_CONFIG_DEBUG` variable is enabled in `src/config.config.h` before running it to be ready!_
_At the starts from graphical desktop mode, but you just do to open the debugger terminal as press **Ctrl+Shift+Enter** to use._
- For use the `make run-iso`, `make run-disk`, `make run-floppy` and `make run-usb` commands to boot straight into a single media (CD-ROM, Hard Disk, Floppy Disk and USB stick).
- To use `make clean` command to remove build artifacts for quick recovery.

**- Native Disk Images for Other Hypervisors:**
- `make disk vmware` converts the raw disk image to **VMware VMDK** _(VMware Workstation/Player, ESXi)_.
- `make disk qemu` converts it to **QEMU QCOW2** _(QEMU/KVM, Proxmox)_.
- `make disk hyper-v` converts it to **Hyper-V VHD** _(Hyper-V Gen 1, Virtual PC)_.
- `make disk virtualbox` converts it to **VirtualBox VDI**.
- Short spellings work too: `vmw`/`vmdk`, `qcow`/`qcow2`/`qem`, `hv`/`hyperv`/`vhd`, `vb`/`vbox`/`vdi` _(for example `make disk vb`)_.

_The conversion uses `qemu-img`..._

## ⚖️ License:
This repository is licensed under the GNU General Public License. You can view the full license here: [License Available](https://github.com/svh03ra/HaloxOS/blob/main/LICENSE)
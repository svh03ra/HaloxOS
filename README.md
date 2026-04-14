# HaloxOS
_100% **REAL** OS, Built from **SCRATCH**, Made with **AI**:_
<img width="640" height="480" src="https://github.com/user-attachments/assets/192f7fb1-f370-4aba-b947-256ec8b5cf0e" />

_<sup>(Graphical Demonstration included)</sup>_

## ❓ What's that?
This was actually made in VS Code with Codex while I was developing my own OS.
I’ve been handling things carefully and delicately for progress... So what’s going to be build lads?

## 📝 System Requirements
Let’s take a look at some requirements you may need for _pinchy salt_!

**Minimum Requirements:**
- **CPU:** `Intel i386(?)` or Fewer
- **RAM:** `5 MB` _(Required to boot from the GRUB bootloader)_
- **VRAM:** `1 MB` or Fewer
- **Run as Boot:** `CDROM` _(ISO)_ Only, not the disk.
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
- binutils
- grub
- xorriso
- qemu
- pkgconf
- libpng
```

- **If you want to get a latest build from the development progress, you need to compile yourself.**

#

### Makefile Commands:
**Look for the information to see how it works:**
- Required for `make` command to compile the full build.
- For using `make run` command to test via QEMU to ensure everything is OK.
- To use `make clean` command to remove build artifacts for quick recovery.

## ⚖️ License:
This repository is licensed under the GNU General Public License. You can view the full license here: [License Available](https://github.com/svh03ra/HaloxOS/blob/main/LICENSE)

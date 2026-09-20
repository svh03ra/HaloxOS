// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: config.h, build-time configuration header.

// This repository is licensed under the GNU General Public License.

#ifndef HALOXOS_CONFIG_H
#define HALOXOS_CONFIG_H

/*
 * Build-time OS defaults.
 *
 * HALOXOS_CONFIG_DEBUG:
 *   0 = disable COM1 debug tracing and graphical debugger hotkey
 *   1 = enable COM1 debug tracing and graphical debugger hotkey
 *
 * HALOXOS_CONFIG_DEV_MODE:
 *   0 = disable if ready to release build when it's done
 *   1 = enable to show development build for during work in progress
 *
 * HALOXOS_CONFIG_SCREEN_WIDTH / HALOXOS_CONFIG_SCREEN_HEIGHT:
 *   Supported 4:3 modes: 1024x768, 960x720, 800x600, 640x480, 480x360,
 *                       320x240, 192x144
 *   Supported wide modes: 1366x768, 1280x800, 1280x720, 854x480, 640x360,
 *                        426x240, 256x144
 *   The mode the Settings resolution arrows offer; the desktop can then be
 *   re-sized inside it (its canvas follows the mode).
 *
 * HALOXOS_CONFIG_SCREEN_BPP:
 *   4  = true 16-color VGA mode (classic VGA backend only)
 *   8  = 256-color palette mode
 *   16 = true-color 16-bit mode (default; modern hardware / framebuffer backends)
 *
 * HALOXOS_CONFIG_VBE_MODES:
 *   0 = never ask the video BIOS to change modes. The resolution list then
 *       only re-sizes the desktop inside the mode the bootloader set, the way
 *       builds without the VBE path behave.
 *   1 = let the resolution list program real display modes through the video
 *       BIOS on a machine with no programmable video hardware.
 *
 *   Set it to 0 when bringing the OS up on a machine whose firmware does not
 *   survive the real-mode BIOS calls (an early boot that shows nothing, a
 *   machine that resets and beeps): everything else stays exactly the same,
 *   and the machine boots the way it does without this feature.  The probe is
 *   lazy either way - it runs when the resolution list is first needed, never
 *   before the desktop is up.
 */

#define HALOXOS_CONFIG_DEBUG 0
#define HALOXOS_CONFIG_DEV_MODE 0
#define HALOXOS_CONFIG_SCREEN_WIDTH 640
#define HALOXOS_CONFIG_SCREEN_HEIGHT 480
#define HALOXOS_CONFIG_SCREEN_BPP 16
#define HALOXOS_CONFIG_VBE_MODES 1

#endif

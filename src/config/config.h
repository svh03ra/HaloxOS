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
 *   Supported 4:3 modes: 960x720, 640x480, 480x360, 320x240, 192x144
 *   Supported wide modes: 1280x720, 854x480, 640x360, 426x240, 256x144
 *
 * HALOXOS_CONFIG_SCREEN_BPP:
 *   4  = true 16-color VGA mode (classic VGA backend only)
 *   8  = 256-color palette mode
 *   16 = true-color 16-bit mode (default; modern hardware / framebuffer backends)
 */
<<<<<<< HEAD
 
=======

>>>>>>> 6f31922 (Legacy VGA Support)
#define HALOXOS_CONFIG_DEBUG 1
#define HALOXOS_CONFIG_DEV_MODE 1
#define HALOXOS_CONFIG_SCREEN_WIDTH 640
#define HALOXOS_CONFIG_SCREEN_HEIGHT 480
#define HALOXOS_CONFIG_SCREEN_BPP 16

#endif

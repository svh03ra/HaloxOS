#ifndef HALOXOS_CONFIG_H
#define HALOXOS_CONFIG_H

/*
 * Build-time OS defaults.
 *
 * HALOXOS_CONFIG_DEBUG:
 *   0 = disable COM1 debug tracing and graphical debugger hotkey
 *   1 = enable COM1 debug tracing and graphical debugger hotkey
 *
 * HALOXOS_CONFIG_SCREEN_WIDTH / HALOXOS_CONFIG_SCREEN_HEIGHT:
 *   Supported 4:3 modes: 960x720, 640x480, 480x360, 320x240, 192x144
 *   Supported wide modes: 1280x720, 854x480, 640x360, 426x240, 256x144
 *
 * HALOXOS_CONFIG_SCREEN_BPP:
 *   4  = low-color palette mode using an 8-bit framebuffer
 *   8  = default 8-bit palette mode
 *   16 = true-color 16-bit mode when the active video backend supports it
 */
 
#define HALOXOS_CONFIG_DEBUG 0
#define HALOXOS_CONFIG_SCREEN_WIDTH 640
#define HALOXOS_CONFIG_SCREEN_HEIGHT 480
#define HALOXOS_CONFIG_SCREEN_BPP 8

#endif

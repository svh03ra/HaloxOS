// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: doom_os.h, DOOM engine port OS shim for HaloxOS.
//
// This includes the DOOM 1997 source code, written by id Software Inc. Please check the license in the current folder.
// This repository is licensed under the GNU General Public License.
//
// DOOM (linuxdoom-1.10, Copyright (C) 1993-1996 by id Software, Inc.,
// released under the DOOM Source Code License) ported to run as a
// HaloxOS desktop app. The original source tree is kept verbatim at
// src/modes/states/graphical/desktop/apps/DOOM/; this shim adapts the
// OS-dependent layer (i_system / i_video / i_sound / i_net / w_wad /
// m_argv / m_misc / d_main / d_net) to the HaloxOS kernel.

#ifndef DOOM_OS_H
#define DOOM_OS_H

/* The port is one big translation unit included from the kernel
 * amalgamation, so DOOM's engine globals live as statics here. Guard
 * everything so a second include is harmless. */
#ifndef DOOM_OS_INCLUDED
#define DOOM_OS_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* ---- tiny libc surface the DOOM engine expects ---------------- */

int doom_strcmp(const char *a, const char *b);
int doom_strncmp(const char *a, const char *b, size_t n);
int doom_strcasecmp(const char *a, const char *b);
char *doom_strcpy(char *dest, const char *src);
char *doom_strncpy(char *dest, const char *src, size_t n);
char *doom_strcat(char *dest, const char *src);
size_t doom_strlen(const char *s);
void *doom_memcpy(void *dest, const void *src, size_t n);
void *doom_memset(void *dest, int value, size_t n);
int doom_memcmp(const void *a, const void *b, size_t n);
int doom_toupper(int c);
int doom_atoi(const char *s);

/* sprintf into a caller buffer (printf-style, subset: %s %d %i %c %x %02d
 * and %% only). The engine needs it for lump names and save headers. */
int doom_sprintf(char *dest, const char *fmt, ...);
int doom_vsprintf(char *dest, const char *fmt, va_list ap);

/* printf replacement: debug builds mirror to COM1 serial trace. */
int doom_printf(const char *fmt, ...);

/* malloc/calloc/free on a bump arena carved out of the DOOM zone; used
 * only by engine code paths that real libc'd (d_main title buffers,
 * i_net tables). Never returns NULL - I_Error's instead. */
void *doom_malloc(size_t size);
void doom_free(void *ptr);

/* ---- HaloxOS glue API (implemented in doom_glue.c) ------------- */

/* One engine frame: TryRunTics + D_Display. Called ~35+ tics/s while
 * the DOOM window is the active app. Returns 0 while the engine runs,
 * nonzero when it quit or errored. */
int doom_frame(void);

/* Push one input event into the engine (key/mouse). */
void doom_post_key_event(int key_down, int doom_key);
void doom_post_mouse_motion(int dx, int dy);
void doom_post_mouse_button(int button, bool down);

/* Latest 320x200 output frame, palette-indexed. Rendered by glue. */
extern uint8_t doom_screen[320 * 200];

/* Engine lifecycle. */
typedef enum {
    DOOM_STATE_OFF = 0,     /* never started */
    DOOM_STATE_NO_WAD = 1,  /* WAD missing: show error window */
    DOOM_STATE_ERROR = 3,   /* engine I_Error: show message */
    DOOM_STATE_RUNNING = 2  /* engine alive */
} DoomRunState;

/* Unwind + event helpers (doom_hosted.c): declared early because the
 * OS shim (doom_i_system.c / doom_wad_api.c) is compiled before the
 * hosted driver in the amalgamation. */
void doom_longjmp_out(void);
void doom_pump_events(void);
extern int doom_run_state;

/* Boot the engine from DOOM1.WAD sectors; returns new doom_run_state.
 * Called on first DOOM window open. */
int doom_boot(void);

/* Map raw ASCII/ch to DOOM key codes (doomdef.h KEY_*). */
int doom_xlate_ascii(char ch);

/* Did D_DoomMain request a quit (I_Quit)? */
extern int doom_quit_requested;

/* Formatted I_Error text (empty string when none). */
const char *doom_get_error_text(void);

/* Clear the recorded I_Error text (start of every engine boot). */
void doom_clear_error_text(void);

#endif /* DOOM_OS_INCLUDED */
#endif /* DOOM_OS_H */

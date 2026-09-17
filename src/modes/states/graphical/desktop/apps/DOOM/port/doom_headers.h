// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: doom_headers.h, DOOM engine port: libc/POSIX mapping.
//
// This includes the DOOM 1997 source code, written by id Software Inc. Please check the license in the current folder.
// This repository is licensed under the GNU General Public License.
//
// The kernel is freestanding: the engine's <stdio.h> etc. includes hit
// EMPTY stubs in port/include/ (put on the include path ahead of the
// system dirs by the Makefile), and this header re-declares every libc
// and POSIX symbol the engine uses, mapped onto the port's shims.

#ifndef DOOM_HEADERS_H
#define DOOM_HEADERS_H

/* This source is compiled as part of the HaloxOS freestanding port, not
 * as linuxdoom's NORMALUNIX target.  Keep that distinction: defining
 * NORMALUNIX would also enable host-only file and signal paths.  The one
 * portable block we deliberately opt into is the original control-default
 * table in m_misc.c (see its HALOXOS_DOOM_PORT guard). */
#define HALOXOS_DOOM_PORT 1

#include "doom_os.h"

/* ---- kernel KeyCode snapshot ----
 * The engine's doomdef.h later #defines KEY_ENTER/KEY_TAB/KEY_F1... as
 * DOOM key codes, clobbering the kernel's KeyCode enum names. Snapshot
 * the kernel enum values here (before doomdef.h is included) so the
 * app's input bridge can still match on them afterwards. */
enum {
    DOOM_KC_NONE = 0, DOOM_KC_ENTER, DOOM_KC_ESC, DOOM_KC_BACKSPACE,
    DOOM_KC_TAB, DOOM_KC_F1, DOOM_KC_F2, DOOM_KC_F3, DOOM_KC_F4,
    DOOM_KC_F5, DOOM_KC_F6, DOOM_KC_F7, DOOM_KC_F8, DOOM_KC_F9,
    DOOM_KC_F10, DOOM_KC_F11, DOOM_KC_F12, DOOM_KC_PAUSE, DOOM_KC_UP,
    DOOM_KC_DOWN, DOOM_KC_LEFT, DOOM_KC_RIGHT, DOOM_KC_DEL,
    DOOM_KC_HOME, DOOM_KC_END
};
_Static_assert(DOOM_KC_ENTER == KEY_ENTER && DOOM_KC_TAB == KEY_TAB &&
               DOOM_KC_F1 == KEY_F1 && DOOM_KC_F2 == KEY_F2 &&
               DOOM_KC_F3 == KEY_F3 && DOOM_KC_F4 == KEY_F4 &&
               DOOM_KC_F5 == KEY_F5 && DOOM_KC_F6 == KEY_F6 &&
               DOOM_KC_F7 == KEY_F7 && DOOM_KC_F8 == KEY_F8 &&
               DOOM_KC_F9 == KEY_F9 && DOOM_KC_F10 == KEY_F10 &&
               DOOM_KC_F11 == KEY_F11 && DOOM_KC_F12 == KEY_F12 &&
               DOOM_KC_PAUSE == KEY_PAUSE && DOOM_KC_UP == KEY_UP &&
               DOOM_KC_DOWN == KEY_DOWN && DOOM_KC_LEFT == KEY_LEFT &&
               DOOM_KC_RIGHT == KEY_RIGHT && DOOM_KC_BACKSPACE == KEY_BACKSPACE &&
               DOOM_KC_ESC == KEY_ESC, "kernel KeyCode order changed");

/* ---- boolean/byte ----
 * The kernel pulls <stdbool.h> before this amalgamation, so DOOM's
 * `typedef enum {false,true} boolean` would collide with the stdbool
 * macros. Drop the macros here so doomtype.h's ORIGINAL definition
 * stands: boolean is a 4-byte enum, which the engine RELIES on
 * (animdefs[] terminates with {-1} tested via `istexture != -1`).
 * Port files below use `bool` (stdbool) directly and treat the
 * false/true enum constants as plain 0/1 ints, which is identical
 * in every context used. */
#undef false
#undef true

/* values.h / limits (pre-define so doomtype.h's copies are no-ops) */
#define MAXCHAR ((char)0x7f)
#define MAXSHORT ((short)0x7fff)
#define MAXINT ((int)0x7fffffff)
#define MAXLONG ((int)0x7fffffff)
#define MINCHAR ((char)0x80)
#define MINSHORT ((short)0x8000)
#define MININT ((int)0x80000000)
#define MINLONG ((int)0x80000000)
#define __DOOMTYPE_LIMITS__

/* rcsid: every engine .c defines `static const char rcsid[]`. In the
 * one-TU amalgamation those redefinitions must not collide, so rcsid
 * is renamed to a unique identifier per included file via __COUNTER__.
 * Each engine include in doom_engine.c re-#defines this. */
#define DOOM_RCSID_UNIQUE \
    CAT2(doom_rcsid_, __COUNTER__)
#define CAT2_(a,b) a##b
#define CAT2(a,b) CAT2_(a,b)
#define rcsid DOOM_RCSID_UNIQUE

/* ---- opaque FILE (debugfile / sndserver pointers, never deref'd) --- */
typedef struct doom_file_s { int unused; } FILE;

/* stdio/stderr only appear as tokens in fprintf(stderr,...) etc. */
#define stderr ((FILE *)0)
#define stdout ((FILE *)1)
#define NULL ((void *)0)

/* ---- string.h / ctype.h / stdlib.h ---- */
#define strcmp  doom_strcmp
#define strncmp doom_strncmp
#define strcasecmp doom_strcasecmp
#define strncasecmp doom_strncasecmp
#define strcpy doom_strcpy
#define strncpy doom_strncpy
#define strcat doom_strcat
#define strlen doom_strlen
#define memcpy doom_memcpy
#define memset doom_memset
#define memcmp doom_memcmp
#define toupper doom_toupper
#define tolower doom_tolower
#define atoi doom_atoi
#define sprintf doom_sprintf
#define vsprintf doom_vsprintf

int doom_printf(const char *fmt, ...);
#define printf doom_printf
int doom_fprintf_stub(FILE *f, const char *fmt, ...);
#define fprintf doom_fprintf_stub
int doom_fputs_shim(const char *s);
#define fputs(s, f) doom_fputs_shim(s)
int doom_puts_shim(const char *s);
#define puts(s) doom_puts_shim(s)

/* stdio functions that read real files: none exist. Save-load menus
 * probe slots with these; failing everything keeps empty slots. */
FILE *doom_fopen_stub(const char *name, const char *mode);
#define fopen(a, b) doom_fopen_stub(a, b)
int doom_fclose_stub(FILE *f);
#define fclose(f) doom_fclose_stub(f)
#define feof(f) 1
#define fread(p, s, n, f) 0
#define fwrite(p, s, n, f) ((size_t)(n))
#define fseek(f, o, w) 0
#define ftell(f) 0
#define fscanf(f, ...) 0
#define fflush(f) 0
#define setbuf(f, b) ((void)0)
int doom_sscanf_stub(const char *s, const char *fmt, ...);
#define sscanf doom_sscanf_stub

/* ---- POSIX file API: only the on-disk IWAD is real ---- */
int doom_wad_open_compat(const char *name);
int doom_wad_read(int handle, void *buf, int count);
int doom_wad_lseek(int handle, int offset, int whence);
int doom_wad_close(int handle);
int doom_wad_write_stub(int handle, const void *buf, int count);

/* struct stat replacement: the engine only reads st_size. */
struct stat { int st_size; };
int doom_wad_fstat_stub(int handle, struct stat *st);
int doom_wad_access_stub(const char *name, int mode);
int doom_wad_stat_stub(const char *name, struct stat *st);

#define DOOM_WAD_HANDLE 77
#define open(name, ...) doom_wad_open_compat(name)
#define close(h) doom_wad_close(h)
#define read(h, buf, count) doom_wad_read(h, buf, count)
#define write(h, buf, count) doom_wad_write_stub(h, buf, count)
#define lseek(h, off, whence) doom_wad_lseek(h, off, whence)
#define fstat(h, st) doom_wad_fstat_stub(h, st)
#define access(name, mode) doom_wad_access_stub(name, mode)

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_TRUNC 0x80
#define O_BINARY 0
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define R_OK 4
#define W_OK 2
#define F_OK 0

/* ---- malloc/free/realloc ---- */
void *doom_malloc(size_t size);
void doom_free(void *ptr);
void *doom_realloc(void *ptr, size_t size);
#define malloc doom_malloc
#define calloc(n, s) doom_malloc((size_t)(n) * (size_t)(s))
#define free doom_free
#define realloc doom_realloc

/* d_main.c dead paths (never reached in the hosted port) */
#define mkdir(path, mode) (-1)
#define getchar() (0)

/* ---- alloca (r_data.c texture scratch) ---- */
void *doom_alloca(size_t size);
#define alloca doom_alloca

/* ---- misc POSIX ---- */
#define exit(n) doom_exit_stub(n)
void doom_exit_stub(int code);
#define getpid() 1
#define sleep(x) ((void)(x))
#define signal(sig, fn) ((void *)0)
#define SIGINT 2
#define getenv(name) ((char *)0)

/* abs() used by m_fixed.c / p_enemy.c */
int doom_abs_stub(int v);
#define abs doom_abs_stub

#endif /* DOOM_HEADERS_H */

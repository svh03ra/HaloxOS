// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: doom_wad_api.c, DOOM engine port: file API over the WAD.
//
// This includes the DOOM 1997 source code, written by id Software Inc. Please check the license in the current folder.
// This repository is licensed under the GNU General Public License.
//
// The engine's w_wad.c / m_misc.c / d_main.c call open/read/lseek/
// close/fstat/access on the single IWAD. This file implements those
// calls over doom_wad_read() from doom_libc.c, tracking a per-handle
// file offset just like a real fd.

#include "doom_os.h"

#ifndef DOOM_WAD_API_INCLUDED
#define DOOM_WAD_API_INCLUDED

/* ---- extra libc bits declared in doom_headers.h ---- */

int doom_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        int ca = doom_toupper((int)(unsigned char)a[i]);
        int cb = doom_toupper((int)(unsigned char)b[i]);
        if (ca != cb) {
            return ca - cb;
        }
        if (ca == 0) {
            return 0;
        }
    }
    return 0;
}

int doom_tolower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }
    return c;
}

int doom_abs_stub(int v) {
    return v < 0 ? -v : v;
}

int doom_puts_shim(const char *s) {
    serial_trace_concat("DOOM", " ", s);
    return 0;
}

int doom_fprintf_stub(FILE *f, const char *fmt, ...) {
    char buffer[160];
    va_list ap;

    (void)f;
    va_start(ap, fmt);
    doom_vsprintf(buffer, fmt, ap);
    va_end(ap);

    serial_trace_concat("DOOM", " ", buffer);
    return (int)doom_strlen(buffer);
}

int doom_fclose_stub(FILE *f) {
    (void)f;
    return 0;
}

FILE *doom_fopen_stub(const char *name, const char *mode) {
    (void)name; (void)mode;
    return NULL; /* no read-write filesystem: all FILE* users no-op */
}

int doom_sscanf_stub(const char *s, const char *fmt, ...) {
    (void)s; (void)fmt;
    return 0; /* only used by M_LoadDefaults config parsing (no disk) */
}

void doom_exit_stub(int code) {
    (void)code;
    doom_quit_requested = 1;
    doom_longjmp_out();
}

/* ---- alloca pool (r_data.c texture merge scratch) ---- */

#define DOOM_ALLOCA_POOL (96 * 1024)
static uint8_t doom_alloca_pool[DOOM_ALLOCA_POOL];
static size_t doom_alloca_used = 0;

void *doom_alloca(size_t size) {
    if (size == 0) {
        size = 1;
    }
    size = (size + 15u) & ~(size_t)15u;
    if (doom_alloca_used + size > DOOM_ALLOCA_POOL) {
        I_Error("alloca pool exhausted");
    }
    void *p = &doom_alloca_pool[doom_alloca_used];
    doom_alloca_used += size;
    return p;
}

/* alloca lifetimes: r_data.c allocates within one function call and
 * never frees; the pool is reset at each engine boot and each level
 * load (R_Init / P_SetupLevel finish before the next burst). */
void doom_alloca_reset(void) {
    doom_alloca_used = 0;
}

/* ---- fd-style API over the on-disk IWAD ---- */

static int doom_fd_offset = 0;
static int doom_fd_open = 0;

/* The only real file is the mounted IWAD.  The raw-media format has no
 * filename, so accept each canonical IWAD name that linuxdoom probes;
 * the hosted boot path selects the right one from its map markers.  A
 * PWAD is not independently runnable and therefore is deliberately not
 * exposed as a standalone file. */
static bool doom_name_is_wad(const char *name) {
    if (name == NULL) {
        return false;
    }
    /* find the basename after the last slash/backslash */
    const char *base = name;
    for (const char *p = name; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return doom_strcasecmp(base, "doom1.wad") == 0 ||
           doom_strcasecmp(base, "doom.wad") == 0 ||
           doom_strcasecmp(base, "doomu.wad") == 0 ||
           doom_strcasecmp(base, "doom2.wad") == 0;
}

int doom_wad_open_compat(const char *name) {
    if (!doom_wad_probe_internal() || !doom_name_is_wad(name)) {
        return -1;
    }
    doom_fd_open = 1;
    doom_fd_offset = 0;
    return DOOM_WAD_HANDLE;
}

int doom_wad_close(int handle) {
    (void)handle;
    doom_fd_open = 0;
    return 0;
}

int doom_wad_read(int handle, void *buf, int count) {
    (void)handle;
    if (!doom_fd_open || count <= 0) {
        return 0;
    }
    int done = doom_wad_read_at((uint32_t)doom_fd_offset, buf, count);
    doom_fd_offset += done;
    return done;
}

int doom_wad_lseek(int handle, int offset, int whence) {
    (void)handle;
    int size = doom_wad_size_internal();
    int base = whence == SEEK_SET ? 0 : (whence == SEEK_CUR ? doom_fd_offset : size);
    int target = base + offset;
    if (target < 0) {
        target = 0;
    }
    if (target > size) {
        target = size;
    }
    doom_fd_offset = target;
    return target;
}

int doom_wad_write_stub(int handle, const void *buf, int count) {
    /* Savegames / screenshots: no writable filesystem yet. */
    (void)handle; (void)buf;
    return count;
}

int doom_wad_fstat_stub(int handle, struct stat *st) {
    st->st_size = handle == DOOM_WAD_HANDLE ? doom_wad_size_internal() : 0;
    return 0;
}

int doom_wad_access_stub(const char *name, int mode) {
    (void)mode;
    /* The only file the port can see is the mounted IWAD. */
    return doom_name_is_wad(name) && doom_wad_probe_internal() ? 0 : -1;
}

int doom_wad_stat_stub(const char *name, struct stat *st) {
    (void)name;
    st->st_size = doom_wad_probe_internal() ? doom_wad_size_internal() : 0;
    return doom_wad_probe_internal() ? 0 : -1;
}

void doom_wad_reset_cache(void) {
    doom_wad_dir_cached = false;
    doom_fd_open = 0;
    doom_fd_offset = 0;
    /* Revalidate the selected source on a later app launch.  This keeps an
     * ATA/ATAPI error from leaving a stale medium descriptor behind. */
    doom_wad_present = false;
    doom_memset(&doom_wad_medium, 0, sizeof(doom_wad_medium));
    doom_wad_embed_ptr = NULL;
    doom_wad_embed_size_cache = 0xFFFFFFFFu;
}

#endif /* DOOM_WAD_API_INCLUDED */

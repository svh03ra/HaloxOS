/* Copyright Svh03ra (C) 2026, All rights reserved */
/* Source File: loader/include/stdio.h, freestanding libc shim for zstd. */

/* This repository is licensed under the GNU General Public License. */

#ifndef LOADER_SHIM_STDIO_H
#define LOADER_SHIM_STDIO_H

/* zstd's DEBUG build paths reference fprintf; the release decompressor
 * with ZSTD_DEBUG=0 never calls it, but keep the declaration harmless. */
int fprintf(void *stream, const char *format, ...);

#endif

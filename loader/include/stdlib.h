/* Copyright Svh03ra (C) 2026, All rights reserved */
/* Source File: loader/include/stdlib.h, freestanding libc shim for zstd. */

/* This repository is licensed under the GNU General Public License. */

#ifndef LOADER_SHIM_STDLIB_H
#define LOADER_SHIM_STDLIB_H

#define NULL ((void *)0)

/* zstd decompression never allocates when using ZSTD_initStaticDCtx, but
 * the library still references these symbols at link time. */
void *malloc(unsigned long size);
void *calloc(unsigned long count, unsigned long size);
void free(void *ptr);

#endif

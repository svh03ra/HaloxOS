/* Copyright Svh03ra (C) 2026, All rights reserved */
/* Source File: loader/include/string.h, freestanding libc shim for zstd. */

/* This repository is licensed under the GNU General Public License. */

#ifndef LOADER_SHIM_STRING_H
#define LOADER_SHIM_STRING_H

/*
 * zstd's zstd_deps.h includes <string.h> and calls memcpy/memset/memmove
 * through __builtin_* helpers, which the compiler lowers to plain symbol
 * calls (memmove with a runtime size can never be inlined).  So the real
 * symbols with the standard names must exist: loader/shim.c provides
 * them.  Declarations only here - no #define renaming.
 */

typedef unsigned long size_t_shim;

void *memcpy(void *dest, const void *src, size_t_shim n);
void *memmove(void *dest, const void *src, size_t_shim n);
void *memset(void *dest, int value, size_t_shim n);

#endif

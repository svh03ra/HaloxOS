// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: shim.c, freestanding libc shim for the vendored zstd.
//
// This repository is licensed under the GNU General Public License.

/*
 * zstd's __builtin_memmove/memcpy/memset with a runtime size lower to
 * plain symbol calls, so the loader must provide the standard names.
 * The byte loops use volatile pointers so GCC never recognizes them as
 * its own builtin idioms (which would turn them into recursive calls).
 */

void *memcpy(void *dest, const void *src, unsigned long n) {
    volatile unsigned char *d = (volatile unsigned char *)dest;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, unsigned long n) {
    volatile unsigned char *d = (volatile unsigned char *)dest;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

void *memset(void *dest, int value, unsigned long n) {
    volatile unsigned char *d = (volatile unsigned char *)dest;
    while (n--) {
        *d++ = (unsigned char)value;
    }
    return dest;
}

/* Never called with ZSTD_initStaticDCtx; present only to satisfy links. */
void *malloc(unsigned long size) {
    (void)size;
    return 0;
}

void *calloc(unsigned long count, unsigned long size) {
    (void)count;
    (void)size;
    return 0;
}

void free(void *ptr) {
    (void)ptr;
}

int fprintf(void *stream, const char *format, ...) {
    (void)stream;
    (void)format;
    return 0;
}

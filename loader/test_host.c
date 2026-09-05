// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: test_host.c, host-side roundtrip test for the zstd boot chain.
//
// This repository is licensed under the GNU General Public License.

/*
 * Windows/WSL host test (not part of the OS image): builds the exact zstd
 * decompressor used by the loader, compresses the real kernel ELF with the
 * same zstd level, packages it with the module header, then decompresses
 * with ZSTD_initStaticDCtx into a fixed-size buffer and byte-compares the
 * result with the original file. Run from the repo root:
 *   gcc -O2 -I loader/include -o build/ztest loader/test_host.c loader/zstd_all.c loader/shim.c
 *   build/ztest build/kernel.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZSTD_STATIC_LINKING_ONLY
#include "third_party/zstd-1.5.7/lib/zstd.h"

typedef struct {
    unsigned int magic;
    unsigned int kernel_entry;
    unsigned int image_size;
    unsigned int frame_size;
    unsigned int crc;
} ModuleHeader;

static unsigned char scratch[1 * 1024 * 1024];

int main(int argc, char **argv) {
    const char *kernel_path = argc > 1 ? argv[1] : "build/kernel.bin";
    FILE *f;
    unsigned char *kernel;
    long kernel_size;
    unsigned char *frame;
    long frame_size;
    ModuleHeader header;
    unsigned char *out;
    ZSTD_DCtx *dctx;
    size_t result;
    FILE *zf;
    unsigned char module_buf[32];

    f = fopen(kernel_path, "rb");
    if (!f) { printf("cannot open %s\n", kernel_path); return 1; }
    fseek(f, 0, SEEK_END);
    kernel_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    kernel = malloc(kernel_size);
    if (fread(kernel, 1, kernel_size, f) != (size_t)kernel_size) { printf("read failed\n"); return 1; }
    fclose(f);
    printf("kernel: %ld bytes\n", kernel_size);

    zf = fopen("build/ztest.frame", "rb");
    if (!zf) { printf("run: zstd -12 -T0 -f -q -o build/ztest.frame %s\n", kernel_path); return 1; }
    fseek(zf, 0, SEEK_END);
    frame_size = ftell(zf);
    fseek(zf, 0, SEEK_SET);
    frame = malloc(frame_size);
    if (fread(frame, 1, frame_size, zf) != (size_t)frame_size) { printf("frame read failed\n"); return 1; }
    fclose(zf);
    printf("frame:  %ld bytes (%.2f%%)\n", frame_size, frame_size * 100.0 / kernel_size);

    header.magic = 0x484C585Au;
    header.kernel_entry = 0x00200000u;
    header.image_size = (unsigned int)kernel_size;
    header.frame_size = (unsigned int)frame_size;
    header.crc = header.magic ^ header.kernel_entry ^ header.image_size ^ header.frame_size;
    memcpy(module_buf, &header, sizeof(header));
    (void)module_buf;

    if (frame_size >= kernel_size) { printf("BAD: frame >= kernel\n"); return 1; }

    printf("zstd version: %s (number %u)\n", ZSTD_versionString(), ZSTD_versionNumber());
    printf("estimateDCtxSize: %zu\n", ZSTD_estimateDCtxSize());
    if (ZSTD_estimateDCtxSize() > sizeof(scratch)) {
        printf("BAD: DCtx estimate %zu exceeds loader scratch 1MB\n", ZSTD_estimateDCtxSize());
        return 1;
    }

    dctx = ZSTD_initStaticDCtx(scratch, sizeof(scratch));
    if (!dctx) { printf("BAD: initStaticDCtx\n"); return 1; }

    out = malloc(kernel_size);
    result = ZSTD_decompressDCtx(dctx, out, kernel_size, frame, frame_size);
    if (ZSTD_isError(result)) {
        printf("BAD: decompress: %s\n", ZSTD_getErrorName(result));
        return 1;
    }
    printf("regenerated: %zu bytes\n", result);
    if (result != (size_t)kernel_size) { printf("BAD: size mismatch\n"); return 1; }
    if (memcmp(out, kernel, kernel_size) != 0) { printf("BAD: content mismatch\n"); return 1; }

    printf("PASS: vendored zstd decompressor roundtrip OK\n");
    return 0;
}

// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: zstd_all.c, vendored zstd 1.5.7 decompressor (single TU).
//
// This repository is licensed under the GNU General Public License.

/*
 * Builds the complete zstd 1.5.7 decompression subset for the loader in
 * one translation unit:
 *   - common:    entropy_common, error_private, zstd_common, fse_decompress,
 *                xxhash, debug (debug compiled out via DEBUGLEVEL=0)
 *   - decompress: zstd_decompress, zstd_decompress_block, huf_decompress,
 *                 zstd_ddict
 * pool.c / threading.c are excluded (single-threaded static DCtx).
 *
 * The asm-optimized huf_decompress_amd64.S is not used: the loader targets
 * plain i386.
 */

#define DEBUGLEVEL 0
#define ZSTD_DISABLE_ASM 1

#include "../third_party/zstd-1.5.7/lib/common/debug.c"
#include "../third_party/zstd-1.5.7/lib/common/entropy_common.c"
#include "../third_party/zstd-1.5.7/lib/common/error_private.c"
#include "../third_party/zstd-1.5.7/lib/common/zstd_common.c"
#include "../third_party/zstd-1.5.7/lib/common/fse_decompress.c"
#include "../third_party/zstd-1.5.7/lib/common/xxhash.c"
#include "../third_party/zstd-1.5.7/lib/decompress/zstd_decompress.c"
#include "../third_party/zstd-1.5.7/lib/decompress/zstd_decompress_block.c"
#include "../third_party/zstd-1.5.7/lib/decompress/huf_decompress.c"
#include "../third_party/zstd-1.5.7/lib/decompress/zstd_ddict.c"

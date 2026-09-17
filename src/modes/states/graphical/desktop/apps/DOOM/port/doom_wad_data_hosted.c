// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: doom_wad_data_hosted.c, glue between the embedded WAD
// image and the WAD reader's medium discovery.
//
// This includes the DOOM 1997 source code, written by id Software Inc. Please check the license in the current folder.
// This repository is licensed under the GNU General Public License.
//
// The WAD reader (doom_libc.c) and the embedded WAD image
// (doom_wad_data.c) live in separate translation units now: the engine
// amalgamation is one unit, the WAD data another (a 4 MB static array
// inside the amalgamation exceeded what a single kernel object should
// carry). This file exposes the image to the reader as a plain function.

#include "doom_os.h"
#include "doom_wad_embed.h"

#ifndef DOOM_WAD_DATA_HOSTED_INCLUDED
#define DOOM_WAD_DATA_HOSTED_INCLUDED

/* Reader hook (doom_libc.c). */
const uint8_t *doom_wad_embed_get(uint32_t *size) {
    *size = doom_wad_data_size;
    return doom_wad_data;
}

#endif /* DOOM_WAD_DATA_HOSTED_INCLUDED */

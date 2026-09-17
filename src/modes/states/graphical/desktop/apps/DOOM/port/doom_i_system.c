// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: doom_i_system.c, DOOM engine port: OS layer for HaloxOS.
//
// This includes the DOOM 1997 source code, written by id Software Inc. Please check the license in the current folder.
// This repository is licensed under the GNU General Public License.
//
// Replaces i_system.c, i_video.c, i_sound.c and i_net.c from
// linuxdoom-1.10 (Copyright (C) 1993-1996 by id Software, Inc.):
//  - I_GetTime runs off the HaloxOS 60 Hz timer re-scaled to DOOM's
//    35 Hz TICRATE.
//  - I_ZoneBase points the DOOM zone allocator at a static memory
//    block above the kernel image (the machine must have >= 8 MB).
//  - Video: engine draws palette-indexed into doom_screen (320x200);
//    I_FinishUpdate marks the frame dirty for the HaloxOS app window.
//  - Sound: silent stubs (no audio hardware in the OS yet).
//  - Net: single-player only (doomcom built locally, no sockets).

#include "doom_os.h"

/* Generated boot-chain addresses (see the Makefile's ram_requirement rule). */
#include "ram_requirement.h"

#ifndef DOOM_I_SYSTEM_INCLUDED
#define DOOM_I_SYSTEM_INCLUDED

/* Engine-side includes of OS headers resolve here through doom_os.h's
 * libc shim + the engine's own headers included by doom_engine.c
 * BEFORE this file. */

/* ---- i_system.c ---- */

int mb_used = 6;

/* Port run state (doom_os.h): OFF / NO_WAD / RUNNING. */
int doom_run_state = DOOM_STATE_OFF;
int doom_quit_requested = 0;

/* The hosted engine remains resident after its window closes.  Unlike the
 * original executable, its static boot allocations must be reset explicitly
 * before a second launch. */
static int doom_time_base = -1;
static int doom_time_frozen = 0;
static bool doom_clock_is_paused = false;
#define DOOM_LOW_POOL_BYTES (320u * 200u * 4u)
static uint8_t doom_low_pool[DOOM_LOW_POOL_BYTES];
static size_t doom_low_used = 0;

/* The DOOM zone is carved from free RAM above the kernel, not a static .bss
 * block.  Normal builds start at 0x800000.  When GRUB supplies a WAD module
 * and the loader has to park it near the top of a 16 MB-class machine, the
 * zone's upper bound becomes the lower of the reported usable-RAM ceiling and
 * the WAD module start, so the heap can never overwrite the WAD. */
#ifdef DOOM_WAD_EMBED_BUILD
#define DOOM_ZONE_DEFAULT_BASE 0x1500000u
#define DOOM_ZONE_MAX_BYTES (4u * 1024u * 1024u)
#define DOOM_ZONE_MIN_MACHINE (22u * 1024u * 1024u)
#else
/* The zone starts just above the loader handoff block (span end rounded
 * to 64 KiB + one 64 KiB guard - about 5.5 MB with the current image) and
 * grows toward the WAD module (which the loader parks at 0xE00000 or
 * higher). Starting at the old fixed 0x800000 would strand ~2.5 MB of
 * free RAM below it on every machine - RAM a 6 MB boot now cannot spare
 * anyway, since the old base sat above what a 6 MB machine even has. */
#define DOOM_ZONE_DEFAULT_BASE (HALOXOS_LOADER_INFO_ADDR + 0x10000u)
#define DOOM_ZONE_MAX_BYTES (6u * 1024u * 1024u)
#define DOOM_ZONE_MIN_MACHINE (8u * 1024u * 1024u)
#endif
static uint32_t doom_zone_base = DOOM_ZONE_DEFAULT_BASE;
static int doom_zone_size = 0;

static bool doom_zone_available(void) {
    extern uint32_t ram_total_bytes;
    extern uint32_t doom_wad_module_addr;
    extern uint32_t doom_wad_module_bytes;
    uint32_t avail;

    if (doom_zone_size != 0) {
        return true;
    }
    if (ram_total_bytes < DOOM_ZONE_MIN_MACHINE) {
        return false; /* machine needs the minimum above for the zone */
    }

    doom_zone_base = DOOM_ZONE_DEFAULT_BASE;
#ifndef DOOM_WAD_EMBED_BUILD
    /* A WAD loaded by GRUB may be parked in the upper part of a 16 MB-class
     * machine.  Keep the DOOM heap strictly below that module so the heap
     * can never overwrite the WAD while the engine is reading lumps from RAM. */
    if (doom_wad_module_addr != 0 && doom_wad_module_bytes != 0) {
        uint32_t zone_end = doom_wad_module_addr;
        if (zone_end > ram_total_bytes) {
            zone_end = ram_total_bytes;
        }
        if (zone_end <= DOOM_ZONE_DEFAULT_BASE) {
            return false;
        }
        avail = zone_end - DOOM_ZONE_DEFAULT_BASE;
    } else {
        if (ram_total_bytes <= DOOM_ZONE_DEFAULT_BASE) {
            return false;
        }
        avail = ram_total_bytes - DOOM_ZONE_DEFAULT_BASE;
    }
#else
    if (ram_total_bytes <= DOOM_ZONE_DEFAULT_BASE) {
        return false;
    }
    avail = ram_total_bytes - DOOM_ZONE_DEFAULT_BASE;
#endif

    if (avail > DOOM_ZONE_MAX_BYTES) {
        avail = DOOM_ZONE_MAX_BYTES;
    }
    /* Leave a hard minimum rather than handing Z_Init a tiny zone that can
     * boot but immediately corrupt/fail later during WAD resource loading. */
    if (avail < (2u * 1024u * 1024u)) {
        return false;
    }
    doom_zone_size = (int)avail;
    serial_trace_hex_value("DOOM", "DOOM zone base", doom_zone_base);
    serial_trace_uint_value("DOOM", "DOOM zone bytes", (uint32_t)doom_zone_size);
    return true;
}

void I_Tactile(int on, int off, int total) {
    (void)on; (void)off; (void)total;
}

static ticcmd_t emptycmd;
ticcmd_t *I_BaseTiccmd(void) {
    return &emptycmd;
}

int I_GetHeapSize(void) {
    return doom_zone_available() ? doom_zone_size : 0;
}

/* The DOOM zone lives in free RAM above the kernel span (see
 * doom_zone_available). On machines with too little RAM, I_Error renders
 * the "not enough memory" error window in the DOOM app instead. */
byte *I_ZoneBase(int *size) {
    if (!doom_zone_available()) {
        I_Error("Not enough RAM for DOOM\n(need 10 MB, machine has less)");
    }
    *size = doom_zone_size;
    return (byte *)(uintptr_t)doom_zone_base;
}

/* 60 Hz kernel timer -> 35 Hz DOOM tics. */
int I_GetTime(void) {
    int now = (int)(timer_ticks * TICRATE / TIMER_HZ);
    if (doom_time_base == -1) {
        doom_time_base = now;
    }
    if (doom_clock_is_paused) {
        return doom_time_frozen;
    }
    return now - doom_time_base;
}

/* Do not make TryRunTics replay minutes of elapsed time when a DOOM window
 * is behind another application.  The game clock keeps its current value and
 * resumes from there as soon as the window becomes active again. */
void doom_clock_pause(void) {
    if (!doom_clock_is_paused) {
        doom_time_frozen = I_GetTime();
        doom_clock_is_paused = true;
    }
}

void doom_clock_resume(void) {
    int now;

    if (!doom_clock_is_paused) {
        return;
    }
    now = (int)(timer_ticks * TICRATE / TIMER_HZ);
    doom_time_base = now - doom_time_frozen;
    doom_clock_is_paused = false;
}

void I_Init(void) {
    /* sound init intentionally skipped (no audio driver in HaloxOS) */
    serial_trace("DOOM", "I_Init: machine state ready (HaloxOS port)");
}

void I_Quit(void) {
    doom_quit_requested = 1;
    serial_trace("DOOM", "I_Quit: engine shutdown requested");
    /* longjmp back to the app frame loop */
    doom_longjmp_out();
}

void I_WaitVBL(int count) {
    uint32_t start = timer_ticks;
    /* 70 Hz VBLs -> 60 Hz ticks */
    while ((timer_ticks - start) < (uint32_t)count * 60u / 70u) {
        __asm__ volatile ("hlt");
    }
}

void I_BeginRead(void) {}
void I_EndRead(void) {}

byte *I_AllocLow(int length) {
    size_t bytes;

    if (length < 0) {
        I_Error("I_AllocLow: low memory exhausted");
        return doom_low_pool;
    }
    bytes = ((size_t)length + 15u) & ~(size_t)15u;
    if (bytes > sizeof(doom_low_pool) - doom_low_used) {
        I_Error("I_AllocLow: low memory exhausted");
        return doom_low_pool;
    }
    byte *mem = doom_low_pool + doom_low_used;
    doom_low_used += bytes;
    doom_memset(mem, 0, (size_t)length);
    return mem;
}

/* I_Error: record the message, jump back to the app. The glue renders
 * it in the DOOM window instead of killing the OS. */
static char doom_error_text[256];
void doom_fatal(const char *message);

void I_Error(char *error, ...) {
    va_list ap;
    va_start(ap, error);
    doom_vsprintf(doom_error_text, error, ap);
    va_end(ap);

    serial_trace_concat("ERROR", "DOOM engine: ", doom_error_text);
    doom_fatal(doom_error_text);
}

const char *doom_get_error_text(void) {
    return doom_error_text;
}

/* Clear the recorded I_Error text: called at the start of every engine
 * boot so a stale message from a previous run cannot leak into the
 * boot-failure state (a fresh run with no I_Error must show NO_WAD or
 * RUNNING, not the old error). */
void doom_clear_error_text(void) {
    doom_error_text[0] = '\0';
}

/* ---- i_video.c ---- */

/* Output frame consumed by the HaloxOS app renderer. */
uint8_t doom_screen[320 * 200];

void I_InitGraphics(void) {
    serial_trace("DOOM", "I_InitGraphics: 320x200 indexed -> app window");
}

void I_ShutdownGraphics(void) {}

/* screens[0] must alias doom_screen so the engine's own patch/column
 * renderers land directly in the app-facing frame. V_Init's
 * I_AllocLow assignment is overridden right after it runs (see
 * doom_hosted.c's boot order: V_Init then doom_boot_screens fixup). */
void doom_boot_screens(void) {
    screens[0] = doom_screen;
}

void I_UpdateNoBlit(void) {}

void I_FinishUpdate(void) {
    /* Nothing to do: doom_glue reads screens[0] after the frame. The
     * engine's screens[0] IS doom_screen when V_Init wired it that way
     * (multiply==1 path, no X image buffer). */
}

void I_ReadScreen(byte *scr) {
    doom_memcpy(scr, screens[0], 320u * 200u);
}

/* Palette changes (PLAYPAL + gammatable) go to the app renderer. */
uint8_t doom_palette_rgb[768]; /* 256 * (r,g,b), post-gamma */
static bool doom_palette_changed = false;

void I_SetPalette(byte *palette) {
    for (int i = 0; i < 256; ++i) {
        doom_palette_rgb[i * 3 + 0] = gammatable[usegamma][*palette++];
        doom_palette_rgb[i * 3 + 1] = gammatable[usegamma][*palette++];
        doom_palette_rgb[i * 3 + 2] = gammatable[usegamma][*palette++];
    }
    doom_palette_changed = true;
}

/* App-side dirty query (see doom_app.c's render). */
int doom_palette_dirty(void) {
    return doom_palette_changed ? 1 : 0;
}

void doom_palette_clear_dirty(void) {
    doom_palette_changed = false;
}

void doom_system_reset_for_restart(void) {
    doom_low_used = 0;
    doom_time_base = -1;
    doom_time_frozen = 0;
    doom_clock_is_paused = false;
    doom_quit_requested = 0;
    doom_palette_changed = false;
    doom_memset(doom_screen, 0, sizeof(doom_screen));
    doom_memset(doom_palette_rgb, 0, sizeof(doom_palette_rgb));
}

void I_StartTic(void) {
    /* Input events arrive through doom_post_*; the hosted driver
     * (doom_hosted.c) queues them and they are fed to the engine's
     * responder chain here, exactly where X11 polling happened. */
    doom_pump_events();
}

void I_StartFrame(void) {}

/* ---- i_sound.c: silent stubs (HaloxOS has no audio yet) ---- */

void I_InitSound(void) {}
void I_ShutdownSound(void) {}
void I_UpdateSound(void) {}
void I_SubmitSound(void) {}
void I_SetChannels(void) {}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo) {
    char namebuf[9];
    doom_sprintf(namebuf, "ds%s", sfxinfo->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority) {
    (void)id; (void)vol; (void)sep; (void)pitch; (void)priority;
    return 1 + (id & 3); /* fake handle */
}

void I_StopSound(int handle) {
    (void)handle;
}

int I_SoundIsPlaying(int handle) {
    (void)handle;
    return 0;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch) {
    (void)handle; (void)vol; (void)sep; (void)pitch;
}

void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) { (void)volume; }
void I_PauseSong(int handle) { (void)handle; }
void I_ResumeSong(int handle) { (void)handle; }
int I_RegisterSong(void *data) { (void)data; return 1; }
void I_PlaySong(int handle, int looping) { (void)handle; (void)looping; }
void I_StopSong(int handle) { (void)handle; }
void I_UnRegisterSong(int handle) { (void)handle; }

/* ---- i_net.c: single-player loopback ---- */

static doomcom_t doom_com_storage;

void I_InitNetwork(void) {
    doomcom = &doom_com_storage;
    doom_memset(doomcom, 0, sizeof(*doomcom));

    /* no -dup / -extratic / -net parsing: hardcoded single player */
    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
    doomcom->ticdup = 1;
    doomcom->extratics = 0;

    serial_trace("DOOM", "I_InitNetwork: single player (loopback)");
}

void I_NetCmd(void) {
    /* never called: no remote nodes exist */
}

#endif /* DOOM_I_SYSTEM_INCLUDED */

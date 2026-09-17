// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: doom_hosted.c, DOOM engine port: hosted loop driver.
//
// This includes the DOOM 1997 source code, written by id Software Inc. Please check the license in the current folder.
// This repository is licensed under the GNU General Public License.
//
// D_DoomLoop in d_main.c never returns on DOS/Linux. Hosted in the OS,
// the engine is driven per-frame instead: doom_boot() runs the D_DoomMain
// startup sequence once, doom_frame() runs one TryRunTics + D_Display
// pass, and I_Error/I_Quit unwind back into the app.

#ifndef DOOM_HOSTED_INCLUDED
#define DOOM_HOSTED_INCLUDED

/* Forward declarations: defined in doom_libc.c / doom_wad_api.c, which
 * the amalgamation includes before this file (but whose prototypes it
 * keeps file-local). */
int doom_wad_read_at(uint32_t offset, void *dest, int count);
void doom_alloca_reset(void);
void doom_malloc_reset(void);
void doom_system_reset_for_restart(void);
void doom_wad_reset_cache(void);

/* Forward declarations from d_main.c used by the hosted frame pump. */
extern boolean advancedemo;
void D_DoAdvanceDemo(void);

/* Unwind flag: set by I_Quit / doom_exit_stub / doom_fatal. The frame
 * pump and boot check it after every engine call; a hosted engine must
 * never call longjmp across the kernel's C frames. */
int doom_hosted_jmp_out = 0;

static bool doom_in_frame = false;

void doom_longjmp_out(void) {
    doom_hosted_jmp_out = 1;
    doom_in_frame = false;
}

/* Called by I_Error: the formatted message is already in
 * doom_error_text (doom_i_system.c); just flag the unwind. The app
 * renders the error in the DOOM window. */
void doom_fatal(const char *message) {
    (void)message;
    doom_hosted_jmp_out = 1;
    doom_in_frame = false;
}

/* ---- input event bridge (HaloxOS -> engine D_PostEvent) ---- */

static event_t doom_pending_events[32];
static int doom_pending_head = 0;
static int doom_pending_tail = 0;

static void doom_push_event(event_t *ev) {
    int next = (doom_pending_head + 1) & 31;
    if (next == doom_pending_tail) {
        return; /* overflow: drop */
    }
    doom_pending_events[doom_pending_head] = *ev;
    doom_pending_head = next;
}

/* Feed queued input events into the engine (called from I_StartTic). */
void doom_pump_events(void) {
    while (doom_pending_tail != doom_pending_head) {
        event_t ev = doom_pending_events[doom_pending_tail];
        doom_pending_tail = (doom_pending_tail + 1) & 31;
        D_PostEvent(&ev);
    }
}

/* ---------------- frame pump ---------------- */

static bool doom_boot_active = false;
static bool doom_frame_rendered = false;
static int doom_last_display_gametic = -1;

/* The source engine was designed as a one-shot executable.  The hosted app
 * stays linked into the kernel, so restore the startup-owned state that the
 * next process would normally receive from the loader.  In particular,
 * wadfiles must be cleared before D_AddFile: leaving its old entries behind
 * made each reopen load duplicate/corrupted WAD state. */
static void doom_prepare_new_run(void) {
    doom_system_reset_for_restart();
    D_ResetHostedNetState();
    doom_malloc_reset();
    doom_alloca_reset();
    doom_wad_reset_cache();

    doom_memset(wadfiles, 0, sizeof(wadfiles));
    lumpinfo = NULL;
    lumpcache = NULL;
    numlumps = 0;
    reloadlump = 0;
    reloadname = NULL;
    mainzone = NULL;
    doom_memset(screens, 0, sizeof(screens));
    doom_memset(events, 0, sizeof(events));
    eventhead = 0;
    eventtail = 0;
    doom_pending_head = 0;
    doom_pending_tail = 0;
    doom_hosted_jmp_out = 0;
    doom_frame_rendered = false;
    doom_last_display_gametic = -1;
}

int doom_frame_did_render(void) {
    return doom_frame_rendered ? 1 : 0;
}

/* The raw WAD medium has no filename.  Infer the Doom family from the
 * map markers in an IWAD directory, then hand linuxdoom one of the
 * canonical virtual filenames accepted by doom_wad_api.c.  This keeps
 * ISO, ATA sidecar-disk, RAM-module and embedded-WAD boot paths identical
 * while supporting Doom shareware, registered/Ultimate Doom, and Doom II.
 * PWADs are overlays, not self-contained games, and are rejected here. */
static bool doom_choose_iwad(char *name, size_t name_size) {
    uint8_t header[12];
    uint8_t entries[16 * 64];
    bool has_e1m1 = false;
    bool has_e2m1 = false;
    bool has_e4m1 = false;
    bool has_map01 = false;
    uint32_t numlumps;
    uint32_t dir_offset;
    uint32_t done = 0;

    if (doom_wad_read_at(0, header, sizeof(header)) != (int)sizeof(header) ||
        header[0] != 'I' || header[1] != 'W' || header[2] != 'A' || header[3] != 'D') {
        I_Error("DOOM needs an IWAD\n(PWADs need a base IWAD)");
        return false;
    }

    numlumps = (uint32_t)header[4] | ((uint32_t)header[5] << 8) |
               ((uint32_t)header[6] << 16) | ((uint32_t)header[7] << 24);
    dir_offset = (uint32_t)header[8] | ((uint32_t)header[9] << 8) |
                 ((uint32_t)header[10] << 16) | ((uint32_t)header[11] << 24);
    if (numlumps == 0 || numlumps > 16384u ||
        dir_offset > (uint32_t)doom_wad_size_internal() ||
        numlumps > ((uint32_t)doom_wad_size_internal() - dir_offset) / 16u) {
        I_Error("DOOM IWAD directory is invalid");
        return false;
    }

    while (done < numlumps) {
        uint32_t count = numlumps - done;
        if (count > 64u) {
            count = 64u;
        }
        if (doom_wad_read_at(dir_offset + done * 16u, entries, (int)(count * 16u)) !=
            (int)(count * 16u)) {
            I_Error("DOOM IWAD directory is unreadable");
            return false;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t *lump = &entries[i * 16u + 8u];
            if (doom_memcmp(lump, "E1M1", 4) == 0) has_e1m1 = true;
            if (doom_memcmp(lump, "E2M1", 4) == 0) has_e2m1 = true;
            if (doom_memcmp(lump, "E4M1", 4) == 0) has_e4m1 = true;
            if (doom_memcmp(lump, "MAP01", 5) == 0) has_map01 = true;
        }
        done += count;
    }

    if (has_map01) {
        gamemode = commercial;
        doom_strncpy(name, "doom2.wad", name_size);
    } else if (has_e4m1) {
        gamemode = retail;
        doom_strncpy(name, "doomu.wad", name_size);
    } else if (has_e2m1) {
        gamemode = registered;
        doom_strncpy(name, "doom.wad", name_size);
    } else if (has_e1m1) {
        gamemode = shareware;
        doom_strncpy(name, "doom1.wad", name_size);
    } else {
        I_Error("DOOM IWAD has no supported maps");
        return false;
    }
    name[name_size - 1] = '\0';
    return true;
}

/* ---- COM1 debug detail: WAD header verify + game state trace -----
 * Reads the on-disk WAD header and directory directly (independent of
 * the engine's W_Init path) so 'make serial' shows the file identity,
 * lump count and the E#M#/MAP## level list before the engine boots. */
static void doom_trace_wad_details(void) {
    uint8_t header[12];

    if (doom_wad_read_at(0, header, 12) != 12) {
        I_Error("DOOM1.WAD header unreadable\non the boot disk");
        return;
    }

    /* wadinfo_t: char identification[4]; int numlumps; int infotableofs; */
    uint32_t numlumps = (uint32_t)header[4] | ((uint32_t)header[5] << 8) |
                        ((uint32_t)header[6] << 16) | ((uint32_t)header[7] << 24);
    uint32_t infotableofs = (uint32_t)header[8] | ((uint32_t)header[9] << 8) |
                            ((uint32_t)header[10] << 16) | ((uint32_t)header[11] << 24);

    serial_trace("INFO", "DOOM WAD verify: IWAD id and header ok");
    serial_trace_uint_value("INFO", "DOOM WAD lumps", numlumps);
    serial_trace_uint_value("INFO", "DOOM WAD directory offset", infotableofs);
    serial_trace_uint_value("INFO", "DOOM WAD total size", (uint32_t)doom_wad_size_internal());

    if (numlumps == 0 || numlumps > 16000u) {
        I_Error("DOOM1.WAD has a bad lump count\n(the file on disk is corrupt)");
        return;
    }

    /* Trace the level lumps from the raw directory (filelump_t is 16
     * bytes: int filepos; int size; char name[8] at offset 8). One
     * sequential chunked scan (no per-lump seeks); this is the "game
     * details" part: which levels ship in this WAD. */
    {
        uint8_t chunk[16 * 64]; /* 64 entries per read */
        char levels[64];
        char name[9];
        int level_count = 0;
        int printed = 0;
        uint32_t done = 0;
        uint32_t total = numlumps * 16u;

        levels[0] = '\0';
        while (done < total) {
            uint32_t want = total - done;
            if (want > sizeof(chunk)) {
                want = (uint32_t)sizeof(chunk);
            }
            if ((uint32_t)doom_wad_read_at(infotableofs + done, chunk, (int)want) != want) {
                /* The stamped info block parsed but the WAD bytes on
                 * disk do not match it (truncated image): the engine
                 * would drown in lump-not-found errors, so abort the
                 * boot here with one clear reason. */
                I_Error("DOOM1.WAD data read failed\n(media returned a short read)\n(rebuild: make disk / make run)");
                return;
            }
            for (uint32_t off = 0; off < want; off += 16) {
                const uint8_t *entry = &chunk[off];
                /* level lumps: "ExMy" (DOOM 1) / "MAPxx" (DOOM 2) */
                bool level_lump = ((entry[8] == 'E' && entry[10] == 'M') ||
                                  (entry[8] == 'M' && entry[9] == 'A' && entry[10] == 'P'));
                if (!level_lump) {
                    continue;
                }
                ++level_count;
                if (printed < 5) {
                    doom_memcpy(name, &entry[8], 8);
                    name[8] = '\0';
                    if (printed > 0) {
                        doom_strcat(levels, " ");
                    }
                    doom_strcat(levels, name);
                    ++printed;
                } else if (printed == 5) {
                    doom_strcat(levels, " ...");
                    ++printed;
                }
            }
            done += want;
        }
        serial_trace_uint_value("INFO", "DOOM WAD level lumps", (uint32_t)level_count);
        serial_trace_concat("INFO", "DOOM WAD maps: ", levels);
    }
}

/* Game-state trace: startup details + one line per level load. Called
 * from doom_boot (after the engine init) and from the frame pump when
 * the current level changes. */
static int doom_trace_last_episode = -1;
static int doom_trace_last_map = -1;
static int doom_trace_last_skill = -1;

static const char *doom_skill_name(int skill) {
    switch (skill) {
        case 0: return "I'm too young to die";
        case 1: return "Hey, not too rough";
        case 2: return "Hurt me plenty";
        case 3: return "Ultra-Violence";
        case 4: return "Nightmare!";
        default: return "unknown";
    }
}

static void doom_trace_game_details(void) {
    char buffer[96];

    doom_sprintf(buffer, "DOOM game: mode=%s episode=%d map=%d skill=%d (%s)",
                 gamemode == shareware ? "shareware (DOOM1.WAD)" :
                 gamemode == registered ? "registered" :
                 gamemode == commercial ? "commercial" : "unknown",
                 gameepisode, gamemap, gameskill, doom_skill_name(gameskill));
    serial_trace("DOOM", buffer);
}

/* Level-change watcher: wired into the frame pump so every G_InitNew /
 * G_DoLoadLevel (menu new game, next level, title demo loop) lands on
 * COM1. */
static void doom_trace_level_watcher(void) {
    if (gameepisode != doom_trace_last_episode ||
        gamemap != doom_trace_last_map ||
        gameskill != doom_trace_last_skill) {
        char buffer[64];
        doom_trace_last_episode = gameepisode;
        doom_trace_last_map = gamemap;
        doom_trace_last_skill = gameskill;
        doom_sprintf(buffer, "DOOM level load: E%dM%d skill=%d (%s)",
                     gameepisode, gamemap, gameskill, doom_skill_name(gameskill));
        serial_trace("DOOM", buffer);
    }
}

/* ---- WAD self-test: raw ATA probes at the cache boundary ------------
 * The engine boot dies when the first read PAST the 72-sector header
 * cache fails (empty lump directory -> every W_GetNumForName errors).
 * These probes read the exact boundary sectors raw through the ATA
 * driver, plus the first directory sector, and trace ATA status for
 * each so 'make serial' pinpoints which layer breaks. */
void doom_wad_selftest(void) {
    extern uint32_t doom_wad_module_addr;
    extern uint32_t doom_wad_module_bytes;

    /* Kernel-embedded WAD first: the Makefile bakes DOOM1.WAD into the
     * kernel image, so the game data is present on ANY boot media with
     * zero bus I/O. Verify the header + the first directory entry name
     * straight from RAM, no media probing at all. */
    {
        extern const uint8_t *doom_wad_embed_get(uint32_t *size);
        uint32_t size = 0;
        const uint8_t *image = doom_wad_embed_get(&size);
        if (image != NULL && size != 0) {
            char line[80];
            doom_sprintf(line, "DOOM WAD embedded: %d bytes in the kernel image", size);
            serial_trace("INFO", line);
            if (size >= 28u && image[0] == 'I' && image[1] == 'W' &&
                image[2] == 'A' && image[3] == 'D') {
                char name[9];
                uint32_t dir_off = (uint32_t)image[8] | ((uint32_t)image[9] << 8) |
                                   ((uint32_t)image[10] << 16) | ((uint32_t)image[11] << 24);
                doom_memcpy(name, &image[dir_off + 8], 8);
                name[8] = '\0';
                serial_trace_concat("INFO", "  header: IWAD ok, first lump: ", name);
            } else {
                serial_trace("WARNING", "  embedded image is not an IWAD - media fallback stays active");
            }
            return;
        }
    }

    /* Loader-relocated multiboot module next: GRUB loaded the WAD via
     * BIOS, the loader moved it above the kernel memory span. Also no
     * bus I/O; the engine reads it straight from RAM. */
    if (doom_wad_module_addr != 0) {
        char line[64];
        uint8_t chunk[512];
        int n;
        doom_wad_reset_cache();
        n = doom_wad_read_at(0u, chunk, (int)sizeof(chunk));
        doom_sprintf(line, "DOOM WAD module: RAM at 0x%x, %d bytes",
                     doom_wad_module_addr, doom_wad_module_bytes);
        serial_trace("INFO", line);
        if (n == (int)sizeof(chunk) && chunk[0] == 'I' && chunk[1] == 'W' &&
            chunk[2] == 'A' && chunk[3] == 'D') {
            serial_trace("INFO", "  WAD header: IWAD ok (engine reads straight from RAM)");
        } else {
            serial_trace("WARNING", "  WAD header unreadable - falling back to media probe");
        }
        doom_wad_reset_cache();
        return;
    }

    /* Test the exact medium selected by WAD discovery. The previous test
     * hard-coded primary-master LBA 131200, which is misleading when the
     * WAD disk is primary slave and also hid compact-vs-legacy layout. */
    {
        uint8_t sector[512];
        uint32_t start_lba = 0;
        uint32_t dir_off = 0;
        uint32_t total = 0;
        int ok_header = 0;

        doom_wad_reset_cache();
        ok_header = doom_wad_probe_internal() &&
                    doom_wad_read_at(0u, sector, (int)sizeof(sector)) >= 12;
        if (ok_header) {
            dir_off = (uint32_t)sector[8] | ((uint32_t)sector[9] << 8) |
                      ((uint32_t)sector[10] << 16) | ((uint32_t)sector[11] << 24);
        }
        start_lba = doom_wad_medium.start_sector;
        total = doom_wad_medium.total_bytes;

        serial_trace("INFO", "DOOM WAD self-test (selected medium):");
        serial_trace_concat("INFO", "  medium: ", doom_wad_medium_desc());
        serial_trace_uint_value("INFO", "  WAD start LBA", start_lba);
        serial_trace_uint_value("INFO", "  WAD bytes", total);

        if (doom_wad_medium.kind == DOOM_MEDIUM_ATA && total != 0) {
            extern bool ata_pio_read_sector_dev(uint16_t io_base, uint8_t devsel_head, uint32_t lba, uint8_t *buffer);
            uint32_t dir_sector = start_lba + (dir_off / 512u);
            uint32_t last_sector = start_lba + ((total - 1u) / 512u);
            int ok_start = ata_pio_read_sector_dev(doom_wad_medium.io_base,
                                                    doom_wad_medium.devsel,
                                                    start_lba, sector);
            int ok_dir = ok_header ? ata_pio_read_sector_dev(doom_wad_medium.io_base,
                                                               doom_wad_medium.devsel,
                                                               dir_sector, sector) : 0;
            int ok_last = ata_pio_read_sector_dev(doom_wad_medium.io_base,
                                                   doom_wad_medium.devsel,
                                                   last_sector, sector);
            serial_trace_concat("INFO", "  WAD header sector: ", ok_start ? "ok" : "FAIL");
            serial_trace_concat("INFO", "  lump directory sector: ", ok_dir ? "ok" : "FAIL");
            serial_trace_concat("INFO", "  final WAD sector: ", ok_last ? "ok" : "FAIL");
            if (start_lba == 2052u) {
                serial_trace("INFO", "  layout: compact raw WAD before FAT partition");
            } else if (start_lba == 131204u) {
                serial_trace("INFO", "  layout: legacy raw WAD past 64M partition");
            }
        }
    }

    /* Port read path: the exact call the engine's W_Init makes for the
     * lump directory (offset 4175796 = sec 8155, past the cache). Runs
     * the medium discovery too (RAM module, any ATA drive, or the
     * ATAPI CD blob). */
    {
        uint8_t chunk[1024];
        int n;
        doom_wad_reset_cache();
        n = doom_wad_read_at(4175796u, chunk, (int)sizeof(chunk));
        {
            char line[48];
            doom_sprintf(line, "  port read of directory: %d bytes", n);
            serial_trace("INFO", line);
        }
        serial_trace_concat("INFO", "  WAD medium: ", doom_wad_medium_desc());
        if (n == (int)sizeof(chunk)) {
            char name[9];
            doom_memcpy(name, &chunk[8], 8);
            name[8] = '\0';
            serial_trace_concat("INFO", "  directory entry 0 name: ", name);
        }
    }
}

/* One engine frame: TryRunTics + D_Display. Returns 0 to keep running,
 * 1 if the engine quit or errored (doom_get_error_text() holds it). */
int doom_frame(void) {
    int gametic_before;
    bool display_due;

    doom_hosted_jmp_out = 0;
    doom_frame_rendered = false;

    if (doom_run_state != DOOM_STATE_RUNNING) {
        return 1;
    }

    /* frame syncronous IO operations (feeds queued events) */
    I_StartFrame();

    /* process one or more tics (NetUpdate runs inside TryRunTics) */
    gametic_before = gametic;
    doom_in_frame = true;
    TryRunTics();
    doom_in_frame = false;
    display_due = gametic != gametic_before;

    /* The original non-hosted loop advances the attract sequence as part of
     * its engine loop. Our hosted frame pump replaces D_DoomLoop, so mirror
     * that responsibility here. This is what makes D_StartTitle() actually
     * display TITLEPIC first and then progress through the normal attract
     * sequence without ever bypassing the title screen at launch. */
    if (advancedemo) {
        D_DoAdvanceDemo();
        display_due = true;
    }

    if (doom_quit_requested || doom_hosted_jmp_out) {
        return 1;
    }

    /* The desktop runs at 60 Hz while the original game state runs at
     * TICRATE (35 Hz).  Re-rendering an identical 320x200 scene on every
     * desktop frame burned most of the time on weak emulators.  Draw only
     * when a tic or attract-state transition produced new pixels. */
    if (!display_due && doom_last_display_gametic == gametic) {
        doom_trace_level_watcher();
        return 0;
    }

    /* Update display with current state. */
    doom_in_frame = true;
    D_Display();
    doom_in_frame = false;
    doom_last_display_gametic = gametic;
    doom_frame_rendered = true;

    doom_trace_level_watcher();

    return doom_quit_requested || doom_hosted_jmp_out ? 1 : 0;
}

/* ---------------- boot ---------------- */

/* Adapted D_DoomMain startup for a hosted single-player run: the real
 * subsystem init sequence in the original order, then the title demo
 * loop (D_StartTitle). Command-line handling, response files and
 * savegame loading are dropped (no argv, no filesystem). */
static void doom_startup_sequence(void) {
    char wadname[16];

    /* myargv/myargc: one fixed fake argv, no options. */
    {
        static char argv0[] = "doom";
        static char *fake_argv[1] = { argv0 };
        myargv = fake_argv;
        myargc = 1;
    }

    /* The storage layer is filename-free.  Select the original engine's
     * game mode from the mounted IWAD before W_Init starts loading data. */
    if (!doom_choose_iwad(wadname, sizeof(wadname))) {
        return;
    }
    D_AddFile(wadname);

    modifiedgame = false;

    startskill = sk_medium;
    startepisode = 1;
    startmap = 1;
    autostart = false;

    doom_printf("V_Init: allocate screens.");
    V_Init();
    doom_boot_screens(); /* screen 0 aliases the app-facing doom_screen */

    doom_printf("M_LoadDefaults: Load system defaults.");
    M_LoadDefaults();

    /* linuxdoom keeps these defaults inside NORMALUNIX.  HaloxOS is
     * intentionally not NORMALUNIX (there is no host POSIX layer), but
     * without the port guard in m_misc.c every control remains BSS-zero.
     * That made correctly delivered key 0xad (Up) update gamekeydown[173]
     * while G_BuildTiccmd read gamekeydown[key_up == 0], so the player
     * never moved.  The table now supplies these values; retain this
     * defensive fallback so a future custom defaults table cannot silently
     * reintroduce an unbound game. */
    if (key_right == 0 || key_left == 0 || key_up == 0 || key_down == 0 ||
        key_strafeleft == 0 || key_straferight == 0 || key_fire == 0 ||
        key_use == 0 || key_strafe == 0 || key_speed == 0) {
        key_right = KEY_RIGHTARROW;
        key_left = KEY_LEFTARROW;
        key_up = KEY_UPARROW;
        key_down = KEY_DOWNARROW;
        key_strafeleft = ',';
        key_straferight = '.';
        key_fire = KEY_RCTRL;
        key_use = ' ';
        key_strafe = KEY_RALT;
        key_speed = KEY_RSHIFT;
        serial_trace("WARNING", "DOOM controls restored by HaloxOS fallback defaults");
    }
    {
        char bindings[128];
        doom_sprintf(bindings, "DOOM controls: up=%d down=%d left=%d right=%d strafe=%d/%d",
                     key_up, key_down, key_left, key_right,
                     key_strafeleft, key_straferight);
        serial_trace("INFO", bindings);
    }

    doom_printf("Z_Init: Init zone memory allocation daemon.");
    Z_Init();

    doom_printf("W_Init: Init WADfiles.");
    W_InitMultipleFiles(wadfiles);

    doom_printf("M_Init: Init miscellaneous info.");
    M_Init();

    doom_printf("R_Init: Init DOOM refresh daemon.");
    R_Init();

    doom_printf("P_Init: Init Playloop state.");
    P_Init();

    doom_printf("I_Init: Setting up machine state.");
    I_Init();

    doom_printf("D_CheckNetGame: Checking network game status.");
    D_CheckNetGame();

    doom_printf("S_Init: Setting up sound.");
    S_Init(snd_SfxVolume, snd_MusicVolume);

    doom_printf("HU_Init: Setting up heads up display.");
    HU_Init();

    doom_printf("ST_Init: Init status bar.");
    ST_Init();

    /* Match original DOOM startup behavior: begin on the title/attract
     * screen rather than jumping directly into E1M1. D_StartTitle()
     * initializes demosequence and requests the first title-screen frame.
     * Input remains fully live: any key during the attract sequence opens
     * the normal DOOM main menu, where New Game can start E1M1. */
    D_StartTitle();
    usergame = false;
    demoplayback = false;
    singledemo = false;
    serial_trace("DOOM", "Hosted launch: original title/attract screen");
}

int doom_boot(void) {
    if (doom_run_state == DOOM_STATE_RUNNING) {
        return doom_run_state;
    }

    doom_prepare_new_run();
    doom_clear_error_text();

    if (!doom_wad_probe_internal()) {
        serial_trace("ERROR", "DOOM: no valid IWAD found on boot media - cannot start");
        doom_run_state = DOOM_STATE_NO_WAD;
        return doom_run_state;
    }

    serial_trace("INFO", "DOOM: IWAD found - booting linuxdoom-1.10 engine");
    serial_trace_uint_value("INFO", "DOOM WAD bytes", (uint32_t)doom_wad_size_internal());
    doom_trace_wad_details();
    doom_alloca_reset();

    if (doom_quit_requested || doom_hosted_jmp_out) {
        /* I_Error in the verify pass (truncated/unreadable WAD data):
         * stop here with the clear reason instead of letting the engine
         * drown in lump-not-found errors. */
        serial_trace_concat("ERROR", "DOOM boot failed: ", doom_get_error_text());
        doom_run_state = DOOM_STATE_ERROR;
        doom_hosted_jmp_out = 0;
        return doom_run_state;
    }

    doom_quit_requested = 0;
    doom_hosted_jmp_out = 0;
    doom_pending_head = doom_pending_tail = 0;

    doom_boot_active = true;
    doom_startup_sequence();
    doom_boot_active = false;

    if (doom_quit_requested || doom_hosted_jmp_out) {
        /* I_Error during init (bad WAD etc): show error window. */
        serial_trace_concat("ERROR", "DOOM boot failed: ", doom_get_error_text());
        doom_run_state = DOOM_STATE_ERROR;
        doom_hosted_jmp_out = 0;
        return doom_run_state;
    }

    doom_run_state = DOOM_STATE_RUNNING;
    doom_trace_game_details();
    doom_trace_last_episode = doom_trace_last_map = doom_trace_last_skill = -1;
    serial_trace("INFO", "DOOM engine running - HaloxOS port");
    return doom_run_state;
}

/* Engine shut down (window closed). The zone is a static block so no
 * free() is needed; just drop the run state and WAD cache. */
void doom_shutdown(void) {
    if (doom_run_state == DOOM_STATE_RUNNING) {
        D_QuitNetGame();
    }
    doom_run_state = DOOM_STATE_OFF;
    doom_frame_rendered = false;
    doom_wad_reset_cache();
}

/* ---- public event entry points (called by the HaloxOS app) ---- */

void doom_post_key_event(int key_down, int doom_key) {
    event_t ev;
    ev.type = key_down ? ev_keydown : ev_keyup;
    ev.data1 = doom_key;
    ev.data2 = -1;
    ev.data3 = -1;
    doom_push_event(&ev);
}

void doom_post_mouse_motion(int dx, int dy) {
    event_t ev;
    ev.type = ev_mouse;
    ev.data1 = 0;
    ev.data2 = dx;
    ev.data3 = dy;
    doom_push_event(&ev);
}

void doom_post_mouse_button(int button, bool down) {
    event_t ev;
    ev.type = ev_mouse;
    ev.data1 = down ? (1 << button) : 0;
    ev.data2 = 0;
    ev.data3 = 0;
    doom_push_event(&ev);
}

/* DOOM key codes for the app's KeyCode/ch translation. */
int doom_xlate_ascii(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 'A';
    }
    return (int)(unsigned char)ch;
}

#endif /* DOOM_HOSTED_INCLUDED */

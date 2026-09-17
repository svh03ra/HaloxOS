// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: doom_engine.c, DOOM engine amalgamation for HaloxOS.
//
// This includes the DOOM 1997 source code, written by id Software Inc. Please check the license in the current folder.
// This repository is licensed under the GNU General Public License.
//
// DOOM (linuxdoom-1.10) Copyright (C) 1993-1996 by id Software, Inc.,
// distributed under the DOOM Source Code License. Engine source files
// below are included VERBATIM from apps/DOOM (the original 1997
// release); only the OS layer (i_*.c) is replaced by the port files in
// apps/DOOM/port, and d_main/d_net/m_menu/g_game carry small noted
// patches to run hosted (no exit(), no endless loop, no files).
//
// This file is #included from the kernel amalgamation (kernel.c) right
// after the UI widget layer: the engine and the OS then share one
// translation unit, so the port can call kernel statics (fill_rect,
// draw_pixel, windows, mouse, serial_trace, ata_pio_read_sector...).

/* Order matters: engine headers first, then OS layer, then engine
 * sources, then the hosted driver and the app glue. Includes are
 * relative to this file (the engine's own headers live one level up,
 * the port files beside it). */

#include "doom_headers.h"

/* ---- engine headers (verbatim) ----
 * doomtype.h first: it defines byte/boolean the rest depend on. */
#include "../doomtype.h"
#include "../doomdef.h"
#include "../dstrings.h"
#include "../d_items.h"
#include "../d_event.h"
#include "../d_player.h"
#include "../d_ticcmd.h"
#include "../d_net.h"
#include "../doomdata.h"
#include "../doomstat.h"
#include "../d_main.h"
#include "../f_finale.h"
#include "../f_wipe.h"
#include "../g_game.h"
#include "../hu_lib.h"
#include "../hu_stuff.h"
#include "../i_net.h"
#include "../i_sound.h"
#include "../i_system.h"
#include "../info.h"
#include "../m_argv.h"
#include "../m_bbox.h"
#include "../m_cheat.h"
#include "../m_fixed.h"
#include "../m_menu.h"
#include "../m_misc.h"
#include "../m_random.h"
#include "../m_swap.h"
#include "../p_inter.h"
#include "../p_local.h"
#include "../p_mobj.h"
#include "../p_pspr.h"
#include "../p_saveg.h"
#include "../p_setup.h"
#include "../p_spec.h"
#include "../p_tick.h"
#include "../r_bsp.h"
#include "../r_data.h"
#include "../r_defs.h"
#include "../r_draw.h"
#include "../r_local.h"
#include "../r_main.h"
#include "../r_plane.h"
#include "../r_segs.h"
#include "../r_sky.h"
#include "../r_state.h"
#include "../r_things.h"
#include "../sounds.h"
#include "../s_sound.h"
#include "../st_lib.h"
#include "../st_stuff.h"
#include "../tables.h"
#include "../v_video.h"
#include "../w_wad.h"
#include "../wi_stuff.h"
#include "../z_zone.h"
#include "../am_map.h"
#include "../i_video.h"
#include "../d_textur.h"

/* ---- port OS layer (replaces i_system/i_video/i_sound/i_net) ---- */
#include "doom_libc.c"
#include "doom_wad_data_hosted.c"
#include "doom_wad_api.c"
#include "doom_i_system.c"

/* ---- engine sources, verbatim ----
 * OS-specific files skipped: i_main.c (real main), i_net.c (sockets),
 * i_sound.c (DMX sndserver), i_video.c (X11), i_system.c (unix),
 * w_wad.c (POSIX fds -> adapted below), d_main.c / d_net.c /
 * m_misc.c (adapted below: no argv/files/exit).
 * m_argv.c, m_swap.c, m_bbox.c, m_fixed.c, m_random.c, m_cheat.c are
 * OS-clean and included verbatim. */
#include "../doomdef.c"
#include "../doomstat.c"
#include "../dstrings.c"
#include "../d_items.c"
#include "../m_argv.c"
#include "../m_bbox.c"
#include "../m_cheat.c"
#include "../m_fixed.c"
#include "../m_random.c"
#include "../m_swap.c"
#include "../tables.c"
#include "../info.c"
#include "../sounds.c"
#include "../z_zone.c"
#include "../w_wad.c"
#include "../f_finale.c"
#include "../f_wipe.c"
/* am_map.c: its automap-framebuffer static `fb` collides with the
 * kernel's framebuffer state `fb` in this shared TU - rename locally. */
#define fb am_fb
#include "../am_map.c"
#undef fb
#include "../hu_lib.c"
#include "../hu_stuff.c"
#include "../p_ceilng.c"
#include "../p_doors.c"
#include "../p_enemy.c"
#include "../p_floor.c"
#include "../p_inter.c"
#include "../p_lights.c"
#include "../p_map.c"
#include "../p_maputl.c"
#include "../p_mobj.c"
#include "../p_plats.c"
#include "../p_pspr.c"
#include "../p_saveg.c"
#include "../p_setup.c"
#include "../p_sight.c"
#include "../p_spec.c"
#include "../p_switch.c"
#include "../p_telept.c"
#include "../p_tick.c"
#include "../p_user.c"
#include "../r_bsp.c"
#include "../r_data.c"
#include "../r_draw.c"
#include "../r_main.c"
#include "../r_plane.c"
#include "../r_segs.c"
#include "../r_sky.c"
#include "../r_things.c"
#include "../st_lib.c"
#include "../st_stuff.c"
#include "../v_video.c"
/* wi_stuff.c: its intermission `anim_t`/`anims` differ from p_spec.c's
 * (the original note says "another anim_t used in wi_stuff, unrelated")
 * - rename locally so both can live in the one amalgamation TU. */
#define anim_t wi_anim_t
#define anims wi_anims
#include "../wi_stuff.c"
#undef anims
#undef anim_t
#include "../s_sound.c"
#include "../g_game.c"
#include "../m_menu.c"
#include "../m_misc.c"
#include "../d_net.c"
#include "../d_main.c"

/* ---- port: engine boot / frame pump / hosted loop ---------------- */

#include "doom_hosted.c"

/* ---- port: HaloxOS app glue (window renderer + input bridge) -----
 * Included AFTER the engine so doom_app.c's engine externs resolve. */
#include "doom_app.c"

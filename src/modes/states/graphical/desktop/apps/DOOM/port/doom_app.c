// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: doom_app.c, DOOM game app: window, renderer, input.
//
// This includes the DOOM 1997 source code, written by id Software Inc. Please check the license in the current folder.
// This repository is licensed under the GNU General Public License.
//
// HaloxOS side of the DOOM port (engine in apps/DOOM/port). Renders the
// engine's 320x200 palette frame into the app window with the DOOM
// palette (PLAYPAL + gamma), scales it to the client area, draws the
// missing-WAD error screen when DOOM1.WAD is not on the disk, and
// bridges keyboard/mouse events into the engine.

#include "doom_os.h"

#ifndef DOOM_APP_INCLUDED
#define DOOM_APP_INCLUDED

/* Engine externs (doom_engine.c amalgamation). */
extern uint8_t doom_palette_rgb[768];
extern int doom_run_state;
int doom_boot(void);
int doom_frame(void);
void doom_shutdown(void);
void doom_app_reset(void);
void doom_post_key_event(int key_down, int doom_key);
void doom_post_mouse_motion(int dx, int dy);
void doom_post_mouse_button(int button, bool down);
int doom_xlate_ascii(char ch);
const char *doom_get_error_text(void);
int doom_frame_did_render(void);
void doom_clock_pause(void);
void doom_clock_resume(void);

/* Input translation state (filled by doom_app_handle_key). */
static int doom_last_mouse_x = 0;
static int doom_last_mouse_y = 0;
static bool doom_prev_left = false;
static bool doom_capture_locked = true;
static void doom_release_all_keys(void);
static bool doom_boot_pending = true;
static bool doom_render_dirty = true;

/* Used by the desktop cursor and mouse router.  The lock is an explicit
 * capture state, separate from engine-running state: DOOM opens captured,
 * Shift+Tab/middle-mouse releases it, and clicking the game client captures
 * it again. Focus changes or engine errors still return the normal desktop
 * pointer automatically because this predicate requires active DOOM. */
static bool doom_pointer_locked(void) {
    return active_window == APP_DOOM && windows[APP_DOOM].open &&
           doom_run_state == DOOM_STATE_RUNNING && doom_capture_locked;
}

static bool doom_point_in_client(int x, int y) {
    Window *window;

    if (active_window != APP_DOOM || !windows[APP_DOOM].open) {
        return false;
    }
    window = &windows[APP_DOOM];
    return point_in_rect(x, y, window->x + 8, window->y + 24,
                         window->w - 16, window->h - 32);
}

static void doom_pointer_recenter(void) {
    Window *window;
    int lock_x;
    int lock_y;

    if (active_window != APP_DOOM || !windows[APP_DOOM].open) {
        return;
    }
    window = &windows[APP_DOOM];
    lock_x = window->x + window->w / 2;
    lock_y = window->y + 19 + (window->h - 20) / 2;
    mouse.x = lock_x;
    mouse.y = lock_y;
    doom_last_mouse_x = lock_x;
    doom_last_mouse_y = lock_y;
}

static void doom_set_pointer_lock(bool locked) {
    if (active_window != APP_DOOM || !windows[APP_DOOM].open ||
        doom_run_state != DOOM_STATE_RUNNING) {
        return;
    }

    doom_capture_locked = locked;
    if (locked) {
        /* A relock click is a capture gesture, not a game click.  Start the
         * next locked frame at the client center so it produces no giant
         * synthetic mouse delta. */
        doom_prev_left = mouse.left;
        doom_pointer_recenter();
    } else {
        /* Release any mouse button that DOOM saw before pointer unlock.
         * The physical button may still be held while the desktop owns the
         * cursor, so do not leave DOOM stuck in a synthetic mouse-down. */
        if (doom_prev_left) {
            doom_post_mouse_button(0, false);
        }
        doom_prev_left = false;
        doom_release_all_keys();
        doom_last_mouse_x = mouse.x;
        doom_last_mouse_y = mouse.y;
    }

    serial_trace("DOOM", locked ? "Mouse captured (Shift+Tab/middle to unlock)"
                                : "Mouse released (click DOOM client or middle to capture)");
}

static void doom_toggle_pointer_lock(void) {
    if (active_window != APP_DOOM || !windows[APP_DOOM].open ||
        doom_run_state != DOOM_STATE_RUNNING) {
        return;
    }
    doom_set_pointer_lock(!doom_capture_locked);
}

static bool doom_app_needs_redraw(void) {
    return doom_pointer_locked() && doom_render_dirty;
}

static void doom_app_mark_presented(void) {
    if (doom_pointer_locked()) {
        doom_render_dirty = false;
    }
}

/* ---- key translation: HaloxOS KeyEvent -> DOOM key codes ----
 * Uses the DOOM_KC_* snapshot (doom_headers.h): the engine's doomdef.h
 * redefines KEY_ENTER/KEY_TAB/etc. as DOOM codes, so the kernel enum
 * names are not visible here. */
static int doom_key_code(KeyEvent event) {
    switch (event.code) {
        case DOOM_KC_UP: return 0xad;      /* KEY_UPARROW */
        case DOOM_KC_DOWN: return 0xaf;    /* KEY_DOWNARROW */
        case DOOM_KC_LEFT: return 0xac;   /* KEY_LEFTARROW */
        case DOOM_KC_RIGHT: return 0xae;  /* KEY_RIGHTARROW */
        case DOOM_KC_ENTER: return 13;    /* KEY_ENTER (DOOM code) */
        case DOOM_KC_ESC: return 27;      /* KEY_ESCAPE */
        case DOOM_KC_TAB: return 9;       /* KEY_TAB (DOOM code) */
        case DOOM_KC_F1: return 0x80 + 0x3b;
        case DOOM_KC_F2: return 0x80 + 0x3c;
        case DOOM_KC_F3: return 0x80 + 0x3d;
        case DOOM_KC_F4: return 0x80 + 0x3e;
        case DOOM_KC_F5: return 0x80 + 0x3f;
        case DOOM_KC_F6: return 0x80 + 0x40;
        case DOOM_KC_F7: return 0x80 + 0x41;
        case DOOM_KC_F8: return 0x80 + 0x42;
        case DOOM_KC_F9: return 0x80 + 0x43;
        case DOOM_KC_F10: return 0x80 + 0x44;
        case DOOM_KC_F11: return 0x80 + 0x57;
        case DOOM_KC_F12: return 0x80 + 0x58;
        case DOOM_KC_PAUSE: return 0xff;
        case DOOM_KC_BACKSPACE: return 127;
        default: break;
    }
    if (event.ch >= 32 && event.ch <= 126) {
        return doom_xlate_ascii(event.ch);
    }
    return 0;
}

static void doom_app_handle_key(KeyEvent event) {
    if (active_window != APP_DOOM || !windows[APP_DOOM].open) {
        return;
    }
    if (doom_run_state != DOOM_STATE_RUNNING) {
        return;
    }
    /* Arrow keys are owned by the held-key sync below: the engine needs
     * them HELD across tics for movement, and the OS queue only delivers
     * press events (the old instant keydown+keyup here meant the engine
     * never saw a held key - no movement). */
    if (event.code == DOOM_KC_UP || event.code == DOOM_KC_DOWN ||
        event.code == DOOM_KC_LEFT || event.code == DOOM_KC_RIGHT ||
        event.ch == 'w' || event.ch == 'W' ||
        event.ch == 'a' || event.ch == 'A' ||
        event.ch == 's' || event.ch == 'S' ||
        event.ch == 'd' || event.ch == 'D') {
        return;
    }
    int key = doom_key_code(event);
    if (key == 0) {
        return;
    }
    /* Printable letters, digits, punctuation and function/navigation keys
     * are edge-driven by the classic DOOM responder.  Hold-state gameplay
     * bindings are synchronized separately below.  The keyboard driver
     * supplies shifted punctuation and Caps Lock-adjusted letters through
     * KeyEvent.ch, so the whole printable ASCII set (32..126) reaches
     * DOOM unchanged apart from its normal lowercase->uppercase mapping. */
    doom_post_key_event(1, key);
    doom_post_key_event(0, key);
}

/* ---- held-key sync (movement / fire / speed) ----
 * The OS only delivers press events, so held keys are synced from the
 * driver's physical key-state table: keydown on the transition down,
 * keyup on the transition up, once per frame. Includes modern WASD
 * movement plus the classic arrow bindings. */
typedef struct {
    int doom_key;
    uint8_t base;
    bool extended;
} DoomKeyBind;

static const DoomKeyBind doom_key_binds[] = {
    { 0xad, 0x48, true },    /* up arrow */
    { 0xaf, 0x50, true },    /* down arrow */
    { 0xac, 0x4B, true },    /* left arrow */
    { 0xae, 0x4D, true },    /* right arrow */
    { 0xad, 0x11, false },   /* W -> up */
    { 0x2C, 0x1E, false },   /* A -> DOOM ',' (strafe left) */
    { 0xaf, 0x1F, false },   /* S -> down */
    { 0x2E, 0x20, false },   /* D -> DOOM '.' (strafe right) */
    { 0x9d, 0x1D, false },   /* Left Ctrl -> fire (key_fire) */
    { 0x9d, 0x1D, true },    /* Right Ctrl -> fire */
    { 0x20, 0x39, false },   /* Space -> use/open (key_use ' ') */
    { 0xb8, 0x38, false },   /* Left Alt -> classic key_strafe */
    { 0xb8, 0x38, true },    /* Right Alt -> classic key_strafe */
    { 0xb6, 0x2A, false },   /* LShift -> speed (run) */
    { 0xb6, 0x36, false }    /* RShift -> speed (run) */
};
#define DOOM_KEY_BIND_COUNT (sizeof(doom_key_binds) / sizeof(doom_key_binds[0]))
/* Multiple physical bindings (W + Up, A + Left, Ctrl aliases, etc.)
 * share the same DOOM key. Track the aggregate physical state per DOOM
 * key so releasing one alias cannot incorrectly release the engine key
 * while another alias is still held. */
static bool doom_key_desired[256];
static bool doom_key_posted[256];

static void doom_sync_keys(void) {
    doom_memset(doom_key_desired, 0, sizeof(doom_key_desired));
    for (size_t i = 0; i < DOOM_KEY_BIND_COUNT; ++i) {
        int key = doom_key_binds[i].doom_key;
        if (key >= 0 && key < 256 &&
            keyboard_key_held(doom_key_binds[i].base, doom_key_binds[i].extended)) {
            doom_key_desired[key] = true;
        }
    }

    for (int key = 0; key < 256; ++key) {
        if (doom_key_desired[key] != doom_key_posted[key]) {
            char buf[48];
            doom_sprintf(buf, "DOOM key %d state -> %d", key,
                         doom_key_desired[key] ? 1 : 0);
            serial_trace("DOOM", buf);
            doom_post_key_event(doom_key_desired[key] ? 1 : 0, key);
            doom_key_posted[key] = doom_key_desired[key];
        }
    }
}

/* Focus lost / window closing: release every held key so the marine
 * stops running. */
static void doom_release_all_keys(void) {
    for (int key = 0; key < 256; ++key) {
        if (doom_key_posted[key]) {
            doom_post_key_event(0, key);
            doom_key_posted[key] = false;
        }
    }
}

/* ---- per-frame engine pump (called from state_update) ---- */

static void update_doom(void) {
    if (!windows[APP_DOOM].open || doom_run_state != DOOM_STATE_RUNNING) {
        return;
    }

    /* Background games are paused, rather than continuously rendering a
     * hidden 320x200 scene.  Freeze I_GetTime as well so reactivating the
     * window cannot make TryRunTics catch up a large elapsed interval. */
    if (active_window != APP_DOOM) {
        if (doom_prev_left) {
            doom_post_mouse_button(0, false);
        }
        doom_prev_left = false;
        doom_release_all_keys();
        doom_clock_pause();
        return;
    }
    doom_clock_resume();
    if (doom_pointer_locked()) {
        doom_sync_keys();

        /* Software pointer lock: consume relative horizontal movement, then
         * recenter the OS pointer in DOOM's client area.  This keeps turning
         * continuous at screen edges, hides the desktop cursor, and prevents
         * game clicks from operating desktop chrome. */
        {
            Window *window = &windows[APP_DOOM];
            int lock_x = window->x + window->w / 2;
            int lock_y = window->y + 19 + (window->h - 20) / 2;
            int dx = mouse.x - doom_last_mouse_x;

            if (dx != 0) {
                doom_post_mouse_motion(dx, 0);
            }
            if (mouse.left != doom_prev_left) {
                doom_post_mouse_button(0, mouse.left);
                doom_prev_left = mouse.left;
            }
            mouse.x = lock_x;
            mouse.y = lock_y;
            doom_last_mouse_x = lock_x;
            doom_last_mouse_y = lock_y;
        }
    } else {
        /* While the pointer is released, keep the engine ticking but do not
         * translate desktop mouse movement/buttons into game input.  Keep
         * doom_prev_left false so a physical button held during the unlock
         * gesture gets a fresh press only after a later intentional relock. */
        doom_release_all_keys();
        doom_prev_left = false;
        doom_last_mouse_x = mouse.x;
        doom_last_mouse_y = mouse.y;
    }

    if (doom_frame() != 0) {
        /* Engine quit (menu quit) or errored: stop the pump and show
         * the reason in the window. Do NOT re-arm the boot: an I_Error
         * is permanent for this run (the engine state cannot resume);
         * reopening the window or closing it re-probes the WAD. */
        if (doom_run_state == DOOM_STATE_RUNNING) {
            serial_trace_concat("INFO", "DOOM engine stopped: ", doom_get_error_text());
            doom_run_state = DOOM_STATE_ERROR;
        }
        doom_render_dirty = true;
    } else if (doom_frame_did_render()) {
        doom_render_dirty = true;
    }
}

/* Engine shut down from the window manager: arm the render-side
 * first-open boot so reopening re-probes the WAD. */
void doom_app_reset(void) {
    doom_boot_pending = true;
    doom_capture_locked = true;
    doom_render_dirty = true;
    doom_prev_left = false;
    doom_memset(doom_key_desired, 0, sizeof(doom_key_desired));
    doom_memset(doom_key_posted, 0, sizeof(doom_key_posted));
}

/* doom_boot failed / engine errored: keep showing this window with the
 * error text; drop any pending re-boot (doom_boot re-arms only through
 * doom_app_reset on window reopen). */
void doom_app_boot_failed(void) {
    doom_boot_pending = false;
}

/* ---- renderer ---- */

/* Nearest-color match of a DOOM palette RGB against the OS palette,
 * cached per DOOM palette index. The OS palette is built at boot, so
 * the cache is rebuilt whenever DOOM sets a new palette. */
static uint8_t doom_color_map[256];
static uint16_t doom_color_map_rgb565[256];
static bool doom_color_map_ready = false;

static void doom_rebuild_color_map(void) {
    for (int i = 0; i < 256; ++i) {
        /* reuse the kernel's nearest_color via palette write-in:
         * nearest_color is a kernel static; replicate its math here
         * against the live OS palette. */
        extern Color palette[256];
        int best_distance = 0x7FFFFFFF;
        uint8_t best_index = 0;
        int r = doom_palette_rgb[i * 3 + 0];
        int g = doom_palette_rgb[i * 3 + 1];
        int b = doom_palette_rgb[i * 3 + 2];

        for (int j = 0; j < 256; ++j) {
            int dr = (int)palette[j].r - r;
            int dg = (int)palette[j].g - g;
            int db = (int)palette[j].b - b;
            int distance = dr * dr + dg * dg + db * db;
            if (distance < best_distance) {
                best_distance = distance;
                best_index = (uint8_t)j;
            }
        }
        doom_color_map[i] = best_index;
        doom_color_map_rgb565[i] = pixel_rgb565_fast(best_index);
    }
    doom_color_map_ready = true;
}

/* Error screen: missing WAD, or an engine I_Error message. */
static void render_doom_error(const Window *window, const char *message) {
    int ox = window->x + 8;
    int oy = window->y + 24;
    int vw = window->w - 16;
    int vh = window->h - 32;
    bool engine_error = message && message[0];

    fill_rect(ox, oy, vw, vh, color_black);
    draw_text_center(ox + vw / 2, oy + 14, "DOOM", color_red, color_black, true);

    if (engine_error) {
        draw_text_center(ox + vw / 2, oy + 34, "Engine error:", color_white, color_black, true);
        draw_text_center(ox + vw / 2, oy + 54, message, color_yellow, color_black, true);
        draw_text_center(ox + vw / 2, oy + vh - 12, "Close and reopen DOOM to retry.", color_gray, color_black, true);
        return;
    }

    draw_text_center(ox + vw / 2, oy + 34, "A DOOM IWAD is missing!", color_white, color_black, true);

    draw_text_center(ox + vw / 2, oy + 56, "A WAD file (Where's All the Data)", color_gray_light, color_black, true);
    draw_text_center(ox + vw / 2, oy + 68, "is required to run DOOM.", color_gray_light, color_black, true);

    /* footer */
    draw_text_center(ox + vw / 2, oy + vh - 22, "Add an IWAD to the boot-media build", color_gray, color_black, true);
    draw_text_center(ox + vw / 2, oy + vh - 12, "and open DOOM again.", color_gray, color_black, true);
}

/* Main DOOM window renderer: engine frame or error screen. */
static void render_doom(const Window *window) {
    int ox = window->x + 8;
    int oy = window->y + 24;
    int vw = window->w - 16;
    int vh = window->h - 32;

    /* First open: probe the WAD and boot the engine once. open_window
     * (window manager) already boots on open; this catches the case
     * where the window was already open across a re-probe (defensive). */
    if (doom_boot_pending) {
        doom_boot_pending = false;
        doom_last_mouse_x = mouse.x;
        doom_last_mouse_y = mouse.y;
        doom_boot();
    }

    if (doom_run_state != DOOM_STATE_RUNNING) {
        if (doom_run_state == DOOM_STATE_ERROR) {
            /* engine died mid-run: show its I_Error text */
            render_doom_error(window, doom_get_error_text());
        } else {
            render_doom_error(window, "");
        }
        return;
    }

    /* Blit the 320x200 indexed frame, scaled to the client area with
     * nearest-neighbor sampling. DOOM's 320x200 is 320x240 on square
     * pixels (4:3 with 1.2 aspect); scale x1.6/1.2 keeps proportions
     * close inside the window while staying integer-ish. The simple
     * stretch below maps the full client area; it is what most DOOM
     * ports do in windowed mode. */
    {
        extern uint8_t doom_screen[320 * 200];
        extern int doom_palette_dirty(void);
        extern void doom_palette_clear_dirty(void);

        /* palette may have been changed by the engine (I_SetPalette) */
        if (!doom_color_map_ready || doom_palette_dirty()) {
            doom_rebuild_color_map();
            doom_palette_clear_dirty();
        }

        /* Only the shadow plane the output presents has to be filled (see
         * present_index_plane_live in vga.c). On the common 16/24/32bpp
         * desktops that is the RGB565 plane, which halves this blit - and
         * the blit is the hottest loop in the whole app. */
        bool want_index = present_need_index_plane();
        bool want_rgb = present_need_rgb_plane();

        for (int y = 0; y < vh; ++y) {
            int sy = y * 200 / vh;
            const uint8_t *src = &doom_screen[sy * 320];
            uint8_t *dst_index = &backbuffer[(size_t)(oy + y) * OS_WIDTH + ox];
            uint16_t *dst_rgb = &backbuffer_rgb565[(size_t)(oy + y) * OS_WIDTH + ox];

            /* The normal DOOM client is exactly 320 pixels wide.  Direct
             * rows avoid 76,800 clipped draw_pixel calls per output frame. */
            if (vw == 320) {
                if (want_rgb && !want_index) {
                    int x = 0;
                    /* Two pixels per 32-bit store. */
                    for (; x + 1 < 320; x += 2) {
                        uint32_t pair = (uint32_t)doom_color_map_rgb565[src[x]] |
                                        ((uint32_t)doom_color_map_rgb565[src[x + 1]] << 16);
                        *(uint32_t *)(void *)(dst_rgb + x) = pair;
                    }
                    if (x < 320) {
                        dst_rgb[x] = doom_color_map_rgb565[src[x]];
                    }
                } else if (want_index && !want_rgb) {
                    for (int x = 0; x < 320; ++x) {
                        dst_index[x] = doom_color_map[src[x]];
                    }
                } else {
                    for (int x = 0; x < 320; ++x) {
                        uint8_t color = doom_color_map[src[x]];
                        if (want_index) dst_index[x] = color;
                        if (want_rgb) dst_rgb[x] = doom_color_map_rgb565[src[x]];
                    }
                }
            } else {
                for (int x = 0; x < vw; ++x) {
                    uint8_t source = src[x * 320 / vw];
                    if (want_index) dst_index[x] = doom_color_map[source];
                    if (want_rgb) dst_rgb[x] = doom_color_map_rgb565[source];
                }
            }
        }
    }
}

#endif /* DOOM_APP_INCLUDED */

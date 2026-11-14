// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: logic.c, settings logic.

// This repository is licensed under the GNU General Public License.

/* Defined below: the Apply path refuses a resolution entry the desktop has no
 * canvas for, so the prototype comes first. */
static bool resolution_option_supported(const SettingsState *base, uint8_t index);

/*
 * The colour row's own name for the depth the machine really took.
 *
 * A mode set is a request, and the firmware programs the depth it HAS for that
 * size: a machine whose only mode of a size is indexed takes an indexed one
 * however the colour row was set, and the other way round for a machine whose
 * small modes are direct-colour only.  Adopting what came back keeps the row
 * naming the setting in force instead of the one that was refused; the desktop
 * draws in the depth the framebuffer reports in either case, so before this
 * the row was the only thing in the OS still claiming otherwise.
 */
static uint8_t palette_mode_for_output_depth(uint8_t bpp, uint8_t requested) {
    if (bpp == 4) {
        /* Planar VGA, the classic 16-colour output. */
        return 1;
    }
    if (bpp == 8) {
        /* Default and Low are both indexed on an 8-bit output; keep Low when
         * that is what was asked for. */
        return requested == 1 ? 1 : 0;
    }
    return 2;
}

static void apply_settings(void) {
    SettingsState next = settings_pending;
    bool video_changed;

    /* The arrows and the widescreen checkbox only ever select an entry this
     * backend can apply, but never hand the mode setter one it cannot use:
     * keep the applied resolution instead of switching to a size the desktop
     * has no canvas for. */
    if (!resolution_option_supported(&settings_applied, next.resolution_mode)) {
        next.resolution_mode = settings_applied.resolution_mode;
    }

    video_changed = settings_applied.palette_mode != next.palette_mode ||
                           settings_applied.resolution_mode != next.resolution_mode ||
                           settings_applied.widescreen != next.widescreen;

    if (video_changed) {
        /* A real Memory/GPU working set lives just below the shadow plane;
         * release it before the canvas asks for a different amount of RAM. */
        stress_stop();
        /* Ask for the new desktop size BEFORE the switch: the mode setter
         * re-derives the canvas from the mode it actually programs, and on a
         * backend that cannot set modes at all (the bootloader framebuffer)
         * this request is the entire change - the desktop is re-sized in
         * software inside the fixed framebuffer, centred at 1:1. */
        set_desktop_size((int)output_width_for_settings(&next),
                         (int)output_height_for_settings(&next));

        if (!set_output_mode(&next)) {
            serial_trace("ERROR", "video mode switch failed");
            serial_trace_video_mode("Screen change failed");
            next.palette_mode = settings_applied.palette_mode;
            next.resolution_mode = settings_applied.resolution_mode;
            next.widescreen = settings_applied.widescreen;
            /* The switch failed, so the desktop has to go back to the size
             * the applied settings describe as well. */
            set_desktop_size((int)output_width_for_settings(&next),
                             (int)output_height_for_settings(&next));
        } else if (video_backend == VIDEO_BACKEND_MULTIBOOT && vbe_mode_available()) {
            /* The firmware programmed the mode; the depth it reported is the
             * one the desktop is drawing in now (see
             * palette_mode_for_output_depth). */
            next.palette_mode = palette_mode_for_output_depth(fb.bpp, next.palette_mode);
        }
    }

    settings_applied = next;
    settings_pending = next;
    program_vga_palette();
    /* The desktop background and the wallpaper fit are painted from
     * settings_applied, and nothing about them changes the video mode, so no
     * mode switch below this point would ever ask for the repaint: request a
     * full one directly. Without it a wallpaper change only appeared the next
     * time something unrelated forced a desktop repaint. */
    ui_input_dirty = true;
    serial_trace_video_mode("Screen Changed");
}

static bool settings_dirty(void) {
    return settings_applied.palette_mode != settings_pending.palette_mode ||
           settings_applied.resolution_mode != settings_pending.resolution_mode ||
           settings_applied.widescreen != settings_pending.widescreen ||
           settings_applied.background_mode != settings_pending.background_mode ||
           settings_applied.wallpaper_fit != settings_pending.wallpaper_fit ||
           settings_applied.window_fade != settings_pending.window_fade ||
           settings_applied.window_trails != settings_pending.window_trails;
}

static const char *palette_name(uint8_t index) {
    static const char *names[] = {
        "Default",
        "Low",
        "True-Color!"
    };
    return names[index % 3];
}

/* Name of one entry of the list this machine offers, e.g. "1024x768". Built
 * from the entry itself so the label and the mode that gets applied can never
 * drift apart - and, on a machine whose list is the firmware's own, that entry
 * can be a size the built-in table has no name for. */
static const char *resolution_name_at(uint8_t index, bool widescreen) {
    static char text[24];
    VideoModeOption option;
    int count = resolution_entry_count(widescreen);
    size_t len = 0;

    text[0] = '\0';
    if (count <= 0 ||
        !resolution_entry_at(widescreen, (int)(index % (uint8_t)count), &option)) {
        return text;
    }
    append_uint(text, &len, sizeof(text), option.width);
    append_char(text, &len, sizeof(text), 'x');
    append_uint(text, &len, sizeof(text), option.height);
    return text;
}

static const char *resolution_name(uint8_t index) {
    return resolution_name_at(index, false);
}

static const char *resolution_name_wide(uint8_t index) {
    return resolution_name_at(index, true);
}

/*
 * Can this entry actually be shown right now?
 *
 * Two different things can make an entry impossible, and they are NOT the
 * same check:
 *
 *   1. A backend that can program a mode (BGA, VMware SVGA, VGA) can present
 *      ANY size its device accepts. The list itself now starts at 480p (see
 *      the mode table), so this mostly guards a size past the output cap or
 *      past what the device itself accepts: those are the dead entries.
 *
 *   2. On a fixed framebuffer there is no runtime mode set at all: the mode
 *      the bootloader set IS the display, the desktop fills it natively at
 *      1:1, and the only size that machine can show is the one it is already
 *      showing. So exactly one entry is usable there - the current output -
 *      and nothing else: a smaller desktop would leave a letterbox border
 *      that display cannot get rid of, and a bigger one cannot be shown.
 *
 * This is the single rule the arrows, the arrow colouring and the Apply path
 * all share, so a click can never land on an entry the setter must refuse.
 */
static bool resolution_option_supported(const SettingsState *base, uint8_t index) {
    SettingsState candidate = *base;
    const VideoModeOption *option;
    int count = resolution_entry_count(base->widescreen);

    if (count <= 0) {
        return false;
    }
    candidate.resolution_mode = (uint8_t)(index % (uint8_t)count);
    option = resolution_option_for_settings(&candidate);

    if (option->width == 0 || option->height == 0 ||
        option->width > MAX_OUTPUT_WIDTH || option->height > MAX_OUTPUT_HEIGHT) {
        return false;
    }

    if (video_backend == VIDEO_BACKEND_MULTIBOOT && vbe_mode_available()) {
        /*
         * This machine can really change its mode, so the question is whether
         * the video BIOS has a mode of this size in the depth the selected
         * palette needs - the same list Apply will program from.  No more
         * guessing: an entry the card does not have is greyed out, and every
         * entry the card does have works.
         */
        return vbe_size_supported(option->width, option->height, candidate.palette_mode);
    }

    if (video_backend == VIDEO_BACKEND_MULTIBOOT && !vbe_mode_available()) {
        /*
         * A bootloader framebuffer with no video BIOS list has no runtime mode
         * set: the output is fixed, and the only thing Apply can do is re-size
         * the desktop inside it.  Entries that fit are selectable; a larger
         * one is not, because it would be clamped back to the output.
         */
        return option->width <= fb.width && option->height <= fb.height;
    }

    if (!video_mode_switch_available) {
        return option->width == fb.width && option->height == fb.height;
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        return option->width <= vmware_svga.max_width && option->height <= vmware_svga.max_height;
    }

    if (video_backend == VIDEO_BACKEND_VGA && candidate.palette_mode == 1) {
        return option->width == 640 && option->height == 480;
    }

    return true;
}

static bool pending_resolution_supported(uint8_t index) {
    return resolution_option_supported(&settings_pending, index);
}

/* Is there an applicable entry in this direction (+1 / -1)? Drives the arrow
 * colours: an arrow that cannot do anything is drawn grey, and one that skips
 * over entries the backend cannot apply stays usable. */
static bool resolution_step_available(int direction) {
    int count = resolution_entry_count(settings_pending.widescreen);
    int start;

    if (count <= 0) {
        return false;
    }
    start = settings_pending.resolution_mode % count;

    for (int step = 1; step <= count; ++step) {
        int next = start + direction * step;
        if (next < 0 || next >= count) {
            break;
        }
        if (pending_resolution_supported((uint8_t)next)) {
            return true;
        }
    }
    return false;
}

/*
 * Move the selection by one applicable entry. Entries this backend cannot
 * apply are stepped over rather than stopping the arrow: the columns are
 * seven entries long but only the ones from the base canvas up can ever be
 * shown, and a selection that lands on a dead entry would leave Apply to
 * fail (or, before the mode rollback existed, to strand the screen).
 *
 * The ends of the list are ends, not a loop.  Wrapping made the arrow past
 * the smallest entry jump to the largest one - so on a machine whose row is
 * the firmware's list, where the size on screen is often the smallest mode
 * the card has, pressing back could only make the display bigger, and the
 * row read as "the resolutions will not change".  A list a user picks from
 * stops at both ends, the way a resolution list is expected to.
 */
static void resolution_step(int direction) {
    int count = resolution_entry_count(settings_pending.widescreen);
    int start;

    if (count <= 0) {
        return;
    }
    start = settings_pending.resolution_mode % count;

    for (int step = 1; step <= count; ++step) {
        int next = start + direction * step;
        if (next < 0 || next >= count) {
            break;
        }
        if (pending_resolution_supported((uint8_t)next)) {
            settings_pending.resolution_mode = (uint8_t)next;
            return;
        }
    }
}

/*
 * The widescreen checkbox flips the column, so the same index now names a
 * different size - and this machine may not be able to show that size (a
 * fixed framebuffer, or a depth the card has no mode for). Snap to the
 * closest entry in the new column that is usable, so the checkbox can never
 * leave the selection pointing at something Apply cannot switch to.
 */
static void resolution_snap_to_supported(void) {
    int count = resolution_entry_count(settings_pending.widescreen);
    int start;

    if (count <= 0) {
        return;
    }
    start = settings_pending.resolution_mode % count;

    if (pending_resolution_supported((uint8_t)start)) {
        return;
    }

    for (int step = 1; step <= count; ++step) {
        int up = (start + step) % count;
        int down = (start - step + count) % count;
        if (pending_resolution_supported((uint8_t)up)) {
            settings_pending.resolution_mode = (uint8_t)up;
            return;
        }
        if (pending_resolution_supported((uint8_t)down)) {
            settings_pending.resolution_mode = (uint8_t)down;
            return;
        }
    }
}

static bool live_palette_supported(uint8_t index) {
    uint8_t palette_mode = index % 3;

    if (video_backend == VIDEO_BACKEND_MULTIBOOT) {
        if (vbe_mode_available()) {
            /* This machine changes modes through the video BIOS, so the
             * firmware's own list decides: a colour mode it has no mode of
             * that depth for cannot be applied, and offering it would only
             * make Apply fail.  Indexed (Default/Low) needs an 8bpp mode,
             * True-Color! any direct-colour one. */
            return palette_mode == 2 ? vbe_depth_offered(VBE_DEPTH_16)
                                     : vbe_depth_offered(VBE_DEPTH_8);
        }
        /* Multiboot gives us a fixed framebuffer; only direct-color modes
         * (15/16/24/32 bpp) can support the True-Color! setting. */
        return palette_mode != 2 || fb.bpp >= 15;
    }

    if (video_backend == VIDEO_BACKEND_VGA) {
        if (palette_mode == 2) {
            return false;
        }
        if (palette_mode == 1) {
            return video_mode_switch_available;
        }
        return true;
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        if (palette_mode == 2) {
            return vmware_svga.host_bpp >= 16;
        }
        return ((vmware_svga.caps & SVGA_CAP_8BIT_EMULATION) != 0) || vmware_svga.host_bpp >= 16;
    }

    if (video_backend == VIDEO_BACKEND_VGA) {
        return index % 3 != 2;
    }

    return palette_mode != 2 || video_mode_switch_available;
}

/* Wallpaper fit, as the Desktop tab's own row spells it. */
static const char *wallpaper_fit_name(bool fit) {
    return fit ? "Fit" : "Stretch";
}

static const char *background_name(uint8_t index) {
    static const char *names[] = {
        "Default",
        "Fruish",
        "Solid Red",
        "Solid Orange",
        "Solid Yellow",
        "Solid Green",
        "Solid Blue",
        "Solid Pink",
        "Solid White",
        "Solid Black"
    };
    return names[index % 10];
}

static void resolution_string(char *buffer, size_t max_len, const SettingsState *state) {
    size_t len = 0;
    append_uint(buffer, &len, max_len, output_width_for_settings(state));
    append_char(buffer, &len, max_len, 'x');
    append_uint(buffer, &len, max_len, output_height_for_settings(state));
}

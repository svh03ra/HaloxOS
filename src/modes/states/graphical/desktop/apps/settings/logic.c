// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: logic.c, settings logic.

// This repository is licensed under the GNU General Public License.

static void apply_settings(void) {
    SettingsState next = settings_pending;
    bool video_changed = settings_applied.palette_mode != next.palette_mode ||
                         settings_applied.resolution_mode != next.resolution_mode ||
                         settings_applied.widescreen != next.widescreen;

    if (video_changed && !set_output_mode(&next)) {
        serial_trace("ERROR", "video mode switch failed");
        serial_trace_video_mode("Screen change failed");
        next.palette_mode = settings_applied.palette_mode;
        next.resolution_mode = settings_applied.resolution_mode;
        next.widescreen = settings_applied.widescreen;
    }

    settings_applied = next;
    settings_pending = next;
    program_vga_palette();
    serial_trace_video_mode("Screen Changed");
}

static bool settings_dirty(void) {
    return settings_applied.palette_mode != settings_pending.palette_mode ||
           settings_applied.resolution_mode != settings_pending.resolution_mode ||
           settings_applied.widescreen != settings_pending.widescreen ||
           settings_applied.background_mode != settings_pending.background_mode ||
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

<<<<<<< HEAD
static const char *resolution_name(const SettingsState *state) {
    static const char *four_three[] = {
        "960x720", "640x480 (Default)", "480x360", "320x240", "192x144"
    };
    static const char *wide[] = {
        "1280x720", "854x480", "640x360", "426x240", "256x144"
    };
    return (state->widescreen ? wide : four_three)[state->resolution_mode % 5];
=======
static const char *resolution_name(uint8_t index) {
    static const char *names[] = {"960x720", "640x480", "480x360", "320x240", "192x144"};
    return names[index % 5];
}

static const char *resolution_name_wide(uint8_t index) {
    static const char *names[] = {"1280x720", "854x480", "640x360", "426x240", "256x144"};
    return names[index % 5];
>>>>>>> 6f31922 (Legacy VGA Support)
}

static bool live_resolution_supported(uint8_t index) {
    SettingsState candidate = settings_applied;

    if (!video_mode_switch_available) {
        return false;
    }

    candidate.resolution_mode = index % 5;
    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        return output_width_for_settings(&candidate) <= vmware_svga.max_width &&
               output_height_for_settings(&candidate) <= vmware_svga.max_height;
    }

    if (video_backend == VIDEO_BACKEND_VGA && settings_pending.palette_mode == 1) {
        return output_width_for_settings(&candidate) == 640 &&
               output_height_for_settings(&candidate) == 480;
    }

    return true;
}

static bool live_palette_supported(uint8_t index) {
    uint8_t palette_mode = index % 3;

    if (video_backend == VIDEO_BACKEND_MULTIBOOT) {
        return true;
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

    if (video_backend == VIDEO_BACKEND_MULTIBOOT) {
        return palette_mode != 2 || fb.bpp >= 15;
    }

    return palette_mode != 2 || video_mode_switch_available;
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

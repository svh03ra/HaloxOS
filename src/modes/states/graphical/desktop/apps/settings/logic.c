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
        "True Color!"
    };
    return names[index % 3];
}

static const char *resolution_name(uint8_t index) {
    static const char *names[] = {"720p", "480p", "360p", "240p", "144p"};
    return names[index % 5];
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

    return true;
}

static bool live_palette_supported(uint8_t index) {
    uint8_t palette_mode = index % 3;

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        if (palette_mode == 2) {
            return vmware_svga.host_bpp >= 16;
        }
        return ((vmware_svga.caps & SVGA_CAP_8BIT_EMULATION) != 0) || vmware_svga.host_bpp >= 16;
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

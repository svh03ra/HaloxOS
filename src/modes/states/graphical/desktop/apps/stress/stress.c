// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: stress.c, Stress Test app logic.

// This repository is licensed under the GNU General Public License.

/*
 * Debug-build stress tool.
 *
 * The trigger runs HERE, in the desktop frame loop - not in the debugger
 * terminal.  Each frame takes one bounded work slice, so the UI keeps
 * painting while the machine is loaded, and each trigger interval adds a
 * line to the on-screen log.  If a debugger breakpoint opens the overlay,
 * the debugger's `continue` command closes it and this loop simply keeps
 * running; nothing here waits on the debugger.
 */

/*
 * Disk stress backend.  ATA/ATAPI is the polled path the kernel owns; a USB
 * boot stick has no ATA presence at all, so it is read through the firmware
 * INT 13h transport (bios_disk.c).  The struct is re-probed on every Start.
 */
static bool stress_disk_is_bios = false;
static BiosDisk stress_disk_bios;

static const char *stress_mode_name(uint8_t mode) {    switch (mode & 3u) {
        case 1: return "Memory";
        case 2: return "GPU";
        case 3: return "Disk";
        default: return "CPU";
    }
}

static int stress_radio_x(uint8_t mode) {
    switch (mode & 3u) {
        case 1: return STRESS_RADIO_X_MEMORY;
        case 2: return STRESS_RADIO_X_GPU;
        case 3: return STRESS_RADIO_X_DISK;
        default: return STRESS_RADIO_X_CPU;
    }
}

/*
 * Trigger interval, in desktop frames.
 *
 * The trigger runs in the frame loop, so the countdown is a frame count, not
 * a millisecond count: one trigger per whole interval, exactly, with no
 * double fire because 16.67 ms of tick did not divide into 1000 ms of
 * interval.  The ms box is converted once and rounded to the nearest frame.
 */
static uint32_t stress_interval_frames(void) {
    uint32_t frames = (stress_interval_ms * TIMER_HZ + 500u) / 1000u;

    if (frames == 0u) {
        frames = 1u;
    }
    if (frames > STRESS_MAX_INTERVAL_FRAMES) {
        frames = STRESS_MAX_INTERVAL_FRAMES;
    }
    return frames;
}

/* total * stress_power / STRESS_POWER_MAX, without a 64-bit division (the
 * freestanding kernel does not link libgcc). */
static uint32_t stress_power_scale(uint32_t total) {
    if (total == 0u || stress_power == 0u) {
        return 0u;
    }
    return (total / STRESS_POWER_MAX) * stress_power +
           ((total % STRESS_POWER_MAX) * stress_power) / STRESS_POWER_MAX;
}

static void stress_set_field_text(int which, uint32_t value) {
    char *text = which == 0 ? stress_intensity_text : stress_interval_text;
    int *len = which == 0 ? &stress_intensity_len : &stress_interval_len;
    size_t used = 0;

    text[0] = '\0';
    append_uint(text, &used, which == 0 ? sizeof(stress_intensity_text)
                                        : sizeof(stress_interval_text), value);
    *len = (int)used;
}

static uint32_t stress_parse_field(int which) {
    const char *text = which == 0 ? stress_intensity_text : stress_interval_text;
    int len = which == 0 ? stress_intensity_len : stress_interval_len;
    uint32_t value = 0;

    for (int i = 0; i < len; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            continue;
        }
        value = value * 10u + (uint32_t)(text[i] - '0');
        /* Stop before a 32-bit overflow; anything this large is clamped
         * below anyway. */
        if (value > STRESS_MAX_INTERVAL_MS) {
            return STRESS_MAX_INTERVAL_MS;
        }
    }
    return value;
}

static void stress_apply_values(void) {
    /* Do not let Enter/Tab during a run replace the fixed setting that the
     * active run is already using. */
    if (stress_test_running) {
        return;
    }

    uint32_t intensity = stress_parse_field(0);
    uint32_t interval = stress_parse_field(1);

    if (intensity < 1u) {
        intensity = 1u;
    }
    if (intensity > STRESS_MAX_INTENSITY) {
        intensity = STRESS_MAX_INTENSITY;
    }
    if (interval < 1u) {
        interval = 1u;
    }
    if (interval > STRESS_MAX_INTERVAL_MS) {
        interval = STRESS_MAX_INTERVAL_MS;
    }

    stress_intensity = intensity;
    stress_interval_ms = interval;
    /* Write the clamped value back, so the box can never show a number the
     * engine is not actually using. */
    stress_set_field_text(0, intensity);
    stress_set_field_text(1, interval);
}

static void stress_focus_field(int which) {
    stress_field_focus = which;
    stress_field_fresh = true;
    if (which == 0) {
        stress_intensity_len = (int)strlen_local(stress_intensity_text);
    } else {
        stress_interval_len = (int)strlen_local(stress_interval_text);
    }
    ui_input_dirty = true;
}

static void stress_log_append(const char *line) {
    if (stress_log_count < STRESS_EVENT_LINES) {
        copy_string(stress_log[stress_log_count], line, STRESS_LOG_LEN);
        ++stress_log_count;
        return;
    }

    /* The view shows the newest lines at the bottom: once it is full the
     * oldest line is cleared on every new one. */
    for (int i = 0; i < STRESS_EVENT_LINES - 1; ++i) {
        copy_string(stress_log[i], stress_log[i + 1], STRESS_LOG_LEN);
    }
    copy_string(stress_log[STRESS_EVENT_LINES - 1], line, STRESS_LOG_LEN);
}

/*
 * Power ramp.
 *
 * The Intensity box picks the starting step; every trigger then adds that
 * step PLUS an accelerating share of the load already in place, so a run
 * looks like +2 > +3 > +4 > +6 > +9 ... and never stops growing until the
 * 2000% cap. No Intensity is an instant case: 65536 used to return the
 * whole cap, which took the load to 2000% on the first trigger and let
 * Memory mode commit its entire reserved pool in one frame.
 */
static uint32_t stress_base_step(void) {
    uint32_t value = stress_intensity;

    if (value <= 64u) {
        return 1u + value / 32u;
    }
    if (value <= 1024u) {
        return 3u + ((value - 64u) * 7u) / 960u;
    }
    if (value <= 8192u) {
        return 10u + ((value - 1024u) * 30u) / 7168u;
    }
    if (value <= 32768u) {
        return 40u + ((value - 8192u) * 160u) / 24576u;
    }
    /* Top of the range keeps the same 200..400 band as the ramp below it
     * instead of jumping to 2000%: the pool touch cap in
     * stress_pool_touch() then decides how fast the working set grows. */
    return 200u + ((value - 32768u) * 200u) / (STRESS_MAX_INTENSITY - 32768u);
}

/* Step applied by the next trigger, clamped to what is left of the ramp. */
static uint32_t stress_step_percent(void) {
    uint32_t base = stress_base_step();
    uint32_t step = base + stress_power / 8u;
    uint32_t remaining = STRESS_POWER_MAX - stress_power;

    if (step < base || step > remaining) {
        step = remaining;
    }
    return step;
}

static void stress_grow_load(uint32_t percent) {
    uint32_t next = stress_power + percent;

    if (next < stress_power || next > STRESS_POWER_MAX) {
        next = STRESS_POWER_MAX;
    }
    stress_power = next;
}

/*
 * One trigger event.
 *
 * CPU/Memory/GPU keep loading the machine between triggers (see
 * update_stress_test below); Disk accumulates its real read bytes between
 * triggers too, so the event line reports actual I/O rather than a guess.
 */
static void stress_do_trigger(void) {
    char line[STRESS_LOG_LEN];
    size_t len = 0;
    uint32_t percent = stress_step_percent();

    /* Ramp first, then report: the line has to show the power the trigger
     * actually produced, not the value it started from. */
    stress_grow_load(percent);

    line[0] = '\0';
    append_char(line, &len, sizeof(line), '#');
    append_uint(line, &len, sizeof(line), stress_trigger_count);
    append_char(line, &len, sizeof(line), ' ');

    if (stress_test_mode == 3u) {
        append_string(line, &len, sizeof(line), "Disk I/O ");
        append_uint(line, &len, sizeof(line), stress_disk_interval_bytes / 1024u);
        append_string(line, &len, sizeof(line), " KB, intensity ");
        append_uint(line, &len, sizeof(line), stress_intensity);
        stress_disk_interval_bytes = 0;
    } else {
        append_string(line, &len, sizeof(line), stress_mode_name(stress_test_mode));
        append_string(line, &len, sizeof(line), " trigger, intensity ");
        append_uint(line, &len, sizeof(line), stress_intensity);
    }
    append_string(line, &len, sizeof(line), " (+");
    append_uint(line, &len, sizeof(line), percent);
    append_string(line, &len, sizeof(line), "%) power ");
    append_uint(line, &len, sizeof(line), stress_power);
    append_char(line, &len, sizeof(line), '%');

    stress_log_append(line);
}

/*
 * Real working-set reservation.
 *
 * Memory mode actually commits RAM, so the pool is assembled from every
 * usable E820 span the desktop is not using: below the shadow plane and
 * above the DOOM zone, plus the free RAM above the WAD module.  A single
 * span was the old behaviour - and on a normal WAD boot the largest single
 * gap is often only a couple of megabytes even on a 16 MB machine, which is
 * exactly why the RAM gauge looked like it was faking the load.
 */
static void stress_pool_reset(void) {
    for (int i = 0; i < STRESS_POOL_SPANS; ++i) {
        stress_regions[i].base = 0u;
        stress_regions[i].bytes = 0u;
        stress_regions[i].touched = 0u;
    }
    stress_region_count = 0;
    stress_pool_bytes = 0u;
    stress_pool_touched = 0u;
    stress_memory_offset = 0u;
}

static void stress_pool_add_span(uint32_t base, uint32_t end) {
    uint32_t aligned_base;
    uint32_t aligned_end;

    if (stress_region_count >= STRESS_POOL_SPANS) {
        return;
    }
    aligned_base = (base + 15u) & ~15u;
    aligned_end = end & ~15u;
    /* Below 64 KiB there is no working set worth reserving. */
    if (aligned_end <= aligned_base + 65536u) {
        return;
    }
    stress_regions[stress_region_count].base = aligned_base;
    stress_regions[stress_region_count].bytes = aligned_end - aligned_base;
    stress_regions[stress_region_count].touched = 0u;
    ++stress_region_count;
}

/* Every usable E820 span inside [floor, ceiling].  Without a memory map, use
 * the contiguous range the shadow-plane allocator trusts. */
static void stress_pool_scan(uint32_t floor, uint32_t ceiling) {
    if (ceiling <= floor) {
        return;
    }

    if (crash_mmap_addr != 0 && crash_mmap_length != 0) {
        uintptr_t cursor = crash_mmap_addr;
        uintptr_t end = crash_mmap_addr + crash_mmap_length;

        while (cursor + sizeof(uint32_t) <= end) {
            const MultibootMmapEntry *entry = (const MultibootMmapEntry *)cursor;

            if (cursor + entry->size + sizeof(uint32_t) > end) {
                break;
            }
            cursor += entry->size + sizeof(uint32_t);
            if (entry->type != 1u) {
                continue;
            }
            {
                uint64_t entry_start = entry->base_addr;
                uint64_t entry_end = entry_start + entry->length;
                uint32_t span_start;
                uint32_t span_end;

                if (entry_start >= (uint64_t)ceiling ||
                    entry_end <= (uint64_t)floor) {
                    continue;
                }
                span_start = entry_start < (uint64_t)floor
                                 ? floor : (uint32_t)entry_start;
                span_end = entry_end > (uint64_t)ceiling
                               ? ceiling : (uint32_t)entry_end;
                stress_pool_add_span(span_start, span_end);
            }
        }
        return;
    }

    stress_pool_add_span(floor, ceiling);
}

static void stress_release_pool(void) {
    stress_pool_reset();
}

static void stress_pool_use_fallback(void) {
    stress_pool_reset();
    stress_regions[0].base = (uint32_t)(uintptr_t)stress_pool_fallback;
    stress_regions[0].bytes = (uint32_t)sizeof(stress_pool_fallback);
    stress_region_count = 1;
    stress_pool_bytes = stress_regions[0].bytes;
}

static void stress_reserve_pool(void) {
    const uint32_t guard = 0x10000u;
    uint32_t floor;
    uint32_t ceiling;
    uint32_t zone_limit;
    uint32_t target;
    uint32_t remaining;

    stress_pool_reset();

    floor = shadow_plane_floor();
    zone_limit = doom_zone_limit();
    if (zone_limit > floor) {
        floor = zone_limit;
    }
    ceiling = shadow_plane_ceiling();
    if (shadow_plane_base != 0 && shadow_plane_base < ceiling) {
        ceiling = shadow_plane_base;
    }
    stress_pool_scan(floor, ceiling);

    /* The DOOM zone is low RAM reserved for the engine that has not been
     * allocated while DOOM is closed - and open_window() stops this load
     * before DOOM boots.  Using it is what lets a 16 MB machine show a
     * nearly full working set instead of the half the gaps alone allow. */
    if (!windows[APP_DOOM].open) {
        uint32_t zone_base = shadow_plane_floor();
        uint32_t zone_end = doom_zone_limit();

        if (zone_end > zone_base) {
            if (shadow_plane_base != 0 && shadow_plane_base < zone_end) {
                zone_end = shadow_plane_base;
            }
            stress_pool_scan(zone_base, zone_end);
        }
    }

    if (doom_wad_module_addr != 0 && doom_wad_module_bytes != 0 &&
        doom_wad_module_addr + doom_wad_module_bytes > doom_wad_module_addr) {
        uint32_t wad_end = doom_wad_module_addr + doom_wad_module_bytes;

        stress_pool_scan(wad_end, ram_total_bytes);
    }

    /* Biggest gaps first, so the target fills with the large spans before
     * the small ones are used. */
    for (int i = 0; i < stress_region_count; ++i) {
        for (int j = i + 1; j < stress_region_count; ++j) {
            if (stress_regions[j].bytes > stress_regions[i].bytes) {
                StressRegion swap = stress_regions[i];

                stress_regions[i] = stress_regions[j];
                stress_regions[j] = swap;
            }
        }
    }

    if (stress_region_count == 0) {
        stress_pool_use_fallback();
        return;
    }

    /* Every free span, so the machine really is loaded; the cap only keeps
     * a huge host from reserving for minutes. */
    target = STRESS_POOL_TARGET_BYTES;

    remaining = target;
    for (int i = 0; i < stress_region_count && remaining > 0u; ++i) {
        uint32_t take = stress_regions[i].bytes;

        take = take > guard ? take - guard : 0u;
        take &= ~15u;
        if (take > remaining) {
            take = remaining;
        }
        stress_regions[i].bytes = take;
        stress_pool_bytes += take;
        remaining -= take;
    }
    if (stress_pool_bytes < 4096u) {
        stress_pool_use_fallback();
        return;
    }
    serial_trace_uint_value("INFO", "stress pool bytes", stress_pool_bytes);
    serial_trace_uint_value("INFO", "stress pool spans", (uint32_t)stress_region_count);
}

/* Touch newly exposed bytes for real: on this kernel there is no paging, so
 * the write is what makes the RAM actually used. New bytes are capped per
 * frame: a high Intensity used to ask for the whole pool in one call, so
 * the RAM gauge jumped 8.68 MB -> 200 MB in a single frame instead of
 * ramping. */
static void stress_pool_touch(uint32_t target) {
    uint32_t remaining;
    uint32_t limit;

    if (stress_pool_bytes == 0u) {
        return;
    }
    if (target > stress_pool_bytes) {
        target = stress_pool_bytes;
    }
    limit = stress_pool_touched + STRESS_POOL_TOUCH_BYTES_PER_FRAME;
    if (target > limit) {
        target = limit;
    }
    if (target <= stress_pool_touched) {
        return;
    }

    remaining = target - stress_pool_touched;
    for (int i = 0; i < stress_region_count && remaining > 0u; ++i) {
        StressRegion *region = &stress_regions[i];
        uint32_t free_bytes = region->bytes - region->touched;
        uint32_t take = free_bytes < remaining ? free_bytes : remaining;

        if (take == 0u) {
            continue;
        }
        memset_local((uint8_t *)(uintptr_t)(region->base + region->touched),
                     (int)(uint8_t)(0xA5u ^ (uint32_t)i),
                     (size_t)take);
        region->touched += take;
        stress_pool_touched += take;
        remaining -= take;
    }
}

/* Map an offset inside the touched working set to a real region. */
static uint8_t *stress_pool_map(uint32_t offset, uint32_t *available) {
    for (int i = 0; i < stress_region_count; ++i) {
        StressRegion *region = &stress_regions[i];

        if (offset < region->touched) {
            *available = region->touched - offset;
            return (uint8_t *)(uintptr_t)(region->base + offset);
        }
        offset -= region->touched;
    }
    *available = 0u;
    return NULL;
}

/* CPU slice.
 *
 * Wall-clock duty cycle, not a raw loop count: at full load it burns for
 * 15 ms of a 16.7 ms frame.  The TSC deadline keeps the slice correctly
 * sized on a fast host; the timer-tick escape hatch guarantees a wrong TSC
 * frequency can never lock the desktop for longer than a few frames.
 */
static void stress_cpu_work(void) {
    uint32_t acc = stress_cpu_sink;
    /* Power runs 0..STRESS_POWER_MAX (2000 = full), so the slice is scaled
     * by that cap - not by 100.  Dividing by 100 turned the 15 ms full-load
     * slice into 300 ms at max power, which locked the desktop for most of
     * every frame. */
    uint32_t slice_ms = (STRESS_CPU_SLICE_MS * stress_power) /
                        STRESS_POWER_MAX;

    if (stress_power == 0u || slice_ms == 0u) {
        return;
    }
    if (slice_ms > STRESS_CPU_SLICE_MAX_MS) {
        slice_ms = STRESS_CPU_SLICE_MAX_MS;
    }

    if (cpu_has_tsc && cpu_speed_mhz != 0) {
        uint64_t cycles = (uint64_t)slice_ms * (uint64_t)cpu_speed_mhz * 1000u;
        uint64_t start = rdtsc_read();
        uint32_t start_tick = timer_ticks;
        /* The escape hatch has to scale with the slice: a fixed 3-tick bound
         * used to cut every high-power burn back to ~50 ms. */
        uint32_t allowed_ticks = (slice_ms * TIMER_HZ) / 1000u + 6u;
        uint32_t spins = 0;

        while ((rdtsc_read() - start) < cycles) {
            acc = acc * 1664525u + 1013904223u;
            acc ^= acc >> 13;
            /* Safety net for a wrong TSC frequency: still yield the frame. */
            if ((++spins & 0x3FFFFu) == 0u &&
                (uint32_t)(timer_ticks - start_tick) >= allowed_ticks) {
                break;
            }
        }
    } else {
        uint32_t iterations = slice_ms * 20000u;

        for (uint32_t i = 0; i < iterations; ++i) {
            acc = acc * 1664525u + 1013904223u;
            acc ^= acc >> 13;
        }
    }
    stress_cpu_sink = acc;
}

/* Memory mode commits the whole working set as the ramp rises, then keeps
 * real traffic flowing through it.  The budget is 4 MB of writes at 100%
 * and 80 MB at 2000%, spread over every reserved region. */
static void stress_memory_work(void) {
    uint32_t traffic;
    uint32_t written = 0;
    uint32_t offset;

    if (stress_pool_bytes == 0u) {
        return;
    }
    stress_pool_touch(stress_power_scale(stress_pool_bytes));
    if (stress_pool_touched == 0u) {
        return;
    }

    traffic = (STRESS_MEM_BYTES_AT_100 * stress_power) / 100u;
    if (traffic == 0u) {
        return;
    }

    offset = stress_memory_offset;
    if (offset >= stress_pool_touched) {
        offset = 0u;
    }
    while (written < traffic) {
        uint32_t available = 0u;
        uint8_t *pool = stress_pool_map(offset, &available);
        uint32_t take = traffic - written;

        if (pool == NULL || available == 0u) {
            offset = 0u;
            continue;
        }
        if (take > available) {
            take = available;
        }
        for (uint32_t i = 0; i < take; ++i) {
            pool[i] = (uint8_t)(offset + i * 7u);
        }
        written += take;
        offset += take;
        if (offset >= stress_pool_touched) {
            offset = 0u;
        }
    }
    stress_memory_offset = offset;
}

/* GPU mode uses the same real pool as its VRAM working set, then the
 * renderer spends a pixel budget filling the window.  Writing the pool is
 * genuine memory traffic, and the touched bytes are what Task Manager shows
 * as GPU memory in use.  1 MB of word writes at 100%, 20 MB at 2000%. */
static void stress_gpu_work(void) {
    uint32_t words;
    uint32_t done = 0;
    uint32_t offset;

    if (stress_pool_bytes == 0u) {
        return;
    }
    stress_pool_touch(stress_power_scale(stress_pool_bytes));
    if (stress_pool_touched < 4u) {
        return;
    }

    words = (STRESS_GPU_WORDS_AT_100 * stress_power) / 100u;
    if (words == 0u) {
        return;
    }

    offset = stress_memory_offset;
    if (offset + 4u > stress_pool_touched) {
        offset = 0u;
    }
    offset &= ~3u;
    while (done < words) {
        uint32_t available = 0u;
        uint8_t *pool = stress_pool_map(offset, &available);
        uint32_t take = available / 4u;
        volatile uint32_t *dst;

        if (pool == NULL || take == 0u) {
            if (available == 0u) {
                offset = 0u;
            } else {
                offset += available;
            }
            continue;
        }
        if (take > words - done) {
            take = words - done;
        }
        dst = (volatile uint32_t *)pool;
        for (uint32_t i = 0; i < take; ++i) {
            dst[i] = 0x5A5A5A5Au ^ ((offset / 4u + i) * 2654435761u);
        }
        done += take;
        offset += take * 4u;
        if (offset + 4u > stress_pool_touched) {
            offset = 0u;
        }
    }
    stress_memory_offset = offset;
}

static void stress_gpu_set_budget(void) {
    const Window *window = &windows[APP_STRESS_TEST];
    uint32_t width;
    uint32_t height;
    uint32_t pixels;
    uint32_t budget;

    if (window->w <= 18 || window->h <= 34) {
        stress_gpu_budget = 0u;
        stress_gpu_budget_target = 0u;
        return;
    }
    width = (uint32_t)(window->w - 18);
    height = (uint32_t)(window->h - 34);
    pixels = width * height;
    /* One whole window per 100%, so 2000% is twenty screens of real
     * framebuffer traffic in the frame. */
    budget = (pixels * stress_power) / 100u;
    if (budget > STRESS_GPU_MAX_PIXELS) {
        budget = STRESS_GPU_MAX_PIXELS;
    }
    stress_gpu_budget = budget;
    stress_gpu_budget_target = budget;
}

/*
 * Disk stress backend selection.
 *
 * ATA PATA/ATAPI is preferred only when it is demonstrably the boot medium:
 * the firmware INT 13h path is the only way to reach a USB stick, and a USB
 * boot on a machine with an internal ATA disk must not stress that internal
 * disk by accident.  The MBRs are compared for that reason: if the ATA disk
 * carries the same sector 0 as the firmware boot drive, it is the same
 * medium and the faster polled path wins; otherwise the firmware drive is
 * the boot stick and is used instead.  A real CD boot (drive 0x9F) keeps the
 * ATAPI path.
 */
static bool stress_disk_select_ata(uint16_t io_base, uint8_t devsel,
                                   bool atapi, uint32_t last_lba) {
    stress_disk_io_base = io_base;
    stress_disk_devsel = devsel;
    stress_disk_is_atapi = atapi;
    stress_disk_is_bios = false;
    stress_disk_last_lba = last_lba;
    stress_disk_lba = 0u;
    stress_disk_ready = true;
    return true;
}

static bool stress_disk_prepare(void) {
    static const uint16_t bases[4] = {
        0x1F0u, 0x1F0u, 0x170u, 0x170u
    };
    static const uint8_t sels[4] = {
        0xE0u, 0xF0u, 0xE0u, 0xF0u
    };
    uint8_t *ata_mbr = stress_disk_buffer;
    uint8_t *bios_mbr = stress_disk_buffer + 512u;
    bool ata_found = false;
    uint16_t ata_base = 0u;
    uint8_t ata_sel = 0u;
    uint32_t ata_sectors = 0u;
    bool bios_ok = false;
    bool boot_is_cd;
    bool boot_is_hdd;
    bool usb_like;

    ata_detect_devices();

    boot_is_cd = boot_drive_valid && boot_drive_number >= 0x9Fu;
    boot_is_hdd = boot_drive_valid && boot_drive_number >= 0x80u &&
                  boot_drive_number <= 0x8Fu;
    usb_like = boot_medium_is_usb;

    /* Firmware path first: on a USB boot the ATA bus either has nothing or
     * an unrelated internal disk.  The probe is read-only. */
    if (boot_is_hdd || usb_like) {
        uint8_t drive = boot_drive_valid ? boot_drive_number : 0x80u;

        if (bios_disk_runtime_allowed() &&
            bios_disk_probe(drive, &stress_disk_bios) &&
            bios_disk_read(&stress_disk_bios, 0u, bios_mbr, 1u)) {
            bios_ok = true;
        } else {
            serial_trace_hex_value("WARNING",
                                   "stress disk: firmware INT13 unavailable for drive",
                                   drive);
        }
    }

    /* First ATA PATA device, with its real capacity when IDENTIFY answers. */
    for (int i = 0; i < 4; ++i) {
        if (ata_kind(bases[i], sels[i]) == ATA_DEV_PATA &&
            ata_pio_read_sector_dev(bases[i], sels[i], 0u, ata_mbr)) {
            ata_found = true;
            ata_base = bases[i];
            ata_sel = sels[i];
            if (!ata_pata_capacity_dev(bases[i], sels[i], &ata_sectors) ||
                ata_sectors == 0u) {
                ata_sectors = STRESS_DISK_PATA_LAST_LBA;
            }
            break;
        }
    }

    if (bios_ok) {
        bool same = false;

        if (ata_found) {
            same = true;
            for (int i = 0; i < 512; ++i) {
                if (ata_mbr[i] != bios_mbr[i]) {
                    same = false;
                    break;
                }
            }
        }
        if (ata_found && same && !usb_like) {
            stress_disk_select_ata(ata_base, ata_sel, false, ata_sectors);
            stress_log_append("Disk stress: ATA boot disk.");
            serial_trace("INFO", "stress disk: using ATA boot disk");
            return true;
        }

        stress_disk_io_base = 0u;
        stress_disk_devsel = 0u;
        stress_disk_is_atapi = false;
        stress_disk_is_bios = true;
        stress_disk_last_lba = bios_disk_capacity(&stress_disk_bios);
        stress_disk_lba = 0u;
        stress_disk_ready = true;
        stress_log_append("Disk stress: firmware INT13 boot disk.");
        serial_trace_hex_value("INFO", "stress disk: using firmware INT13 drive",
                               stress_disk_bios.drive);
        serial_trace_uint_value("INFO", "stress disk: firmware sectors",
                                stress_disk_last_lba);
        return true;
    }

    if (ata_found) {
        stress_disk_select_ata(ata_base, ata_sel, false, ata_sectors);
        stress_log_append("Disk stress: ATA disk.");
        serial_trace("INFO", "stress disk: using ATA disk");
        return true;
    }

    /* No ATA disk.  A USB/SCSI boot with no firmware path must not fall
     * through to an unrelated ATAPI CD-ROM: that would stress the wrong
     * device.  A real CD boot (drive 0x9F) still uses ATAPI. */
    if (!boot_is_cd && (usb_like || boot_is_hdd)) {
        stress_log_append("Disk stress: USB boot has no INT13 disk.");
        serial_trace("WARNING",
                     "stress disk: USB boot but no firmware disk path");
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        if (ata_kind(bases[i], sels[i]) == ATA_DEV_ATAPI) {
            uint32_t last_lba = 0u;
            uint32_t block_bytes = 0u;

            if (ata_packet_capacity_dev(bases[i], sels[i], &last_lba,
                                        &block_bytes) &&
                block_bytes == 2048u &&
                last_lba > STRESS_DISK_ATAPI_BLOCKS) {
                if (last_lba < 0xFFFFFFFFu) {
                    ++last_lba; /* capacity is exclusive in the read loop */
                }
                stress_disk_select_ata(bases[i], sels[i], true, last_lba);
                stress_log_append("Disk stress: ATAPI CD-ROM.");
                serial_trace("INFO", "stress disk: using ATAPI CD-ROM");
                return true;
            }
        }
    }
    return false;
}

static bool stress_disk_read_chunk(void) {
    uint32_t bytes;

    if (!stress_disk_ready) {
        return false;
    }

    if (stress_disk_is_bios) {
        uint32_t sectors = STRESS_DISK_CHUNK_BYTES / 512u;

        if (stress_disk_last_lba == 0u) {
            return false;
        }
        if (stress_disk_lba >= stress_disk_last_lba) {
            stress_disk_lba = 0u;
        }
        if (sectors > stress_disk_last_lba - stress_disk_lba) {
            sectors = stress_disk_last_lba - stress_disk_lba;
        }
        if (sectors == 0u ||
            !bios_disk_read(&stress_disk_bios, stress_disk_lba,
                            stress_disk_buffer, sectors)) {
            return false;
        }
        bytes = sectors * 512u;
        stress_disk_lba += sectors;
        stress_disk_bytes_read += bytes;
        stress_disk_interval_bytes += bytes;
        return true;
    }

    if (stress_disk_is_atapi) {
        uint32_t blocks = STRESS_DISK_ATAPI_BLOCKS;

        if (stress_disk_last_lba == 0u) {
            return false;
        }
        if (stress_disk_lba >= stress_disk_last_lba) {
            stress_disk_lba = 0u;
        }
        if (blocks > stress_disk_last_lba - stress_disk_lba) {
            blocks = stress_disk_last_lba - stress_disk_lba;
        }
        if (blocks == 0u ||
            !ata_packet_read_dev(stress_disk_io_base, stress_disk_devsel,
                                 stress_disk_lba, (uint16_t)blocks,
                                 stress_disk_buffer)) {
            return false;
        }
        bytes = blocks * 2048u;
        stress_disk_lba += blocks;
        stress_disk_bytes_read += bytes;
        stress_disk_interval_bytes += bytes;
        return true;
    }

    {
        uint32_t sectors = STRESS_DISK_PATA_SECTORS;

        if (stress_disk_last_lba == 0u) {
            return false;
        }
        if (stress_disk_lba >= stress_disk_last_lba) {
            stress_disk_lba = 0u;
        }
        if (sectors > stress_disk_last_lba - stress_disk_lba) {
            sectors = stress_disk_last_lba - stress_disk_lba;
        }
        if (sectors == 0u ||
            !ata_pio_read_sectors_dev(stress_disk_io_base, stress_disk_devsel,
                                      stress_disk_lba, stress_disk_buffer,
                                      (uint8_t)sectors)) {
            return false;
        }
        bytes = sectors * 512u;
        stress_disk_lba += sectors;
        stress_disk_bytes_read += bytes;
        stress_disk_interval_bytes += bytes;
        return true;
    }
}

static void stress_disk_work(void) {
    uint32_t units;

    if (!stress_disk_probe_done) {
        stress_disk_probe_done = true;
        if (!stress_disk_prepare()) {
            stress_log_append("Disk stress: no readable disk device.");
            return;
        }
    }
    if (!stress_disk_ready) {
        return;
    }

    /* 8 chunks (256 KB) of real reads at 100%, 64 chunks (2 MB) at 2000%.
     * The firmware USB path is capped much lower because each INT 13h call
     * masks interrupts for the whole transaction. */
    if (stress_disk_is_bios) {
        units = (2u * stress_power) / 100u;
        if (units > STRESS_DISK_BIOS_MAX_UNITS) {
            units = STRESS_DISK_BIOS_MAX_UNITS;
        }
    } else {
        units = (8u * stress_power) / 100u;
        if (units > STRESS_DISK_MAX_UNITS) {
            units = STRESS_DISK_MAX_UNITS;
        }
    }
    if (units == 0u) {
        units = 1u;
    }
    for (uint32_t i = 0; i < units; ++i) {
        if (!stress_disk_read_chunk()) {
            stress_disk_ready = false;
            stress_log_append("Disk read failed; stress stopped.");
            break;
        }
    }
}

static void update_stress_test(void) {
    if (!stress_test_running) {
        return;
    }

    /* The interval is counted in whole frames, so a trigger lands on exactly
     * one frame per interval and never fires twice because a leftover
     * millisecond did not divide into the 60 Hz tick.  The first trigger is
     * fired by stress_start() on the start frame only. */
    ++stress_frames_since_trigger;
    if (stress_frames_since_trigger >= stress_interval_frames()) {
        stress_frames_since_trigger = 0u;
        stress_last_trigger_tick = timer_ticks;
        ++stress_trigger_count;
        if (stress_trigger_count == 0u) {
            stress_trigger_count = 1u;
        }
        stress_do_trigger();
        ui_input_dirty = true;
    }

    /* Continuous load while the trigger is running.  Keeping it in small
     * slices is what leaves the desktop (and the Stop button) responsive. */
    switch (stress_test_mode) {
        case 0:
            stress_cpu_work();
            break;
        case 1:
            stress_memory_work();
            break;
        case 2:
            stress_gpu_work();
            stress_gpu_set_budget();
            break;
        default:
            stress_disk_work();
            break;
    }
}

static void stress_start(void) {
    if (stress_test_running) {
        return;
    }
    stress_apply_values();
    stress_test_running = true;
    stress_power = 0u;
    stress_frames_since_trigger = 0u;
    stress_last_trigger_tick = timer_ticks;
    stress_trigger_count = 1u;
    stress_gpu_budget = 0u;
    stress_gpu_budget_target = 0u;
    stress_memory_offset = 0u;
    stress_disk_interval_bytes = 0u;
    stress_disk_probe_done = false;
    stress_disk_ready = false;
    stress_disk_is_bios = false;
    stress_disk_lba = 0u;
    if (stress_test_mode == 1u || stress_test_mode == 2u) {
        stress_reserve_pool();
    } else {
        stress_release_pool();
    }
    stress_log_append("Trigger started. Stop with Ctrl+Esc.");
    /* Fire the first trigger immediately (only on the start frame), so the
     * ramp begins at once instead of waiting one full interval. */
    stress_do_trigger();
    serial_trace("INFO", "stress test trigger started");
    ui_input_dirty = true;
}

static void stress_stop(void) {
    bool was_running = stress_test_running;

    stress_test_running = false;
    stress_power = 0u;
    stress_release_pool();
    stress_gpu_budget = 0u;
    stress_gpu_budget_target = 0u;
    stress_disk_ready = false;
    stress_disk_probe_done = false;
    stress_disk_is_bios = false;
    if (!was_running) {
        return;
    }
    /* The Intensity box is the fixed setting and does not grow by itself;
     * restore it in case it was edited during the run. */
    stress_set_field_text(0, stress_intensity);
    stress_log_append("Trigger stopped.");
    serial_trace("INFO", "stress test trigger stopped");
    ui_input_dirty = true;
}

static void stress_app_reset(void) {
    stress_test_running = false;
    stress_test_mode = 0;
    stress_field_focus = -1;
    stress_field_fresh = false;
    stress_log_count = 0;
    stress_trigger_count = 0;
    stress_memory_offset = 0u;
    stress_gpu_budget = 0u;
    stress_gpu_budget_target = 0u;
    stress_disk_lba = 0u;
    stress_disk_bytes_read = 0u;
    stress_disk_interval_bytes = 0u;
    stress_disk_sample_bytes = 0u;
    stress_disk_sample_tick = timer_ticks;
    stress_disk_measured_percent = 0u;
    stress_disk_probe_done = false;
    stress_disk_ready = false;
    stress_disk_is_bios = false;
    stress_release_pool();
    stress_apply_values();
    stress_power = 0u;
    stress_frames_since_trigger = 0u;
    stress_last_trigger_tick = timer_ticks;
    ui_input_dirty = true;
}


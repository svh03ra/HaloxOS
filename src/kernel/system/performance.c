// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: performance.c, used with task manager info.

// This repository is licensed under the GNU General Public License.

static const char *cpu_state_label(void) {
    if (cpu_halted_overlay) {
        return "Halted";
    }
    if (timer_ticks - last_input_tick > TIMER_HZ * 2u && open_window_count() <= 1) {
        return "Wait";
    }
    return "Normal";
}

static uint8_t cpu_state_color(void) {
    return streq(cpu_state_label(), "Normal") ? color_green_dark : color_red;
}

static void draw_usage_graph(int x, int y, const uint8_t *history, uint8_t color) {
    fill_rect(x, y, 64, 32, color_black);
    draw_rect(x, y, 64, 32, color_gray);
    for (int i = 0; i < 64; ++i) {
        int value = history[(perf_history_index + i) % 64];
        int bar_h = (value * 28) / 100;
        fill_rect(x + i, y + 31 - bar_h, 1, bar_h, color);
    }
}

static void format_snake_score(char *buffer, size_t max_len) {
    size_t len = 0;
    append_padded_uint(buffer, &len, max_len, snake_score, 8);
}

static void format_gpu_memory(char *buffer, size_t max_len) {
    size_t len = 0;
    append_memory_amount(buffer, &len, max_len, gpu_memory_used_bytes);
    append_char(buffer, &len, max_len, ' ');
    append_char(buffer, &len, max_len, '/');
    append_char(buffer, &len, max_len, ' ');
    append_memory_amount(buffer, &len, max_len, gpu_memory_total_bytes);
}

static void format_ram_total(char *buffer, size_t max_len) {
    format_single_memory_amount(buffer, max_len, ram_total_bytes);
}

static void format_ram_used(char *buffer, size_t max_len) {
    format_single_memory_amount(buffer, max_len, ram_used_bytes);
}

static void format_cpu_speed(char *buffer, size_t max_len) {
    size_t len = 0;
    append_frequency_label(buffer, &len, max_len, cpu_speed_mhz);
}

static void format_disk_io(char *buffer, size_t max_len) {
    size_t len = 0;
    append_uint(buffer, &len, max_len, disk_io_megabytes);
    append_char(buffer, &len, max_len, ' ');
    append_char(buffer, &len, max_len, 'M');
    append_char(buffer, &len, max_len, 'B');
}

static void init_cpu_monitoring(void) {
    uint32_t max_basic_leaf = 0;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    cpu_has_cpuid = cpuid_is_available();
    cpu_has_tsc = false;
    cpu_speed_mhz = 0;

    if (!cpu_has_cpuid) {
        return;
    }

    cpuid_query(0, &max_basic_leaf, &ebx, &ecx, &edx);
    if (max_basic_leaf >= 1) {
        cpuid_query(1, &eax, &ebx, &ecx, &edx);
        cpu_has_tsc = (edx & (1u << 4)) != 0;
    }
    if (max_basic_leaf >= 0x16) {
        cpuid_query(0x16, &eax, &ebx, &ecx, &edx);
        if ((eax & 0xFFFFu) != 0) {
            cpu_speed_mhz = eax & 0xFFFFu;
        }
    }
}

static void calibrate_cpu_speed(void) {
    uint32_t start_tick;
    uint64_t start_cycles;
    uint64_t end_cycles;
    uint32_t delta_cycles;

    if (!cpu_has_tsc || cpu_speed_mhz != 0) {
        return;
    }

    start_tick = timer_ticks;
    while (timer_ticks == start_tick) {
        cpu_halt_once();
    }
    start_tick = timer_ticks;
    start_cycles = rdtsc_read();
    while (timer_ticks < start_tick + 15u) {
        cpu_halt_once();
    }
    end_cycles = rdtsc_read();
    delta_cycles = (uint32_t)(end_cycles - start_cycles);
    cpu_speed_mhz = (uint32_t)(delta_cycles / 250000u);
    if (cpu_speed_mhz == 0) {
        cpu_speed_mhz = 1;
    }
}

static void update_performance_metrics(uint64_t frame_cycles) {
    (void)frame_cycles;
    int windows_open = open_window_count();
    uint32_t framebuffer_bytes = fb.pitch * fb.height;
    uint8_t gpu_estimate;
    uint32_t total_cycles = 0;
    uint32_t static_ram = (uint32_t)(sizeof(backbuffer) +
                                     sizeof(palette) +
                                     sizeof(idt) +
                                     sizeof(key_queue) +
                                     sizeof(mouse_packet) +
                                     sizeof(windows) +
                                     sizeof(notepad_text) +
                                     sizeof(paint_canvas) +
                                     sizeof(boot_term) +
                                     sizeof(cmd_term) +
                                     sizeof(snake_x) +
                                     sizeof(snake_y) +
                                     sizeof(mines_value) +
                                     sizeof(mines_revealed) +
                                     sizeof(mines_flagged));

    if (cpu_has_tsc && perf_window_ready) {
        total_cycles = (uint32_t)(rdtsc_read() - perf_window_start_cycles);
    }

    if (cpu_has_tsc && perf_window_ready && total_cycles != 0) {
        uint32_t usage = (perf_busy_cycle_accum * 100u) / total_cycles;
        cpu_usage_percent = (uint8_t)(usage > 100u ? 100u : usage);
    } else {
        cpu_usage_percent = (uint8_t)clampi(8 + windows_open * 7 + (menu_open ? 8 : 0) + (active_window >= 0 ? 10 : 0), 0, 100);
    }

    gpu_estimate = (uint8_t)clampi((int)cpu_usage_percent / 2 +
                                   windows_open * 6 +
                                   (menu_open ? 8 : 0) +
                                   (context_menu_open ? 4 : 0) +
                                   ((active_window == APP_PAINT || active_window == APP_GAME_CENTER) ? 10 : 0), 0, 100);
    gpu_usage_percent = gpu_estimate;
    ram_total_bytes = ram_total_bytes == 0 ? (32u * 1024u * 1024u) : ram_total_bytes;
    ram_used_bytes = static_ram + framebuffer_bytes + 768u * 1024u + (uint32_t)windows_open * 128u * 1024u;
    if (active_window == APP_TASK_MANAGER) {
        ram_used_bytes += 128u * 1024u;
    }
    if (ram_used_bytes > ram_total_bytes) {
        ram_used_bytes = ram_total_bytes;
    }
    ram_usage_percent = (uint8_t)((ram_used_bytes * 100u) / ram_total_bytes);
    disk_usage_percent = (uint8_t)clampi(windows_open * 3 + (menu_open ? 2 : 0), 0, 100);
    disk_io_megabytes = windows_open * 4;
    gpu_memory_used_bytes = framebuffer_bytes;
    gpu_memory_total_bytes = vmware_svga.fb_size != 0 ? vmware_svga.fb_size : framebuffer_bytes * 4u;
    if (gpu_memory_total_bytes < 2u * 1024u * 1024u) {
        gpu_memory_total_bytes = 2u * 1024u * 1024u;
    }
    if (gpu_memory_total_bytes < gpu_memory_used_bytes) {
        gpu_memory_total_bytes = gpu_memory_used_bytes;
    }

    cpu_usage_history[perf_history_index] = cpu_usage_percent;
    ram_usage_history[perf_history_index] = ram_usage_percent;
    gpu_usage_history[perf_history_index] = gpu_usage_percent;
    disk_usage_history[perf_history_index] = disk_usage_percent;
    perf_history_index = (perf_history_index + 1) % 64;

    if (cpu_has_tsc) {
        perf_window_start_cycles = (uint32_t)rdtsc_read();
        perf_busy_cycle_accum = 0;
        perf_window_ready = true;
    }
}


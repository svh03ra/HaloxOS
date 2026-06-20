// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: init.c, all of init states.

// This repository is licensed under the GNU General Public License.

static void init_state(void) {
    build_system_palette();
    init_theme_colors();
    init_desktop_icon_defaults();
    desktop_icon_persistence_enabled = false;
    selected_desktop_icon = -1;
    desktop_icon_press = -1;
    drag_desktop_icon = -1;
    desktop_icon_drag_moved = false;
    desktop_icon_press_was_selected = false;
    desktop_icon_menu_open = false;
    start_app_menu_open = false;
    power_menu_open = false;
    desktop_clipboard_valid = false;
    desktop_clipboard_cut = false;
    desktop_rename_active = false;
    desktop_rename_icon = -1;
    load_desktop_icon_positions();
    clear_boot_status();
    boot_menu_dirty = true;
    boot_terminal_dirty = true;
    boot_terminal_last_blink = 0xFFFFFFFFu;
    last_input_tick = 0;
    keyboard_alt = false;
    keyboard_ctrl = false;
    task_manager_tab = 0;
    task_manager_selected_process = 0;
    task_manager_confirm_kill = false;
    task_manager_kill_target = -1;
    gpu_palette_scroll = 0;
    task_manager_gpu_scroll_drag = false;
    task_manager_gpu_scroll_drag_offset = 0;
    memset_local(cpu_usage_history, 0, sizeof(cpu_usage_history));
    memset_local(gpu_usage_history, 0, sizeof(gpu_usage_history));
    memset_local(ram_usage_history, 0, sizeof(ram_usage_history));
    memset_local(disk_usage_history, 0, sizeof(disk_usage_history));
    last_desktop_redraw_input_tick = 0xFFFFFFFFu;
    last_desktop_redraw_second = 0xFFFFFFFFu;
    last_desktop_redraw_perf_phase = 0xFFFFFFFFu;
    last_desktop_redraw_terminal_blink = 0xFFFFFFFFu;
    last_desktop_redraw_snake_tick = 0xFFFFFFFFu;
    last_performance_sample_phase = 0xFFFFFFFFu;
    perf_window_start_cycles = 0;
    perf_busy_cycle_accum = 0;
    perf_window_ready = false;
    terminal_reset(&boot_term);
    terminal_reset(&cmd_term);
    terminal_reset(&debug_term);
    debug_overlay_open = false;
    debug_pending_action = DEBUG_ACTION_NONE;
    debug_history_count = 0;
    debug_history_cursor = 0;
    debug_memory_view_open = false;
    debug_memory_mode = DEBUG_MEMORY_MODE_HEX;
    debug_memory_base = 0;
    debug_memory_cursor = 0;
    debug_memory_edit_nibble = -1;
    debug_edited_range_count = 0;
    debug_forced_fault_reason = NULL;
    cmd_term.wrap_chars = 51;
    terminal_add_line(&cmd_term, "Welcome to HaloxOS Terminal:");
    terminal_add_line(&cmd_term, "Type 'help' for show all comannds to use.");
    memset_local(paint_canvas, color_white, sizeof(paint_canvas));
    random_state = ((uint32_t)cmos_read(0x00) << 24) |
                   ((uint32_t)cmos_read(0x02) << 16) |
                   ((uint32_t)cmos_read(0x04) << 8) |
                   cmos_read(0x07);
    random_state ^= 0xA5A5A5A5u;
    reset_snake();
    snake_spawn_food();
    reset_guess();
    mines_place();
}

void kernel_main(uint32_t magic, const MultibootInfo *mbi) {
    uint32_t frame_cycle_start = 0;
    uint32_t frame_cycle_end = 0;

    serial_init();
    serial_trace("INFO", "initialize kernel");
    serial_trace_hex_value("INFO", "kernel multiboot magic", magic);
    serial_trace_hex_value("INFO", "kernel entry address", (uint32_t)(uintptr_t)&kernel_main);
    boot_drive_valid = false;
    boot_drive_number = 0;
    if (magic == 0x2BADB002 && mbi != NULL && (mbi->flags & (1u << 1)) != 0) {
        boot_drive_valid = true;
        boot_drive_number = (uint8_t)(mbi->boot_device & 0xFFu);
    }
    detect_boot_drive_info(mbi);
    serial_trace_disk_details();
    ram_total_bytes = detect_total_ram_bytes(mbi);
    serial_trace_uint_value("INFO", "RAM total bytes", ram_total_bytes);

    init_framebuffer(magic, mbi);
    serial_trace_video_mode("initialize graphics");
    video_mode_switch_available = detect_video_mode_switch();
    if (!video_mode_switch_available) {
        serial_trace_concat("WARNING", "video mode switch unavailable on ", video_backend_name());
    }
    init_interrupts();
    serial_trace("INFO", "initialize interrupts");
    init_state();
    enter_boot_text_mode();
    draw_everything();
    present();
    init_mouse();
    __asm__ volatile ("sti");
    init_cpu_monitoring();
    serial_trace("INFO", cpu_has_cpuid ? "CPU CPUID available" : "CPU CPUID unavailable");
    serial_trace("INFO", cpu_has_tsc ? "CPU TSC available" : "CPU TSC unavailable");
    calibrate_cpu_speed();
    serial_trace_uint_value("INFO", "CPU speed MHz", cpu_speed_mhz);
    if (cpu_has_tsc) {
        perf_window_start_cycles = (uint32_t)rdtsc_read();
        perf_busy_cycle_accum = 0;
        perf_window_ready = true;
    }
    last_performance_sample_phase = timer_ticks / PERF_UPDATE_TICKS;
    last_input_tick = timer_ticks;

    for (;;) {
        uint32_t frame_tick = timer_ticks;
        uint32_t perf_phase = timer_ticks / PERF_UPDATE_TICKS;
        if (cpu_has_tsc) {
            frame_cycle_start = rdtsc_read();
        }
        poll_input();
        update_state();
        if (system_state != STATE_DESKTOP || desktop_should_redraw()) {
            draw_everything();
            present();
            if (system_state == STATE_DESKTOP) {
                mark_desktop_redrawn();
            }
        }
        if (cpu_has_tsc) {
            frame_cycle_end = (uint32_t)rdtsc_read();
            if (perf_window_ready) {
                perf_busy_cycle_accum += frame_cycle_end - frame_cycle_start;
            }
        }
        if (perf_phase != last_performance_sample_phase) {
            update_performance_metrics(0);
            last_performance_sample_phase = perf_phase;
        }

        if (system_state == STATE_SHUTDOWN) {
            draw_everything();
            present();
            if (!attempt_poweroff()) {
                shutdown_poweroff_failed = true;
                draw_everything();
                present();
            }
            __asm__ volatile ("cli");
            for (;;) {
                __asm__ volatile ("hlt");
            }
        }

        while (timer_ticks == frame_tick) {
            cpu_halt_once();
            if (shutdown_pending) {
                break;
            }
        }
    }
}

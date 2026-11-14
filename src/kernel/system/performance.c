// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: performance.c, used with task manager info.

// This repository is licensed under the GNU General Public License.

static const char *cpu_state_label(void) {
    if (cpu_halted_overlay) {
        return "Halted";
    }
    /* A running stress trigger is busy even though the user is not touching
     * the machine, so it must not be labelled as waiting. */
    if (!stress_test_running &&
        timer_ticks - last_input_tick > TIMER_HZ * 2u && open_window_count() <= 1) {
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

/*
 * Timer-interrupt watchdog.
 *
 * calibrate_cpu_speed() used to wait for IRQ0 with nothing but HLT.  If
 * the PIT never reaches the PIC (or the PIC masks it), that loop sleeps
 * forever and the machine appears hung right after the self-test.  The
 * probe and calibration below spin with a TSC/port-0x80 deadline instead,
 * and flag the failure so kernel_main can keep running without HLT.
 *
 * 500M cycles is ~167ms at 3GHz, ~1s at 500MHz and ~2.5s at 200MHz -
 * comfortably longer than one 60Hz tick, but still bounded on a CPU
 * whose TSC never advances (a broken TSC also cannot block the boot).
 */
#define TIMER_PROBE_MAX_CYCLES 500000000ULL
#define TIMER_PROBE_NO_TSC_LOOPS 2000000u
#define TIMER_PROBE_MAX_SPINS 100000000ULL
#define TIMER_FALLBACK_FRAME_WAIT 20000u

static bool timer_irq_verified = false;

static uint16_t pit_read_counter(void) {
    uint8_t low;
    uint8_t high;

    /* Latch channel 0, then read the latched pair.  This does not
     * disturb the rate generator, so a running desktop is unaffected. */
    outb(0x43, 0x00);
    low = inb(0x40);
    high = inb(0x40);
    return (uint16_t)((uint16_t)low | ((uint16_t)high << 8));
}

static uint8_t pic_read_register(uint8_t ocw3) {
    outb(0x20, ocw3);
    return inb(0x20);
}

static void trace_timer_failure(void) {
    uint8_t irr = pic_read_register(0x0A);
    uint8_t isr = pic_read_register(0x0B);
    uint8_t imr = inb(0x21);
    uint16_t pit_a = pit_read_counter();
    uint16_t pit_b;

    io_wait();
    io_wait();
    pit_b = pit_read_counter();

    serial_trace_hex_value("WARNING", "timer: master PIC IRR", irr);
    serial_trace_hex_value("WARNING", "timer: master PIC ISR", isr);
    serial_trace_hex_value("WARNING", "timer: master PIC IMR", imr);
    serial_trace_hex_value("WARNING", "timer: PIT counter sample A", pit_a);
    serial_trace_hex_value("WARNING", "timer: PIT counter sample B", pit_b);
    if (pit_a == pit_b) {
        serial_trace("WARNING", "timer: PIT counter is not advancing");
    }
}

/*
 * Wait for timer_ticks to advance, but never sleep in HLT: if IRQ0 is
 * dead, HLT would never return.  The TSC (or a bounded port-0x80 loop
 * when there is no TSC) provides the escape hatch.
 */
static bool timer_wait_ticks_bounded(uint32_t ticks, uint64_t max_cycles) {
    uint32_t start_tick = timer_ticks;
    uint64_t start_cycles;
    uint64_t spins = 0;

    if (cpu_has_tsc) {
        start_cycles = rdtsc_read();
        while ((uint32_t)(timer_ticks - start_tick) < ticks) {
            if ((rdtsc_read() - start_cycles) > max_cycles ||
                ++spins > TIMER_PROBE_MAX_SPINS) {
                return false;
            }
            __asm__ volatile ("pause");
        }
        return true;
    }

    for (uint32_t slow_spins = 0; slow_spins < TIMER_PROBE_NO_TSC_LOOPS; ++slow_spins) {
        if ((uint32_t)(timer_ticks - start_tick) >= ticks) {
            return true;
        }
        io_wait();
        __asm__ volatile ("pause");
    }
    return false;
}

/*
 * Confirm that one PIT tick actually reaches the CPU before any code
 * depends on timer_ticks.  Called after STI; returns false (and logs the
 * PIC/PIT state) when IRQ0 is not being delivered.
 */
static bool timer_probe_boot_irq(void) {
    /* The probe spins without HLT, so make sure the interrupt flag is
     * actually set: a self-test that briefly cleared it must not look
     * like a dead PIT. */
    __asm__ volatile ("sti");
    timer_irq_verified = timer_wait_ticks_bounded(1u, TIMER_PROBE_MAX_CYCLES);

    if (timer_irq_verified) {
        serial_trace("INFO", "timer: IRQ0 tick verified");
    } else {
        serial_trace("WARNING", "timer: IRQ0 did not tick within the probe window");
        trace_timer_failure();
    }
    return timer_irq_verified;
}

static void calibrate_cpu_speed(void) {
    uint32_t start_tick;
    uint32_t elapsed_ticks;
    uint32_t cycles_per_tick;
    uint64_t start_cycles;
    uint64_t end_cycles;
    uint64_t deadline;

    if (!cpu_has_tsc || cpu_speed_mhz != 0 || !timer_irq_verified) {
        return;
    }

    /* Measure two ticks first so the per-tick estimate is not distorted
     * by starting halfway through a PIT period. */
    start_tick = timer_ticks;
    start_cycles = rdtsc_read();
    if (!timer_wait_ticks_bounded(2u, TIMER_PROBE_MAX_CYCLES * 2u)) {
        timer_irq_verified = false;
        serial_trace("WARNING", "timer: IRQ0 stalled during CPU calibration");
        trace_timer_failure();
        return;
    }
    end_cycles = rdtsc_read();
    elapsed_ticks = timer_ticks - start_tick;
    if (elapsed_ticks == 0) {
        return;
    }
    cycles_per_tick = (uint32_t)(end_cycles - start_cycles) / elapsed_ticks;
    if (cycles_per_tick == 0) {
        return;
    }

    start_tick = timer_ticks;
    start_cycles = rdtsc_read();
    deadline = (uint64_t)cycles_per_tick * 45u; /* three times the 15-tick span */
    while ((uint32_t)(timer_ticks - start_tick) < 15u) {
        if ((rdtsc_read() - start_cycles) > deadline) {
            timer_irq_verified = false;
            serial_trace("WARNING", "timer: IRQ0 stalled during CPU calibration");
            trace_timer_failure();
            return;
        }
        __asm__ volatile ("pause");
    }
    end_cycles = rdtsc_read();
    elapsed_ticks = timer_ticks - start_tick;
    /* 32-bit only: the freestanding kernel does not link libgcc, so a
     * 64-bit divide would pull in __udivdi3.  One 60Hz tick is
     * 16,667,000 cycles per MHz, hence the fixed 16667 divisor. */
    cycles_per_tick = (uint32_t)(end_cycles - start_cycles) / elapsed_ticks;
    cpu_speed_mhz = cycles_per_tick / 16667u;
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
    uint32_t stress_ram_bytes = 0;
    /* The kernel image itself (code, rodata, data, bss and the boot stack)
     * plus the embedded WAD module are real RAM the gauge never counted;
     * without them a loaded 16 MB machine read as half empty.  The image
     * links at 2 MB and __bss_end is the end of the last bss byte. */
    extern uint8_t __bss_end[];
    uint32_t kernel_image_bytes =
        (uint32_t)(uintptr_t)__bss_end - 0x200000u;

    if (stress_test_running &&
        (stress_test_mode == 1u || stress_test_mode == 2u)) {
        /* The working set is real RAM that has actually been touched (the
         * Memory/GPU pool), not a value scaled from the intensity setting. */
        stress_ram_bytes = stress_pool_touched;
    }

    if (cpu_has_tsc && perf_window_ready) {
        total_cycles = (uint32_t)(rdtsc_read() - perf_window_start_cycles);
    }

    if (cpu_has_tsc && perf_window_ready && total_cycles != 0) {
        uint32_t usage = 0;

        /* Do not multiply the accumulator by 100 first: once the machine is
         * busy for more than ~13% of the window that product overflows
         * 32 bits, which made a pegged CPU report as 3%.  Divide the window
         * into 1% units instead, so the division stays in range. */
        if (total_cycles >= 100u) {
            uint32_t cycles_per_percent = (total_cycles + 99u) / 100u;

            usage = perf_busy_cycle_accum / cycles_per_percent;
        }
        cpu_usage_percent = (uint8_t)(usage > 100u ? 100u : usage);
    } else {
        /* No TSC or no closed sample window: do not invent load from open
         * windows, menus or the active app.  An idle desktop is 0%, and a
         * running CPU trigger can only report the share it was asked for. */
        if (stress_test_running && stress_test_mode == 0u) {
            cpu_usage_percent = (uint8_t)((stress_power * 100u) /
                                          STRESS_POWER_MAX);
        } else {
            cpu_usage_percent = 0;
        }
    }

    {
        int gpu_load = 0;

        if (stress_test_running && stress_test_mode == 2u) {
            /* Real pixel throughput: the budget is the number of pixels the
             * stress renderer will actually paint this frame.  Compare it
             * with one full pass over its client area, which is exactly what
             * the 100% power setting asks for, so the gauge follows the real
             * ramp instead of a formula built from the intensity box.
             *
             * An idle desktop is 0%: rendering here is software into the
             * shadow plane, so open windows and menus do not prove GPU
             * work.  The old baseline (cpu/2 + 6% per window + 8% per menu)
             * made Task Manager report 20-90% while the machine was idle. */
            const Window *stress_window = &windows[APP_STRESS_TEST];
            uint32_t ref_w = stress_window->w > 18
                                 ? (uint32_t)(stress_window->w - 18) : 0u;
            uint32_t ref_h = stress_window->h > 34
                                 ? (uint32_t)(stress_window->h - 34) : 0u;
            uint32_t reference = ref_w * ref_h;

            if (reference != 0u) {
                uint32_t painted =
                    (stress_gpu_budget_target * 100u + reference - 1u) /
                    reference;

                gpu_load = (int)(painted > 100u ? 100u : painted);
            }
        }
        gpu_estimate = (uint8_t)clampi(gpu_load, 0, 100);
        gpu_usage_percent = gpu_estimate;
    }
    ram_total_bytes = ram_total_bytes == 0 ? (32u * 1024u * 1024u) : ram_total_bytes;
    ram_used_bytes = kernel_image_bytes + (uint32_t)shadow_plane_capacity +
                     doom_wad_module_bytes + stress_ram_bytes +
                     framebuffer_bytes + 768u * 1024u +
                     (uint32_t)windows_open * 128u * 1024u;
    if (active_window == APP_TASK_MANAGER) {
        ram_used_bytes += 128u * 1024u;
    }
    if (ram_used_bytes > ram_total_bytes) {
        ram_used_bytes = ram_total_bytes;
    }
    {
        /* Divide first: ram_used_bytes * 100 overflows 32 bits once the
         * machine uses more than ~42 MB, which made a nearly full RAM gauge
         * report a random 20-40%. */
        uint32_t per_percent = ram_total_bytes / 100u;

        if (per_percent == 0u) {
            per_percent = 1u;
        }
        ram_usage_percent = (uint8_t)(ram_used_bytes / per_percent);
        if (ram_usage_percent > 100u) {
            ram_usage_percent = 100u;
        }
    }
    {
        /* No baseline from open windows or menus: a desktop that is not
         * reading a device is 0% disk load.  Only the measured ATA/ATAPI
         * throughput below can raise it. */
        int disk_load = 0;

        if (stress_test_running && stress_test_mode == 3u) {
            uint32_t now = timer_ticks;
            uint32_t elapsed = now - stress_disk_sample_tick;
            uint32_t delta = stress_disk_bytes_read - stress_disk_sample_bytes;
            uint32_t bytes_per_tick;
            uint32_t per_percent;
            uint32_t measured;
            int span;

            if (elapsed == 0u) {
                elapsed = 1u;
            }
            /* Real throughput from the bytes the ATA/ATAPI reads actually
             * returned, not a value invented from the intensity box.
             * Divide first: delta * TIMER_HZ * 100 overflows 32 bits on a
             * fast disk, which reported nonsense percentages. */
            bytes_per_tick = delta / elapsed;
            per_percent = (STRESS_DISK_FULL_BPS / TIMER_HZ) / 100u;
            if (per_percent == 0u) {
                per_percent = 1u;
            }
            measured = bytes_per_tick / per_percent;
            stress_disk_sample_bytes = stress_disk_bytes_read;
            stress_disk_sample_tick = now;
            span = 100 - disk_load;
            if (span < 0) {
                span = 0;
            }
            if (measured > (uint32_t)span) {
                measured = (uint32_t)span;
            }
            stress_disk_measured_percent = measured;
            disk_load += (int)measured;
        } else {
            stress_disk_sample_bytes = stress_disk_bytes_read;
            stress_disk_sample_tick = timer_ticks;
            stress_disk_measured_percent = 0u;
        }
        disk_usage_percent = (uint8_t)clampi(disk_load, 0, 100);
    }
    /* Total real bytes every storage backend transferred: ATA/ATAPI PIO
     * plus firmware INT 13h reads/writes.  The old value only counted
     * stress-test reads, so a USB boot that had no ATA device stayed at
     * 0 MB no matter how much the stick was actually read. */
    disk_io_megabytes = (uint32_t)(disk_io_total_bytes >> 20);
    gpu_memory_used_bytes = framebuffer_bytes;
    gpu_memory_total_bytes = vmware_svga.fb_size != 0 ? vmware_svga.fb_size : framebuffer_bytes * 4u;
    if (stress_test_running && stress_test_mode == 2u) {
        /* GPU memory behaves like the RAM gauge: the touched part of the
         * real working set is shown as used, and the reservation as total. */
        gpu_memory_used_bytes += stress_pool_touched;
        if (gpu_memory_total_bytes < framebuffer_bytes + stress_pool_bytes) {
            gpu_memory_total_bytes = framebuffer_bytes + stress_pool_bytes;
        }
    }
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


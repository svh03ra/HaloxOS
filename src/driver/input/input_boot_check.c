// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: input_boot_check.c, boot-time keyboard and mouse check.
//
// This repository is licensed under the GNU General Public License.

/*
 * Every boot has to answer one question before the menu becomes
 * interactive: is there a keyboard, and is there a mouse?
 *
 * Both input paths have already been probed when this runs: the legacy
 * 8042/PS/2 controller and the native xHCI HID driver.  A machine whose
 * 8042 keyboard is not usable depends on the native xHCI keyboard, so a
 * missing endpoint means a machine that cannot select a boot option.
 * A boot-time hot-plug is common (the keyboard is still enumerating when
 * the kernel starts), so the check retries the xHCI controller ten times
 * and prints the attempt count on screen.  If still no keyboard exists,
 * xhci_boot_recover() flushes the on-medium trace, detaches the boot stick
 * and takes the controller over: a full reset and root-port enumeration
 * create the HID endpoint the firmware never configured.  This is the
 * hard-reset recovery path.  Only if that also finds no keyboard does the
 * machine stop on the red BOOT ERROR screen.
 *
 * A missing mouse is reported but does not stop the boot: the keyboard is
 * what selects an option, and refusing to start a working machine because
 * no mouse is plugged into an xHCI port would be worse than continuing
 * without one.  The success line says so explicitly.
 *
 * Debug builds draw the full DEBUGGER panel: the controller line, the
 * connected root-port count, the input sources, the 8042 counters and the
 * retry handler.  The "Checking xHCI..." stage is black, an OK row is
 * painted with a green background, and a failed row uses the BOOT ERROR
 * red.  Release builds draw the same initialization screen - the heading,
 * the Keyboard/Mouse rows, the retry countdown and the success line - but
 * without the DEBUGGER heading and without any of the controller, source,
 * 8042 or handler detail rows.
 */

#define INPUT_BOOT_MAX_ATTEMPTS   10u
#define INPUT_BOOT_RETRY_FRAMES   12u   /* ~200 ms at 60 Hz */
#define INPUT_BOOT_SUCCESS_FRAMES 45u   /* ~750 ms at 60 Hz */

/* VGA text attributes.  16-colour text mode has no #32D95E; the green
 * here is the standard DAC green that the OK row can actually paint on
 * both the native VGA text buffer and the emulated framebuffer text
 * mode.  The failure rows reuse the white-on-red BOOT ERROR attribute. */
#define INPUT_BOOT_ATTR_CHECK  0x0Fu    /* white on black */
#define INPUT_BOOT_ATTR_OK     0x20u    /* black on green */
#define INPUT_BOOT_ATTR_FAIL   0x4Fu    /* white on red   */
#define INPUT_BOOT_ATTR_DETAIL 0x07u    /* gray on black  */

static bool boot_input_xhci_required;
static bool boot_input_keyboard_ok;
static bool boot_input_mouse_ok;
static uint32_t boot_input_attempt;
static bool boot_input_attempting;

/* Defined in render_dispatch.c, which is included after this fragment. */
static void draw_everything(void);

/*
 * PIT channel-0 polling gives a delay that works with interrupts off and
 * does not depend on timer_ticks having been verified yet.  The counter
 * counts down and reloads, so a value that jumps UP is one frame.  The
 * spin bound keeps a machine with no PIT from hanging the boot here.
 */
static uint16_t boot_input_pit_count(void) {
    uint16_t lo;
    uint16_t hi;

    outb(0x43, 0x00);   /* latch channel 0 for a stable read */
    lo = (uint16_t)inb(0x40);
    hi = (uint16_t)inb(0x40);
    return (uint16_t)((hi << 8) | lo);
}

static void boot_input_wait_frames(uint32_t frames) {
    for (uint32_t frame = 0; frame < frames; ++frame) {
        uint16_t last = boot_input_pit_count();

        for (uint32_t spins = 0; spins < 50000u; ++spins) {
            uint16_t now = boot_input_pit_count();

            if (now > last) {
                break; /* reload: one 60 Hz frame at the timer divisor */
            }
            last = now;
            io_wait();
        }
    }
}

static void boot_input_fill_row(uint8_t row, uint8_t attr) {
    char spaces[VGA_TEXT_COLS + 1];

    for (int i = 0; i < VGA_TEXT_COLS; ++i) {
        spaces[i] = ' ';
    }
    spaces[VGA_TEXT_COLS] = '\0';
    draw_text_mode_row(row, 0, spaces, attr);
}

static void boot_input_colored_row(uint8_t row, uint8_t attr, const char *text) {
    boot_input_fill_row(row, attr);
    draw_text_mode_row(row, 2, text, attr);
}

#if HALOXOS_CONFIG_DEBUG
/* Detail-row helpers: the "Keyboard source" / "Mouse source" line is part
 * of the debug panel only, so a release build does not compile them. */
static const char *boot_input_keyboard_source(void) {
    if (xhci_native_keyboard_ready()) {
        return "xHCI native";
    }
    if (ps2_keyboard_usable()) {
        return "PS/2 8042";
    }
    return "none";
}

static const char *boot_input_mouse_source(void) {
    if (xhci_native_mouse_ready()) {
        return "xHCI native";
    }
    if (ps2_mouse_usable()) {
        return "PS/2 aux";
    }
    return "none";
}
#endif /* HALOXOS_CONFIG_DEBUG */

static void boot_input_status_text(char *line, size_t max, const char *label,
                                   bool ok, bool final_fail) {
    size_t len = 0;

    line[0] = '\0';
    append_string(line, &len, max, label);
    if (ok) {
        append_string(line, &len, max, "... OK");
    } else if (final_fail) {
        append_string(line, &len, max, "... FAIL");
    } else {
        append_string(line, &len, max, "... DISCONNECTED");
    }
}

/*
 * Draw one state of the check.  final_fail turns a still-missing device
 * row from "DISCONNECTED" (black, still trying) into "FAIL" (red, no
 * more attempts).
 *
 * Both build types show this screen: it is the xHCI startup stage before
 * the boot menu.  The DEBUGGER heading and every detail row (controller
 * line, root-port count, input sources, 8042 counters, retry handler) are
 * debug-build only - a release build shows the plain "Checking xHCI input
 * devices..." heading, the two OK/FAIL rows, the retry countdown and the
 * success line.
 */
static void boot_input_render(bool final_fail) {
    char line[96];
    bool kbd_fail = final_fail && !boot_input_keyboard_ok;
    bool mouse_fail = final_fail && !boot_input_mouse_ok;

    /* Graphical text screens: 12px cells tighten the white line gaps a
     * little; native VGA text mode keeps its hardware 8x16 cells. */
    text_mode_set_cell_height(12);
    vga_text_clear(0x00);
#if HALOXOS_CONFIG_DEBUG
    draw_text_mode_center(1, "***** DEBUGGER *****", INPUT_BOOT_ATTR_CHECK);
    draw_text_mode_center(3, "Checking xHCI...", INPUT_BOOT_ATTR_CHECK);
#else
    draw_text_mode_center(3, "Checking xHCI input devices...", INPUT_BOOT_ATTR_CHECK);
#endif

    boot_input_status_text(line, sizeof(line), "Keyboard",
                           boot_input_keyboard_ok, kbd_fail);
    boot_input_colored_row(5, boot_input_keyboard_ok ? INPUT_BOOT_ATTR_OK
                             : (kbd_fail ? INPUT_BOOT_ATTR_FAIL
                                         : INPUT_BOOT_ATTR_CHECK), line);

    boot_input_status_text(line, sizeof(line), "Mouse",
                           boot_input_mouse_ok, mouse_fail);
    boot_input_colored_row(6, boot_input_mouse_ok ? INPUT_BOOT_ATTR_OK
                             : (mouse_fail ? INPUT_BOOT_ATTR_FAIL
                                           : INPUT_BOOT_ATTR_CHECK), line);

#if HALOXOS_CONFIG_DEBUG
    {
        size_t len = 0;

        /* The detail lines stay single-spaced so the panel fits; there is
         * one blank row between sections, like the BOOT ERROR screen.  The
         * device rows above stay a pair. */
        draw_text_mode_row(8, 0, xhci_input_status_text(), INPUT_BOOT_ATTR_DETAIL);

        line[0] = '\0';
        len = 0;
        append_string(line, &len, sizeof(line), "xHCI root ports connected: ");
        append_uint(line, &len, sizeof(line), xhci_connected_ports());
        append_string(line, &len, sizeof(line), "  mode: ");
        append_string(line, &len, sizeof(line),
                      boot_input_xhci_required ? "xHCI required"
                                               : "legacy path available");
        draw_text_mode_row(9, 0, line, INPUT_BOOT_ATTR_DETAIL);

        line[0] = '\0';
        len = 0;
        append_string(line, &len, sizeof(line), "Keyboard source: ");
        append_string(line, &len, sizeof(line), boot_input_keyboard_source());
        append_string(line, &len, sizeof(line), "  Mouse source: ");
        append_string(line, &len, sizeof(line), boot_input_mouse_source());
        draw_text_mode_row(10, 0, line, INPUT_BOOT_ATTR_DETAIL);

        draw_text_mode_row(11, 0, ps2_input_status_text(), INPUT_BOOT_ATTR_DETAIL);

        line[0] = '\0';
        len = 0;
        append_string(line, &len, sizeof(line),
                      "Handler: input_boot_check -> xhci_boot_reconnect  attempts ");
        append_uint(line, &len, sizeof(line), boot_input_attempt);
        append_string(line, &len, sizeof(line), " / ");
        append_uint(line, &len, sizeof(line), INPUT_BOOT_MAX_ATTEMPTS);
        draw_text_mode_row(12, 0, line, INPUT_BOOT_ATTR_DETAIL);

        draw_text_mode_row(13, 0,
                           serial_bootlog_bios_keyboard_usable()
                               ? "Firmware INT 16h: present (fallback only, not trusted)"
                               : "Firmware INT 16h: unavailable",
                           INPUT_BOOT_ATTR_DETAIL);
    }
#endif

    if (boot_input_attempting && !boot_input_keyboard_ok) {
        size_t len = 0;

        line[0] = '\0';
        append_string(line, &len, sizeof(line), "Reconnect attempt ");
        append_uint(line, &len, sizeof(line), boot_input_attempt);
        append_string(line, &len, sizeof(line), " of ");
        append_uint(line, &len, sizeof(line), INPUT_BOOT_MAX_ATTEMPTS);
        append_string(line, &len, sizeof(line), "...");
        draw_text_mode_row(15, 2, line, INPUT_BOOT_ATTR_CHECK);

        line[0] = '\0';
        len = 0;
        append_string(line, &len, sizeof(line), "Attempts left: ");
        append_uint(line, &len, sizeof(line),
                    INPUT_BOOT_MAX_ATTEMPTS - boot_input_attempt);
        draw_text_mode_row(16, 2, line, INPUT_BOOT_ATTR_CHECK);
    }

    if (boot_input_keyboard_ok) {
        boot_input_colored_row(15, INPUT_BOOT_ATTR_OK,
                               boot_input_mouse_ok
                                   ? "System started, SUCCESS!!!"
                                   : "System started (keyboard only), mouse missing");
    } else if (final_fail) {
        boot_input_colored_row(15, INPUT_BOOT_ATTR_FAIL,
                               "System halted: inputs permanently disabled");
    }

    present();
    text_mode_set_cell_height(16);
}

/*
 * The red screen the boot loader uses for a fatal boot is text mode,
 * white on red, with a clear BOOT ERROR heading.  The kernel stops here
 * when no keyboard survived the ten xHCI reconnect attempts: without one
 * the boot menu cannot be driven, so continuing would only leave the
 * machine on a screen that ignores every key.  The trace is flushed to
 * the boot medium first so the failure is readable afterwards.
 */
static void boot_input_fatal_screen(void) {
    char line[96];
    size_t len = 0;

    text_mode_set_cell_height(12);
    vga_text_clear(INPUT_BOOT_ATTR_FAIL);
    draw_text_mode_center(1, "***** BOOT ERROR!!! *****", INPUT_BOOT_ATTR_FAIL);
    draw_text_mode_center(3, "xHCI input initialization failed.", INPUT_BOOT_ATTR_FAIL);

    boot_input_status_text(line, sizeof(line), "Keyboard",
                           boot_input_keyboard_ok, true);
    draw_text_mode_center(5, line, INPUT_BOOT_ATTR_FAIL);
    boot_input_status_text(line, sizeof(line), "Mouse",
                           boot_input_mouse_ok, true);
    draw_text_mode_center(6, line, INPUT_BOOT_ATTR_FAIL);

    draw_text_mode_center(8, "10 reconnect attempts failed.", INPUT_BOOT_ATTR_FAIL);
    if (!boot_input_keyboard_ok && !boot_input_mouse_ok) {
        draw_text_mode_center(9, "All inputs are permanently disabled and the system cannot continue.",
                              INPUT_BOOT_ATTR_FAIL);
    } else {
        draw_text_mode_center(9, "No keyboard is available and the system cannot continue.",
                              INPUT_BOOT_ATTR_FAIL);
    }
    draw_text_mode_center(11, "Connect a USB keyboard and mouse to the xHCI ports,",
                          INPUT_BOOT_ATTR_FAIL);
    draw_text_mode_center(12, "then restart the machine.",
                          INPUT_BOOT_ATTR_FAIL);
    if (boot_medium_is_usb || boot_medium_on_xhci) {
        draw_text_mode_center(14,
                              "USB boot medium: xHCI takeover attempted for input recovery.",
                              INPUT_BOOT_ATTR_FAIL);
    }

    line[0] = '\0';
    append_string(line, &len, sizeof(line), "xHCI: ");
    append_string(line, &len, sizeof(line), xhci_input_status_text());
    draw_text_mode_center(15, line, INPUT_BOOT_ATTR_FAIL);
    draw_text_mode_center(17, "HaloxOS boot error: xHCI input failure",
                          INPUT_BOOT_ATTR_FAIL);
    present();

    serial_trace("ERROR", "input check: boot stopped, no keyboard after ten xHCI reconnect attempts");
    serial_bootlog_flush("xHCI input failure");
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* The XHCI_HID_FORCE build tests the native path even when an emulated
 * 8042 is present.  Do not let that 8042 mask a missing xHCI endpoint in
 * the boot check: the knob exists precisely to exercise enumeration. */
static bool boot_input_keyboard_available(void) {
#if HALOXOS_FORCE_XHCI_HID
    return xhci_native_keyboard_ready();
#else
    return ps2_keyboard_usable() || xhci_native_keyboard_ready();
#endif
}

static bool boot_input_mouse_available(void) {
#if HALOXOS_FORCE_XHCI_HID
    return xhci_native_mouse_ready();
#else
    return ps2_mouse_usable() || xhci_native_mouse_ready();
#endif
}

/*
 * Run once, after the 8042 and xHCI setup and before the main loop.
 * xhci_required is true when the native keyboard driver is the machine's
 * only keyboard source (the 8042 path was not usable, or the bring-up
 * knob forced the native path).
 */
static void boot_input_check(bool xhci_required) {
    boot_input_xhci_required = xhci_required;
    boot_input_keyboard_ok = boot_input_keyboard_available();
    boot_input_mouse_ok = boot_input_mouse_available();
    boot_input_attempt = 0;
    boot_input_attempting = false;

    serial_trace("INFO", "input check: verifying boot keyboard and mouse");
    boot_input_render(false);
    boot_input_wait_frames(6);

    while (xhci_required && !boot_input_keyboard_ok &&
           boot_input_attempt < INPUT_BOOT_MAX_ATTEMPTS) {
        ++boot_input_attempt;
        boot_input_attempting = true;
        serial_trace_uint_value("INFO", "input check: xHCI reconnect attempt",
                                boot_input_attempt);
        boot_input_render(false);
        (void)xhci_boot_reconnect();
        boot_input_keyboard_ok = boot_input_keyboard_available();
        boot_input_mouse_ok = boot_input_mouse_available();
        if (boot_input_keyboard_ok) {
            break;
        }
        boot_input_render(false);
        boot_input_wait_frames(INPUT_BOOT_RETRY_FRAMES);
    }
    boot_input_attempting = false;

    /*
     * Ten adoption retries only prove the firmware never configured a
     * usable endpoint; they cannot create one.  Take the controller over
     * and enumerate the ports ourselves as the last chance before the
     * fatal screen.  xhci_boot_recover flushes the trace and detaches the
     * medium first, because the reset breaks the firmware's USB boot path;
     * with no keyboard the machine cannot continue anyway.  Run it even
     * after a previous full-init attempt that found no HID: the keyboard
     * may have appeared during the retries, and there is nothing left to
     * preserve at this point.
     */
    if (xhci_required && !boot_input_keyboard_ok) {
        serial_trace("WARNING",
                     "input check: adoption retries exhausted, trying full xHCI takeover");
        boot_input_render(false);
        (void)xhci_boot_recover();
        boot_input_keyboard_ok = boot_input_keyboard_available();
        boot_input_mouse_ok = boot_input_mouse_available();
    }

    if (xhci_required && !boot_input_keyboard_ok) {
        boot_input_render(true);
        boot_input_wait_frames(INPUT_BOOT_SUCCESS_FRAMES);
        boot_input_fatal_screen();
    }

    serial_trace("INFO", "input check: keyboard ready");
    if (!boot_input_mouse_ok) {
        serial_trace("WARNING", "input check: no mouse; continuing keyboard-only");
    }

    boot_input_render(false);
    boot_input_wait_frames(INPUT_BOOT_SUCCESS_FRAMES);

    /* Put the boot menu back over the panel before the main loop starts. */
    boot_menu_dirty = true;
    draw_everything();
    present();
}

// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: keyboard_mouse.c, keyboard and mouse input driver.

// This repository is licensed under the GNU General Public License.

/* Native xHCI HID input is defined in xhci_hid.c, included later in the
 * kernel amalgamation.  Forward declarations keep the per-frame poll and
 * the fallback gate close to the legacy input code that uses them. */
static void xhci_hid_poll(void);
static bool xhci_hid_input_ready(void);

/*
 * Firmware INT 16h keyboard fallback: OFF by default.
 *
 * The probe (INT 16h AH=01h) returns fine on every machine seen so
 * far, but the follow-up read (AH=00h) is a BLOCKING firmware call:
 * it does not return until a key arrives.  VMware's PhoenixBIOS even
 * reports ZF=0 from AH=01h with an empty buffer, so the kernel called
 * AH=00h on its first fallback poll and never came back.  The boot
 * menu froze on the last painted frame and every key was ignored -
 * exactly the real-hardware symptom this fallback was meant to cure.
 *
 * Never arm this without a firmware that documents a non-blocking
 * read.  A machine whose keyboard is only visible to firmware needs
 * the native USB HID driver, not a blocking INT 16h call.
 * BIOS_KBD_FALLBACK=1 still builds the old path for experiments.
 */
#ifndef HALOXOS_DISABLE_BIOS_KEYBOARD_FALLBACK
#define HALOXOS_DISABLE_BIOS_KEYBOARD_FALLBACK 1
#endif

static void enqueue_key(KeyCode code, char ch) {
    int next = (key_tail + 1) % 64;

    if (next == key_head) {
        return;
    }
    last_input_tick = timer_ticks;
    ui_input_dirty = true;
    key_queue[key_tail].code = code;
    key_queue[key_tail].ch = ch;
    key_tail = next;
}

/* A keyboard IRQ alone does not prove that a usable keyboard is attached:
 * controllers can raise an empty/spurious IRQ during firmware handoff, and
 * command/ACK bytes can arrive without representing user input.  Only a
 * decoded PS/2 key event should suppress the BIOS fallback. */
static volatile bool ps2_keyboard_event_seen = false;
/* Set only when the 8042 actually delivered a keyboard-port byte.  The
 * native xHCI rescue path must not decode a duplicate USB keyboard when the
 * legacy controller is genuinely working; ps2_keyboard_event_seen is also
 * set by xHCI-emitted scancodes, so it cannot make that distinction. */
static volatile bool ps2_hardware_key_seen = false;

static void enqueue_ps2_key(KeyCode code, char ch) {
    ps2_keyboard_event_seen = true;
    enqueue_key(code, ch);
}

/* Physical key state per scancode ([0] = plain, [1] = E0-extended).
 * The OS queue only delivers press events; apps that need HELD keys
 * (the DOOM port's movement/fire) sync from this table instead. */
static bool key_held[2][128];

/* These counters are intentionally small and low-level.  The boot menu
 * displays them on debug builds, so a photograph of a hung real machine can
 * distinguish "the USB keyboard never reached the legacy controller" from
 * "IRQ1 fired but the menu did not consume the key" without needing COM1. */
static volatile uint32_t keyboard_irq_count = 0;
static volatile uint32_t keyboard_byte_count = 0;
static volatile uint32_t mouse_irq_count = 0;
static volatile uint32_t mouse_byte_count = 0;
static uint8_t ps2_controller_config = 0;
static bool ps2_controller_config_valid = false;
/* The enable command's 0xFA acknowledgement (or a byte already received
 * from the auxiliary device) is what tells the boot input check that a
 * PS/2 mouse is actually present. */
static bool ps2_mouse_enabled = false;

/* PS/2 Set-1 Pause is a six-byte make sequence (E1 1D 45 E1 9D C5)
 * with no normal break code.  Keep a tiny decoder so applications such
 * as DOOM can receive KEY_PAUSE just like a PC keyboard. */
static uint8_t keyboard_pause_state = 0;

static bool keyboard_key_held(uint8_t base, bool extended) {
    return key_held[extended ? 1 : 0][base & 0x7Fu];
}

static bool dequeue_key(KeyEvent *event) {
    if (key_head == key_tail) {
        return false;
    }
    *event = key_queue[key_head];
    key_head = (key_head + 1) % 64;
    return true;
}

static char scancode_to_char(uint8_t scancode, bool shifted, bool capslock, bool numlock) {
    static const char normal[] =
        "\0\0331234567890-=\0\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shifted_map[] =
        "\0\033!@#$%^&*()_+\0\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

    /* Numeric keypad in NumLock mode becomes printable input.  When
     * NumLock is clear the same physical keys are left for navigation
     * handling below (or ignored if DOOM has no binding for them). */
    if (numlock) {
        switch (scancode) {
            case 0x37: return '*';
            case 0x4A: return '-';
            case 0x4E: return '+';
            case 0x35: return '/';
            case 0x4F: return '1';
            case 0x50: return '2';
            case 0x51: return '3';
            case 0x4B: return '4';
            case 0x4C: return '5';
            case 0x4D: return '6';
            case 0x47: return '7';
            case 0x48: return '8';
            case 0x49: return '9';
            case 0x52: return '0';
            case 0x53: return '.';
            default: break;
        }
    }

    if (scancode >= sizeof(normal) - 1) {
        return 0;
    }
    char normal_ch = normal[scancode];
    char shifted_ch = shifted_map[scancode];
    /* Caps Lock changes alphabetic keys only; punctuation remains
     * controlled exclusively by Shift.  Shift+Caps therefore returns
     * lowercase letters, matching normal PC keyboard behavior. */
    if (capslock && normal_ch >= 'a' && normal_ch <= 'z') {
        return shifted ? normal_ch : (char)(normal_ch - 'a' + 'A');
    }
    return shifted ? shifted_ch : normal_ch;
}

static void handle_scancode(uint8_t code) {
    bool released;
    uint8_t base;

    /* Decode the special six-byte Set-1 Pause make sequence without
     * allowing its bytes to fall through as ordinary keys. */
    if (keyboard_pause_state != 0) {
        static const uint8_t pause_seq[] = {0x1D, 0x45, 0xE1, 0x9D, 0xC5};
        uint8_t expected = pause_seq[keyboard_pause_state - 1];
        if (code == expected) {
            ++keyboard_pause_state;
            if (keyboard_pause_state == 6) {
                keyboard_pause_state = 0;
                enqueue_ps2_key(KEY_PAUSE, 0);
            }
            return;
        }
        keyboard_pause_state = 0;
        /* The sequence was malformed. Continue handling this byte as a
         * fresh scancode so a normal key cannot get swallowed. */
    }

    if (code == 0xE1) {
        keyboard_pause_state = 1;
        return;
    }

    if (code == 0xE0) {
        keyboard_extended = true;
        return;
    }

    released = (code & 0x80u) != 0;
    base = code & 0x7Fu;
    key_held[keyboard_extended ? 1 : 0][base] = !released;

    if (base == 0x2A || base == 0x36) {
        keyboard_shift = !released;
        keyboard_extended = false;
        return;
    }

    if (base == 0x38) {
        keyboard_alt = !released;
        keyboard_extended = false;
        return;
    }

    if (base == 0x3A) {
        if (!released) {
            keyboard_capslock = !keyboard_capslock;
        }
        keyboard_extended = false;
        return;
    }

    if (base == 0x45) {
        if (!released) {
            keyboard_numlock = !keyboard_numlock;
        }
        keyboard_extended = false;
        return;
    }

    if (base == 0x1D) {
        keyboard_ctrl = !released;
        keyboard_extended = false;
        return;
    }

    if (released) {
        keyboard_extended = false;
        return;
    }

    if (keyboard_extended) {
        keyboard_extended = false;
        switch (base) {
            case 0x48: enqueue_ps2_key(KEY_UP, 0); return;
            case 0x50: enqueue_ps2_key(KEY_DOWN, 0); return;
            case 0x4B: enqueue_ps2_key(KEY_LEFT, 0); return;
            case 0x4D: enqueue_ps2_key(KEY_RIGHT, 0); return;
            case 0x47: enqueue_ps2_key(KEY_HOME, 0); return;
            case 0x4F: enqueue_ps2_key(KEY_END, 0); return;
            case 0x53: enqueue_ps2_key(KEY_DEL, 0); return;
            default: return;
        }
    }

    switch (base) {
        case 0x01: enqueue_ps2_key(KEY_ESC, 0); return;
        case 0x0E: enqueue_ps2_key(KEY_BACKSPACE, 0); return;
        case 0x0F: enqueue_ps2_key(KEY_TAB, 0); return;
        case 0x1C: enqueue_ps2_key(KEY_ENTER, '\n'); return;
        case 0x3B: enqueue_ps2_key(KEY_F1, 0); return;
        case 0x3C: enqueue_ps2_key(KEY_F2, 0); return;
        case 0x3D: enqueue_ps2_key(KEY_F3, 0); return;
        case 0x3E: enqueue_ps2_key(KEY_F4, 0); return;
        case 0x3F: enqueue_ps2_key(KEY_F5, 0); return;
        case 0x40: enqueue_ps2_key(KEY_F6, 0); return;
        case 0x41: enqueue_ps2_key(KEY_F7, 0); return;
        case 0x42: enqueue_ps2_key(KEY_F8, 0); return;
        case 0x43: enqueue_ps2_key(KEY_F9, 0); return;
        case 0x44: enqueue_ps2_key(KEY_F10, 0); return;
        case 0x57: enqueue_ps2_key(KEY_F11, 0); return;
        case 0x58: enqueue_ps2_key(KEY_F12, 0); return;
        case 0x53: enqueue_ps2_key(KEY_DEL, 0); return;
        default: {
            char ch = scancode_to_char(base, keyboard_shift, keyboard_capslock, keyboard_numlock);
            if (ch) {
                enqueue_ps2_key(KEY_NONE, ch);
            }
            return;
        }
    }
}

static bool ps2_wait_write(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((inb(0x64) & 0x02) == 0) {
            return true;
        }
    }
    return false;
}

static bool ps2_wait_read(void) {
    for (int i = 0; i < 100000; ++i) {
        if (inb(0x64) & 0x01) {
            return true;
        }
    }
    return false;
}

static void ps2_write_mouse(uint8_t value) {
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x64, 0xD4);
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x60, value);
}

/*
 * GRUB and the firmware normally leave the keyboard enabled, which hides a
 * missing setup step in virtual machines.  Real USB-legacy implementations
 * are less forgiving: they often inject the keyboard as PS/2 port 1 only
 * after the OS has enabled IRQ1 and the first port clock.  Preserve the
 * firmware's other policy bits, but explicitly request IRQ1, translation to
 * the Set-1 stream this driver decodes, and an enabled first port.
 *
 * There is deliberately no attempt to command a keyboard here.  A native USB
 * keyboard may be emulated by SMM rather than be a physical PS/2 device; its
 * command responses are not reliable, whereas the controller configuration
 * is the compatibility boundary common to both kinds of firmware emulation.
 */
static void ps2_discard_pending_output(void) {
    /* A stale byte left by GRUB/firmware can otherwise be mistaken for the
     * 8042 command-byte reply. Bound the drain in case a broken controller
     * continuously reports output-buffer-full. */
    for (uint32_t i = 0; i < 64u; ++i) {
        uint8_t status = inb(0x64);
        if ((status & 0x01u) == 0) {
            break;
        }
        (void)inb(0x60);
    }
}

static bool ps2_write_config(uint8_t config) {
    if (!ps2_wait_write()) {
        return false;
    }
    outb(0x64, 0x60); /* write controller configuration byte */
    if (!ps2_wait_write()) {
        return false;
    }
    outb(0x60, config);
    return ps2_wait_write();
}

static bool ps2_read_config(uint8_t *config) {
    if (config == NULL || !ps2_wait_write()) {
        return false;
    }
    ps2_discard_pending_output();
    outb(0x64, 0x20); /* read controller configuration byte */
    if (!ps2_wait_read()) {
        return false;
    }
    *config = inb(0x60);
    return true;
}

/*
 * Initialize the legacy 8042 interface conservatively, without sending PS/2
 * device commands (USB-legacy/SMM keyboard emulation does not reliably answer
 * them).
 *
 * The firmware (and the old 1.1 code) leaves the keyboard clock and IRQ1
 * alone.  Some virtual 8042s - VMware's in particular - stop delivering
 * keyboard interrupts after an 0xAD/0xAE port-disable/enable cycle even
 * though the command byte reads back as valid.  Use a single
 * read-modify-write instead: enable both port clocks, IRQ1, IRQ12 and Set-1
 * translation, then verify.  This is the smallest change from the working
 * firmware configuration.
 *
 * The kernel decodes Set-1 scan codes, so controller translation must be on.
 * IRQ1 must be enabled in both the controller command byte and the PIC mask;
 * init_interrupts() unmasks IRQ1 before this function is called.
 */
static void init_keyboard_controller(void) {
    uint8_t config;
    uint8_t verify;

    if (!ps2_wait_write()) {
        serial_trace("WARNING", "input: 8042 controller not ready for keyboard setup (IBF timeout)");
        ps2_controller_config_valid = false;
        boot_menu_dirty = true;
        return;
    }

    /* Enable the auxiliary port before the command byte is written; the
     * mouse setup below only sends device commands. */
    outb(0x64, 0xA8);
    if (!ps2_wait_write()) {
        serial_trace("WARNING", "input: 8042 timeout enabling auxiliary port");
        ps2_controller_config_valid = false;
        boot_menu_dirty = true;
        return;
    }

    ps2_discard_pending_output();
    if (!ps2_read_config(&config)) {
        serial_trace("WARNING", "input: 8042 did not return its configuration byte");
        ps2_controller_config_valid = false;
        boot_menu_dirty = true;
        return;
    }
    /* One write: IRQ1 + IRQ12 + Set-1 translation, both clocks enabled.
     * Preserve unrelated firmware-owned command bits. */
    config |= 0x43u;
    config &= (uint8_t)~0x30u;
    if (!ps2_write_config(config)) {
        serial_trace("WARNING", "input: 8042 timed out writing keyboard configuration");
        ps2_controller_config_valid = false;
        boot_menu_dirty = true;
        return;
    }

    if (!ps2_read_config(&verify)) {
        serial_trace("WARNING", "input: 8042 config write could not be verified");
        ps2_controller_config_valid = false;
        boot_menu_dirty = true;
        return;
    }

    ps2_controller_config = verify;
    ps2_controller_config_valid = (verify & 0x51u) == 0x41u;
    if (!ps2_controller_config_valid) {
        serial_trace_hex_value("WARNING", "input: 8042 config verify mismatch", verify);
    } else {
        serial_trace_hex_value("INFO", "input: 8042 keyboard config verified", verify);
        serial_trace("INFO", "input: IRQ1 keyboard handler armed (USB legacy if firmware provides it)");
    }
    /* The first frame is drawn before controller setup. Refresh the visible
     * diagnostics now, otherwise it can permanently claim "not ready". */
    boot_menu_dirty = true;
}

static void init_mouse(void) {
    /* The command byte was already written by init_keyboard_controller:
     * IRQ12 and the auxiliary clock are enabled there.  Only send the
     * device commands here; re-writing the command byte would add another
     * reconfiguration step on virtual 8042s that are sensitive to it. */
    if (!ps2_wait_write()) {
        boot_menu_dirty = true;
        return;
    }
    outb(0x64, 0xA8); /* enable the auxiliary port (idempotent) */
    if (!ps2_wait_write()) {
        boot_menu_dirty = true;
        return;
    }

    ps2_write_mouse(0xF6);
    if (ps2_wait_read() && inb(0x60) == 0xFA) {
        ps2_mouse_enabled = true;
    }
    ps2_write_mouse(0xF4);
    if (ps2_wait_read() && inb(0x60) == 0xFA) {
        ps2_mouse_enabled = true;
    }
    boot_menu_dirty = true;
}

/* The verified 8042 IRQ1 configuration is the only legacy keyboard path
 * this kernel can rely on.  A USB keyboard routed through firmware SMM may
 * still deliver bytes here, in which case the native xHCI driver must stay
 * out of the way. */
static bool ps2_keyboard_usable(void) {
    return ps2_controller_config_valid;
}

/* A PS/2 mouse is usable when it acknowledged the enable command, or
 * when at least one byte has already arrived from the auxiliary port. */
static bool ps2_mouse_usable(void) {
    return ps2_mouse_enabled || mouse_byte_count > 0u;
}

/*
 * Firmware keyboard fallback.
 *
 * A USB keyboard can reach the machine through two different legacy paths:
 *
 *   - the 8042/SMM "legacy USB" path, which puts Set-1 scancodes in the
 *     controller's output buffer (what init_keyboard_controller arms), or
 *   - the BIOS's own keyboard buffer, readable with INT 16h, when the
 *     firmware does the translation but never injects into the 8042.
 *
 * The second path is invisible to port 0x60 and is common on boards whose
 * USB 3.0 ports are handled by firmware code that is not an 8042 emulation.
 * It is used until a decoded PS/2 key event proves that the legacy path is
 * delivering usable input, so an empty/spurious IRQ cannot disable fallback.
 */
#ifndef HALOXOS_FORCE_BIOS_KEYBOARD
#define HALOXOS_FORCE_BIOS_KEYBOARD 0
#endif

static void poll_bios_keyboard_fallback(void) {
    static uint32_t settle_frames;
    static uint8_t poll_divider;
    uint8_t ascii = 0;
    uint8_t scan = 0;

    /* The native xHCI path has already claimed the keyboard endpoint; the
     * firmware buffer is not a second source of the same keypress. */
    if (xhci_hid_input_ready()) {
        return;
    }

#if !HALOXOS_FORCE_BIOS_KEYBOARD
    /* Only a decoded PS/2 key event disables this fallback. An empty IRQ1,
     * a stale ACK/BAT byte, or unrelated controller output is not evidence
     * that this machine's keyboard is reaching port 0x60. The previous IRQ
     * counter gate could permanently disable BIOS input after one spurious
     * interrupt, which is especially harmful on firmware USB-legacy paths. */
    if (ps2_keyboard_event_seen) {
        return;
    }
#endif
    /* Counted in frames, not timer ticks: a machine whose PIT never ticks
     * must still be able to fall back (timer_ticks would stay at zero). */
    if (settle_frames < 30u) {
        ++settle_frames;
        return; /* let the controller finish its own setup first */
    }
    /* The protected/real-mode bridge is much more expensive than a port
     * read.  Poll at 15 Hz (four 60 Hz frames) rather than switching CPU modes
     * on every frame when no legacy keyboard path exists. */
    if (++poll_divider < 4u) {
        return;
    }
    poll_divider = 0;
    if (!serial_bootlog_bios_key_poll(&ascii, &scan)) {
        return;
    }

    if (ascii == '\r' || ascii == '\n') {
        enqueue_key(KEY_ENTER, '\n');
    } else if (ascii == 0x1B) {
        enqueue_key(KEY_ESC, 0);
    } else if (ascii == 8) {
        enqueue_key(KEY_BACKSPACE, 0);
    } else if (ascii == '\t') {
        enqueue_key(KEY_TAB, 0);
    } else if (ascii >= 32 && ascii < 127) {
        enqueue_key(KEY_NONE, (char)ascii);
    } else {
        switch (scan) {
            case 0x48: enqueue_key(KEY_UP, 0); break;
            case 0x50: enqueue_key(KEY_DOWN, 0); break;
            case 0x4B: enqueue_key(KEY_LEFT, 0); break;
            case 0x4D: enqueue_key(KEY_RIGHT, 0); break;
            case 0x47: enqueue_key(KEY_HOME, 0); break;
            case 0x4F: enqueue_key(KEY_END, 0); break;
            case 0x53: enqueue_key(KEY_DEL, 0); break;
            default: return;
        }
    }
    if (system_state == STATE_BOOT_MENU) {
        boot_menu_dirty = true;
    }
}

/*
 * Real-time mouse input.
 *
 * Packets are consumed by IRQ12 the instant they arrive (cursor moves
 * with interrupt latency, ~1ms, instead of waiting for the next 60Hz
 * frame poll - the old path batch-applied ~120ms of movement on heavy
 * emulator frames and the cursor teleported / felt desynced).
 *
 * Split state model:
 *  - mouse.x / mouse.y / mouse.middle: written live by the ISR (real
 *    time position, handlers read them mid-frame).
 *  - mouse.left / mouse.right / prev_*: shaped once per frame in
 *    poll_input from the physical hw state. Buttons are frame-granular
 *    so each physical press produces exactly one "clicked" edge, even
 *    when a press+release completes inside a single slow frame.
 *  - press latch counters: the ISR counts release->press edges; poll
 *    replays one pending edge per frame. Without this, a fast click
 *    that fully lands between two frames would be lost.
 */
static bool mouse_hw_left = false;
static bool mouse_hw_right = false;
static bool mouse_hw_middle = false;
static uint8_t mouse_press_latch_left = 0;
static uint8_t mouse_press_latch_right = 0;
static uint8_t mouse_press_latch_middle = 0;
static bool mouse_click_replay_left = false;
static bool mouse_click_replay_right = false;
static bool mouse_click_replay_middle = false;

/* Apply one decoded relative movement/button sample.  Both the PS/2
 * packet parser and the native xHCI mouse decoder feed this, so
 * cursor clamping, click latches and the desktop dirty gate stay in one
 * place.  dy follows the PS/2 convention: positive is up. */
static void mouse_apply_state(int dx, int dy, bool new_left,
                              bool new_right, bool new_middle) {
    int nx = mouse.x + dx;
    int ny = mouse.y - dy;

    if (nx < 0) nx = 0;
    else if (nx > OS_WIDTH - 1) nx = OS_WIDTH - 1;
    if (ny < 0) ny = 0;
    else if (ny > OS_HEIGHT - 1) ny = OS_HEIGHT - 1;

    if (nx != mouse.x || ny != mouse.y || new_left != mouse_hw_left ||
        new_right != mouse_hw_right || new_middle != mouse.middle) {
        last_input_tick = timer_ticks;
        /* This can run in the IRQ12 handler, at any point in the frame -
         * including while one is being painted. Setting a flag (rather
         * than comparing tick stamps later) cannot lose the event: the
         * gate only clears it once a frame has actually been drawn. */
        ui_input_dirty = true;
    }

    if (new_left && !mouse_hw_left && mouse_press_latch_left < 255) {
        ++mouse_press_latch_left;
    }
    if (new_right && !mouse_hw_right && mouse_press_latch_right < 255) {
        ++mouse_press_latch_right;
    }
    if (new_middle && !mouse_hw_middle && mouse_press_latch_middle < 255) {
        ++mouse_press_latch_middle;
    }
    mouse_hw_left = new_left;
    mouse_hw_right = new_right;
    mouse_hw_middle = new_middle;

    mouse.x = nx;
    mouse.y = ny;
    mouse.middle = new_middle;
}

static void mouse_apply_byte(uint8_t data) {
    /* Byte 0 must have bit 3 set; else the stream desynced - resync. */
    if (mouse_packet_index == 0 && (data & 0x08) == 0) {
        return;
    }

    mouse_packet[mouse_packet_index++] = data;
    if (mouse_packet_index != 3) {
        return;
    }
    mouse_packet_index = 0;

    {
        int dx = (mouse_packet[0] & 0x10) ? (int)mouse_packet[1] - 256 : (int)mouse_packet[1];
        int dy = (mouse_packet[0] & 0x20) ? (int)mouse_packet[2] - 256 : (int)mouse_packet[2];

        mouse_apply_state(dx, dy,
                          (mouse_packet[0] & 0x01) != 0,
                          (mouse_packet[0] & 0x02) != 0,
                          (mouse_packet[0] & 0x04) != 0);
    }
}

/* Native xHCI HID mouse report.  Two layouts are accepted:
 *
 *   Boot protocol (3 or 4 bytes, no report ID):
 *     [buttons][dx int8][dy int8][wheel int8?]
 *
 *   Report protocol used by the known 30FA:1040 gaming mouse (7 bytes):
 *     [report ID 1][buttons:5 + pad:3][dx int16 LE][dy int16 LE][wheel int8]
 *
 * The firmware's CSM may leave the device in either protocol.  HID Y grows
 * downward while the shared state follows PS/2's upward-positive convention;
 * the optional wheel byte is not used by the desktop yet. */
void mouse_apply_hid_report(const uint8_t *report, uint32_t length) {
    static uint32_t trace_count;
    uint8_t buttons;
    int dx;
    int dy;

    if (report == NULL || length < 3u) {
        return;
    }

    if (length >= 7u && report[0] == 1u) {
        int16_t raw_dx = (int16_t)((uint16_t)report[2] |
                                   ((uint16_t)report[3] << 8));
        int16_t raw_dy = (int16_t)((uint16_t)report[4] |
                                   ((uint16_t)report[5] << 8));

        buttons = report[1] & 0x07u;
        dx = (int)raw_dx;
        dy = -(int)raw_dy;
        if (trace_count < 8u) {
            ++trace_count;
            serial_trace_hex_value("INFO", "input: xHCI mouse report proto",
                                   ((uint32_t)report[0] << 24) |
                                   ((uint32_t)report[1] << 16) |
                                   ((uint32_t)report[2] << 8) |
                                   (uint32_t)report[3]);
            serial_trace_hex_value("INFO", "input: xHCI mouse delta",
                                   ((uint32_t)(uint16_t)dx << 16) |
                                   (uint16_t)dy);
        }
        mouse_apply_state(dx, dy,
                          (buttons & 0x01u) != 0,
                          (buttons & 0x02u) != 0,
                          (buttons & 0x04u) != 0);
        return;
    }

    buttons = report[0] & 0x07u;
    dx = (int)(int8_t)report[1];
    dy = (int)(int8_t)report[2];
    if (dy != 0) {
        dy = -dy;
    }
    if (trace_count < 8u) {
        ++trace_count;
        serial_trace_hex_value("INFO", "input: xHCI mouse report",
                               ((uint32_t)buttons << 16) |
                               ((uint32_t)report[1] << 8) |
                               (uint32_t)report[2]);
    }
    mouse_apply_state(dx, dy,
                      (buttons & 0x01u) != 0,
                      (buttons & 0x02u) != 0,
                      (buttons & 0x04u) != 0);
}

/* IRQ12 (PS/2 mouse): drain everything the 8042 queued while the CPU was
 * busy. This is what kills the ~120ms batch-apply lag on heavy emulators.
 * Stray keyboard bytes share the same output queue and MUST be routed to
 * the scancode handler here, otherwise a mouse burst would eat them. */
static void ps2_drain_output(void) {
    /* A missing or wedged 8042 can leave the output-buffer-full bit stuck
     * (a floating bus reads 0xFF).  An unbounded drain would then spin in
     * the main loop forever, which is exactly what happens on a USB-only
     * board whose firmware never brought the 8042 up.  Bound the burst and
     * drop bytes flagged as parity/timeout errors. */
    for (uint32_t guard = 0; guard < 32u; ++guard) {
        uint8_t status = inb(0x64);
        if ((status & 0x01) == 0) {
            return;
        }
        uint8_t data = inb(0x60);
        if ((status & 0xC0) != 0) {
            continue; /* parity/timeout: no valid byte behind it */
        }
        if (status & 0x20) {
            ++mouse_byte_count;
            mouse_apply_byte(data);
        } else {
            ++keyboard_byte_count;
            ps2_hardware_key_seen = true;
            if (system_state == STATE_BOOT_MENU) {
                /* The debug counter is also a live diagnostic: repaint the
                 * menu even for bytes that do not decode to a usable key. */
                boot_menu_dirty = true;
            }
            handle_scancode(data);
        }
    }
}

/* The controller's output buffer is shared.  Either IRQ may be the first
 * one delivered by a real chipset/SMM USB-legacy implementation, so both
 * entry points drain and classify every pending byte instead of assuming
 * IRQ1 means keyboard and IRQ12 means mouse. */
void keyboard_packet_from_isr(void) {
    ++keyboard_irq_count;
    if (system_state == STATE_BOOT_MENU) {
        boot_menu_dirty = true;
    }
    ps2_drain_output();
}

void mouse_packet_from_isr(void) {
    ++mouse_irq_count;
    ps2_drain_output();
}

static const char *ps2_input_status_text(void) {
    static char status_line[96];
    size_t len = 0;
    uint8_t pic_mask = inb(0x21);
    uint8_t controller_status = inb(0x64);

    status_line[0] = '\0';
    append_string(status_line, &len, sizeof(status_line), "INPUT 8042 ");
    if (ps2_controller_config_valid) {
        append_string(status_line, &len, sizeof(status_line), "cfg 0x");
        append_hex8(status_line, &len, sizeof(status_line), ps2_controller_config);
    } else {
        append_string(status_line, &len, sizeof(status_line), "not ready");
    }
    append_string(status_line, &len, sizeof(status_line), " IRQ1 ");
    append_uint(status_line, &len, sizeof(status_line), keyboard_irq_count);
    append_string(status_line, &len, sizeof(status_line), " key ");
    append_uint(status_line, &len, sizeof(status_line), keyboard_byte_count);
    append_string(status_line, &len, sizeof(status_line), " bios ");
    append_uint(status_line, &len, sizeof(status_line), serial_bootlog_bios_key_count());
    append_string(status_line, &len, sizeof(status_line), " IRQ12 ");
    append_uint(status_line, &len, sizeof(status_line), mouse_irq_count);
    append_string(status_line, &len, sizeof(status_line), " PIC 0x");
    append_hex8(status_line, &len, sizeof(status_line), pic_mask);
    append_string(status_line, &len, sizeof(status_line), " KBC 0x");
    append_hex8(status_line, &len, sizeof(status_line), controller_status);
    return status_line;
}

static void poll_input(void) {
    /* Snapshot the state the previous frame was shaped from, so the end of
     * this function can tell whether anything actually changed. Comparing
     * against the previous frame - rather than stamping timer_ticks - is
     * what makes a second edge inside one timer tick visible. */
    bool was_left = mouse.left;
    bool was_right = mouse.right;
    bool was_middle = mouse.middle;
    int was_x = mouse.x;
    int was_y = mouse.y;

    /* End a replayed one-frame click hold: restore physical state.
     * This is a visible button-state change (pressed -> released fill,
     * cursor press offset), so it must dirty the desktop exactly like a
     * real button edge does - otherwise a fast click that lands entirely
     * inside one frame leaves the pressed look on screen until the next
     * unrelated input. */
    if (mouse_click_replay_left || mouse_click_replay_right || mouse_click_replay_middle) {
        last_input_tick = timer_ticks;
        ui_input_dirty = true;
    }
    if (mouse_click_replay_left) {
        mouse.left = mouse_hw_left;
        mouse_click_replay_left = false;
    }
    if (mouse_click_replay_right) {
        mouse.right = mouse_hw_right;
        mouse_click_replay_right = false;
    }
    if (mouse_click_replay_middle) {
        mouse.middle = mouse_hw_middle;
        mouse_click_replay_middle = false;
    }

    mouse.prev_left = mouse.left;
    mouse.prev_right = mouse.right;
    mouse.prev_middle = mouse.middle;

    mouse.left = mouse_hw_left;
    mouse.right = mouse_hw_right;
    mouse.middle = mouse_hw_middle;

    /* Deliver one pending press edge per frame. When the physical button
     * is already held, just expose the edge (prev=false). When the click
     * completed entirely inside the last frame, hold the button for this
     * single frame so click handlers see it exactly once. */
    if (mouse_press_latch_left != 0) {
        --mouse_press_latch_left;
        mouse.prev_left = false;
        if (!mouse.left) {
            mouse.left = true;
            mouse_click_replay_left = true;
        }
    }
    if (mouse_press_latch_right != 0) {
        --mouse_press_latch_right;
        mouse.prev_right = false;
        if (!mouse.right) {
            mouse.right = true;
            mouse_click_replay_right = true;
        }
    }
    if (mouse_press_latch_middle != 0) {
        --mouse_press_latch_middle;
        mouse.prev_middle = false;
        if (!mouse.middle) {
            mouse.middle = true;
            mouse_click_replay_middle = true;
        }
    }

    /* 8042 drain: keyboard bytes always, mouse bytes as a fallback for
     * boxes where IRQ12 never fires. Runs with IRQs off so the ISR and
     * this loop can never interleave a packet byte pair.  Once the native
     * xHCI HID path is live the 8042 is known-unusable (that is the gate
     * that started the driver), and its floating status port must not be
     * polled; IRQ1/IRQ12 still drain on boards whose 8042 really fires. */
    if (!xhci_hid_input_ready()) {
        __asm__ volatile ("cli");
        ps2_drain_output();
        __asm__ volatile ("sti");
    }

    /* Native xHCI HID keyboard: one poll per frame.  This is the only
     * input path on a machine whose firmware left no usable 8042 or
     * INT 16h keyboard behind. */
    xhci_hid_poll();

    /* Opt-in only: INT 16h AH=00h blocks until a key arrives, and a
     * firmware that misreports "key available" freezes the machine here.
     * See the note above HALOXOS_DISABLE_BIOS_KEYBOARD_FALLBACK. */
#if !HALOXOS_DISABLE_BIOS_KEYBOARD_FALLBACK
    poll_bios_keyboard_fallback();
#endif

    /* One notification per real change, covering motion (the ISR and the
     * drain both write mouse.x/mouse.y), button edges, and the replayed
     * click hold above. The frame loop's gate reads ui_input_dirty, which
     * is only cleared once a frame has actually been presented, so no edge
     * can be lost between two ticks. */
    if (mouse.left != was_left || mouse.right != was_right || mouse.middle != was_middle) {
        ui_pointer_state_changed = true;
    }
    if (mouse.left != was_left || mouse.right != was_right || mouse.middle != was_middle ||
        mouse.x != was_x || mouse.y != was_y) {
        last_input_tick = timer_ticks;
        ui_input_dirty = true;
    }
}

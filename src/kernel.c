/*
    HaloxOS - Version 1.0 Dev!
    Copyright Svh03ra (C) 2026, All rights reserved.
    Source File: kernel.c, main core.
    Build: 22th April 2026

    Made in AI used: GPT-5.4 for Visual Code Editor at Codex.
*/

/*  This repository is licensed under the GNU General Public License.
        --------------------------------------------------------------
    Free to use, modify, or create your own fork,
    provided that you agree to and comply with the terms of the license. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "build_info.h"

// Backgrounds
extern const uint8_t _binary_build_login_bin_start[];
extern const uint8_t _binary_build_desktop_bin_start[];
extern const uint8_t _binary_build_user_frame_bin_start[];
extern const uint8_t _binary_build_notepad_icon_bin_start[];
extern const uint8_t _binary_build_terminal_icon_bin_start[];
extern const uint8_t _binary_build_game_icon_bin_start[];
extern const uint8_t _binary_build_program_icon_bin_start[];
extern const uint8_t _binary_build_settings_icon_bin_start[];

#define OS_WIDTH 640
#define OS_HEIGHT 480
#define MAX_OUTPUT_WIDTH 1280
#define MAX_OUTPUT_HEIGHT 720
#define TASKBAR_H 28
#define DESKTOP_ICON_COUNT 2
#define MAX_TEXT 4096
#define TERM_MAX_LINES 32
#define TERM_LINE_LEN 72
#define APP_COUNT 11
#define SNAKE_MAX_SEGMENTS 128
#define MINES_SIZE 8
#define MINES_COUNT 10
#define TIMER_HZ 60
#define TERMINAL_CURSOR_BLINK_TICKS 30
#define SNAKE_STEP_TICKS 9
#define PERF_UPDATE_TICKS 6
#define PAINT_CANVAS_W 304
#define PAINT_CANVAS_H 172
#define GPU_GRID_COLS 16
#define GPU_GRID_ROWS 16
#define GPU_GRID_PAGE_ENTRIES (GPU_GRID_COLS * GPU_GRID_ROWS)
#define VGA_TEXT_COLS 80
#define VGA_TEXT_ROWS 25
#define VGA_TEXT_ATTR_GRAY 0x07
#define VGA_TEXT_ATTR_BLUE 0x09
#define DESKTOP_LAYOUT_MAGIC 0x484C5850u
#define DESKTOP_LAYOUT_LBA 1536u

typedef enum {
    STATE_BOOT_MENU,
    STATE_BOOT_TERMINAL,
    STATE_LOGIN,
    STATE_DESKTOP,
    STATE_SHUTDOWN
} SystemState;

typedef enum {
    KEY_NONE,
    KEY_ENTER,
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_F4,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT
} KeyCode;

typedef struct {
    KeyCode code;
    char ch;
} KeyEvent;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} __attribute__((packed)) MultibootInfo;

typedef struct {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed)) MultibootMmapEntry;

typedef struct {
    int x;
    int y;
} DesktopIconPosition;

typedef struct {
    uint32_t magic;
    uint32_t checksum;
    int32_t icon_x[DESKTOP_ICON_COUNT];
    int32_t icon_y[DESKTOP_ICON_COUNT];
} __attribute__((packed)) DesktopLayoutSector;

typedef struct {
    uint8_t *address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
} Framebuffer;

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed)) IdtEntry;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) IdtPointer;

typedef struct {
    int x;
    int y;
    bool left;
    bool right;
    bool middle;
    bool prev_left;
    bool prev_right;
    bool prev_middle;
} MouseState;

typedef struct {
    bool open;
    int x;
    int y;
    int w;
    int h;
    const char *title;
} Window;

typedef enum {
    APP_NOTEPAD,
    APP_CMD,
    APP_PAINT,
    APP_EXPLORER,
    APP_SNAKE,
    APP_GUESS,
    APP_MINES,
    APP_GAME_CENTER,
    APP_POWER,
    APP_SETTINGS,
    APP_TASK_MANAGER
} AppId;

typedef struct {
    char lines[TERM_MAX_LINES][TERM_LINE_LEN];
    int line_count;
    char input[TERM_LINE_LEN];
    int input_len;
} Terminal;

typedef struct {
    uint8_t palette_mode;
    uint8_t resolution_mode;
    bool widescreen;
    uint8_t background_mode;
} SettingsState;

typedef enum {
    VIDEO_BACKEND_NONE,
    VIDEO_BACKEND_MULTIBOOT,
    VIDEO_BACKEND_BGA,
    VIDEO_BACKEND_VMWARE_SVGA
} VideoBackend;

typedef struct {
    uint16_t io_base;
    uint8_t port_stride;
    uint32_t fb_start;
    uint32_t fb_offset;
    uint32_t fb_size;
    uint32_t mem_start;
    uint32_t mem_size;
    uint32_t fifo_num_regs;
    uint32_t caps;
    uint32_t host_bpp;
    uint32_t max_width;
    uint32_t max_height;
    volatile uint32_t *fifo;
    bool fifo_ready;
} VmwareSvgaState;

static const char *app_titles[APP_COUNT] = {
    "Notepad",
    "Prompt",
    "Paint",
    "Explorer",
    "Snake",
    "Guess Num.",
    "Minesw...",
    "Games",
    "Power",
    "Settings",
    "TaskMgr"
};

static const uint8_t font8x8_basic[96][8] = {
    {0,0,0,0,0,0,0,0},{24,60,60,24,24,0,24,0},{54,54,20,0,0,0,0,0},{54,54,127,54,127,54,54,0},
    {24,62,3,30,48,31,24,0},{0,99,102,12,24,51,99,0},{28,54,28,59,102,102,59,0},{6,6,12,0,0,0,0,0},
    {12,6,3,3,3,6,12,0},{6,12,24,24,24,12,6,0},{0,102,60,255,60,102,0,0},{0,12,12,63,12,12,0,0},
    {0,0,0,0,0,12,12,24},{0,0,0,63,0,0,0,0},{0,0,0,0,0,24,24,0},{96,48,24,12,6,3,1,0},
    {62,99,115,123,111,103,62,0},{12,14,15,12,12,12,63,0},{30,51,48,28,6,51,63,0},{30,51,48,28,48,51,30,0},
    {56,60,54,51,127,48,120,0},{63,3,31,48,48,51,30,0},{28,6,3,31,51,51,30,0},{63,51,48,24,12,12,12,0},
    {30,51,51,30,51,51,30,0},{30,51,51,62,48,24,14,0},{0,24,24,0,0,24,24,0},{0,12,12,0,0,12,12,24},
    {24,12,6,3,6,12,24,0},{0,0,63,0,63,0,0,0},{3,6,12,24,12,6,3,0},{30,51,48,24,12,0,12,0},
    {62,99,123,123,123,3,30,0},{12,30,51,51,63,51,51,0},{31,54,54,30,54,54,31,0},{60,102,3,3,3,102,60,0},
    {31,54,102,102,102,54,31,0},{127,70,22,30,22,70,127,0},{127,70,22,30,22,6,15,0},{60,102,3,3,115,102,124,0},
    {51,51,51,63,51,51,51,0},{30,12,12,12,12,12,30,0},{120,48,48,48,51,51,30,0},{103,102,54,30,54,102,103,0},
    {15,6,6,6,70,102,127,0},{99,119,127,107,99,99,99,0},{99,103,111,123,115,99,99,0},{28,54,99,99,99,54,28,0},
    {31,54,54,30,6,6,15,0},{30,51,51,51,59,30,56,0},{31,54,54,30,54,102,103,0},{30,51,7,14,56,51,30,0},
    {63,45,12,12,12,12,30,0},{51,51,51,51,51,51,63,0},{51,51,51,51,51,30,12,0},{99,99,99,107,127,119,99,0},
    {99,99,54,28,54,99,99,0},{51,51,51,30,12,12,30,0},{127,99,49,24,76,102,127,0},{30,6,6,6,6,6,30,0},
    {3,6,12,24,48,96,64,0},{30,24,24,24,24,24,30,0},{8,28,54,99,0,0,0,0},{0,0,0,0,0,0,0,255},
    {12,12,24,0,0,0,0,0},{0,0,30,48,62,51,110,0},{7,6,6,30,54,54,27,0},{0,0,30,51,3,51,30,0},
    {56,48,48,60,54,54,108,0},{0,0,30,51,63,3,30,0},{28,54,6,15,6,6,15,0},{0,0,108,54,54,60,48,31},
    {7,6,54,110,102,102,103,0},{12,0,14,12,12,12,30,0},{48,0,56,48,48,54,54,28},{7,6,102,54,30,54,103,0},
    {14,12,12,12,12,12,30,0},{0,0,51,127,107,99,99,0},{0,0,31,51,51,51,51,0},{0,0,30,51,51,51,30,0},
    {0,0,27,54,54,30,6,15},{0,0,108,54,54,60,48,120},{0,0,27,54,6,6,15,0},{0,0,62,3,30,48,31,0},
    {8,12,62,12,12,44,24,0},{0,0,51,51,51,51,110,0},{0,0,51,51,51,30,12,0},{0,0,99,99,107,127,54,0},
    {0,0,99,54,28,54,99,0},{0,0,51,51,51,62,48,31},{0,0,63,25,12,38,63,0},{56,12,12,7,12,12,56,0},
    {24,24,24,0,24,24,24,0},{7,12,12,56,12,12,7,0},{110,59,0,0,0,0,0,0},{0,8,28,54,99,99,127,0}
};

static Framebuffer fb;
static SystemState system_state = STATE_BOOT_MENU;
static MouseState mouse = {320, 240, false, false, false, false, false, false};
static Window windows[APP_COUNT];
static uint8_t backbuffer[OS_WIDTH * OS_HEIGHT];
static Color palette[256];
static IdtEntry idt[256];
static KeyEvent key_queue[64];
static int key_head = 0;
static int key_tail = 0;
static uint8_t mouse_packet[3];
static int mouse_packet_index = 0;
static bool keyboard_extended = false;
static bool keyboard_shift = false;
static bool keyboard_alt = false;
static bool menu_open = false;
static bool context_menu_open = false;
static int context_menu_x = 0;
static int context_menu_y = 0;
static int active_window = -1;
static int drag_window = -1;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static bool cpu_halted_overlay = false;
static bool shutdown_pending = false;
static uint32_t random_state = 1;
static volatile uint32_t timer_ticks = 0;
static uint32_t last_input_tick = 0;
static int explorer_selected = 0;
static VideoBackend video_backend = VIDEO_BACKEND_NONE;
static VmwareSvgaState vmware_svga;
static char boot_status_text[80];
static bool boot_menu_dirty = true;
static bool boot_terminal_dirty = true;
static uint32_t boot_terminal_last_blink = 0xFFFFFFFFu;

static uint8_t color_black;
static uint8_t color_white;
static uint8_t color_gray_dark;
static uint8_t color_gray;
static uint8_t color_gray_light;
static uint8_t color_green;
static uint8_t color_green_dark;
static uint8_t color_blue;
static uint8_t color_blue_dark;
static uint8_t color_red;
static uint8_t color_yellow;
static uint8_t color_orange;
static uint8_t color_pink;
static uint8_t color_desktop_icon;

static char notepad_text[MAX_TEXT];
static size_t notepad_len = 0;

static Terminal boot_term;
static Terminal cmd_term;

static uint8_t paint_canvas[PAINT_CANVAS_W * PAINT_CANVAS_H];
static uint8_t paint_color = 1;
static uint8_t paint_brush_size = 1;

static int snake_x[SNAKE_MAX_SEGMENTS];
static int snake_y[SNAKE_MAX_SEGMENTS];
static int snake_length = 0;
static int snake_dir = 1;
static int snake_next_dir = 1;
static int snake_food_x = 0;
static int snake_food_y = 0;
static uint32_t snake_last_step_tick = 0;
static uint32_t snake_score = 0;
static bool snake_dead = false;

static char guess_input[8];
static int guess_input_len = 0;
static int guess_target = 0;
static char guess_message[64] = "Guess a number from 1 to 100.";

static uint8_t mines_value[MINES_SIZE][MINES_SIZE];
static bool mines_revealed[MINES_SIZE][MINES_SIZE];
static bool mines_flagged[MINES_SIZE][MINES_SIZE];
static bool mines_lost = false;
static bool mines_won = false;

static SettingsState settings_applied = {0, 1, false, 0};
static SettingsState settings_pending = {0, 1, false, 0};
static uint8_t settings_tab = 0;
static uint8_t task_manager_tab = 0;
static int task_manager_selected_process = 0;
static bool task_manager_confirm_kill = false;
static int task_manager_kill_target = -1;
static uint32_t gpu_palette_scroll = 0;
static uint8_t cpu_usage_history[64];
static uint8_t gpu_usage_history[64];
static uint8_t ram_usage_history[64];
static uint8_t disk_usage_history[64];
static uint8_t cpu_usage_percent = 0;
static uint8_t gpu_usage_percent = 0;
static uint8_t ram_usage_percent = 0;
static uint8_t disk_usage_percent = 0;
static uint32_t cpu_speed_mhz = 0;
static uint32_t ram_total_bytes = 0;
static uint32_t ram_used_bytes = 0;
static uint32_t gpu_memory_used_bytes = 0;
static uint32_t gpu_memory_total_bytes = 0;
static uint32_t disk_io_megabytes = 0;
static uint32_t perf_history_index = 0;
static bool cpu_has_cpuid = false;
static bool cpu_has_tsc = false;
static bool boot_drive_valid = false;
static uint8_t boot_drive_number = 0;
static bool desktop_icon_persistence_enabled = false;
static DesktopIconPosition desktop_icons[DESKTOP_ICON_COUNT];
static int selected_desktop_icon = -1;
static int desktop_icon_press = -1;
static int drag_desktop_icon = -1;
static int desktop_icon_press_x = 0;
static int desktop_icon_press_y = 0;
static int desktop_icon_drag_offset_x = 0;
static int desktop_icon_drag_offset_y = 0;
static bool desktop_icon_drag_moved = false;
static bool desktop_icon_press_was_selected = false;
static bool video_mode_switch_available = false;
static bool boot_text_mode = true;
static volatile uint16_t *const vga_text_buffer = (volatile uint16_t *)(uintptr_t)0xB8000;
static uint16_t present_x_map[MAX_OUTPUT_WIDTH];
static uint16_t present_y_map[MAX_OUTPUT_HEIGHT];
static uint32_t last_desktop_redraw_input_tick = 0xFFFFFFFFu;
static uint32_t last_desktop_redraw_second = 0xFFFFFFFFu;
static uint32_t last_desktop_redraw_perf_phase = 0xFFFFFFFFu;
static uint32_t last_desktop_redraw_terminal_blink = 0xFFFFFFFFu;
static uint32_t last_desktop_redraw_snake_tick = 0xFFFFFFFFu;
static uint32_t last_performance_sample_phase = 0xFFFFFFFFu;
static bool task_manager_gpu_scroll_drag = false;
static int task_manager_gpu_scroll_drag_offset = 0;
static uint32_t perf_window_start_cycles = 0;
static uint32_t perf_busy_cycle_accum = 0;
static bool perf_window_ready = false;

extern void irq0_stub(void);
extern void irq_default_stub(void);
extern void isr_default_stub(void);
extern void idt_load(const IdtPointer *pointer);
extern void cpu_halt_once(void);

static void program_vga_palette(void);

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static bool ata_wait_ready(uint16_t status_port) {
    for (int i = 0; i < 100000; ++i) {
        uint8_t status = inb(status_port);
        if ((status & 0x80u) == 0) {
            return true;
        }
    }
    return false;
}

static bool ata_wait_drq(uint16_t status_port) {
    for (int i = 0; i < 100000; ++i) {
        uint8_t status = inb(status_port);
        if ((status & 0x01u) != 0) {
            return false;
        }
        if ((status & 0x08u) != 0 && (status & 0x80u) == 0) {
            return true;
        }
    }
    return false;
}

static bool ata_pio_read_sector(uint32_t lba, uint8_t *buffer) {
    const uint16_t io_base = 0x1F0;
    const uint16_t status_port = io_base + 7;

    if (!ata_wait_ready(status_port)) {
        return false;
    }

    outb(io_base + 6, (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    outb(io_base + 2, 1);
    outb(io_base + 3, (uint8_t)(lba & 0xFFu));
    outb(io_base + 4, (uint8_t)((lba >> 8) & 0xFFu));
    outb(io_base + 5, (uint8_t)((lba >> 16) & 0xFFu));
    outb(status_port, 0x20);

    if (!ata_wait_drq(status_port)) {
        return false;
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t word = inw(io_base);
        buffer[i * 2 + 0] = (uint8_t)(word & 0xFFu);
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }

    return true;
}

static bool ata_pio_write_sector(uint32_t lba, const uint8_t *buffer) {
    const uint16_t io_base = 0x1F0;
    const uint16_t status_port = io_base + 7;

    if (!ata_wait_ready(status_port)) {
        return false;
    }

    outb(io_base + 6, (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    outb(io_base + 2, 1);
    outb(io_base + 3, (uint8_t)(lba & 0xFFu));
    outb(io_base + 4, (uint8_t)((lba >> 8) & 0xFFu));
    outb(io_base + 5, (uint8_t)((lba >> 16) & 0xFFu));
    outb(status_port, 0x30);

    if (!ata_wait_drq(status_port)) {
        return false;
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t word = (uint16_t)buffer[i * 2 + 0] | ((uint16_t)buffer[i * 2 + 1] << 8);
        outw(io_base, word);
    }

    outb(status_port, 0xE7);
    return ata_wait_ready(status_port);
}

static bool cpuid_is_available(void) {
    uint32_t before;
    uint32_t after;

    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x00200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl\n\t"
        : "=&r"(before), "=&r"(after)
        :
        : "cc");

    return ((before ^ after) & 0x00200000u) != 0;
}

static void cpuid_query(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile (
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf));

    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

static uint64_t rdtsc_read(void) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       (offset & 0xFCu);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       (offset & 0xFCu);
    outl(0xCF8, address);
    outl(0xCFC, value);
}

static bool find_vga_framebuffer_bar(uint32_t *base_out) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t function = 0; function < 8; ++function) {
                uint32_t id = pci_config_read32((uint8_t)bus, slot, function, 0x00);
                uint32_t class_reg;
                uint32_t bar0;

                if (id == 0xFFFFFFFFu) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                class_reg = pci_config_read32((uint8_t)bus, slot, function, 0x08);
                if (((class_reg >> 24) & 0xFFu) != 0x03u || ((class_reg >> 16) & 0xFFu) != 0x00u) {
                    continue;
                }

                bar0 = pci_config_read32((uint8_t)bus, slot, function, 0x10);
                if ((bar0 & 0x1u) == 0 && (bar0 & ~0x0Fu) != 0) {
                    *base_out = bar0 & ~0x0Fu;
                    return true;
                }
            }
        }
    }
    return false;
}

void timer_tick_from_isr(void) {
    ++timer_ticks;
}

static void *memset_local(void *dest, int value, size_t len) {
    uint8_t *ptr = (uint8_t *)dest;
    for (size_t i = 0; i < len; ++i) {
        ptr[i] = (uint8_t)value;
    }
    return dest;
}

static void *memcpy_local(void *dest, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
    return dest;
}

static size_t strlen_local(const char *text) {
    size_t len = 0;
    while (text[len] != '\0') {
        ++len;
    }
    return len;
}

static bool streq(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return false;
        }
        ++i;
    }
    return a[i] == b[i];
}

static bool starts_with(const char *text, const char *prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return false;
        }
        ++i;
    }
    return true;
}

static int clampi(int value, int min, int max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void copy_string(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    if (max_len == 0) {
        return;
    }
    while (src[i] && i + 1 < max_len) {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = '\0';
}

static void append_char(char *dest, size_t *len, size_t max_len, char ch) {
    if (*len + 1 >= max_len) {
        return;
    }
    dest[*len] = ch;
    ++(*len);
    dest[*len] = '\0';
}

static void append_uint(char *dest, size_t *len, size_t max_len, uint32_t value) {
    char buffer[16];
    int pos = 0;

    if (value == 0) {
        append_char(dest, len, max_len, '0');
        return;
    }

    while (value > 0 && pos < 15) {
        buffer[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (pos > 0) {
        append_char(dest, len, max_len, buffer[--pos]);
    }
}

static void append_padded_uint(char *dest, size_t *len, size_t max_len, uint32_t value, uint32_t width) {
    char buffer[16];
    int pos = 0;

    if (value == 0) {
        buffer[pos++] = '0';
    } else {
        while (value > 0 && pos < 15) {
            buffer[pos++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }

    while ((uint32_t)pos < width) {
        append_char(dest, len, max_len, '0');
        --width;
    }

    while (pos > 0) {
        append_char(dest, len, max_len, buffer[--pos]);
    }
}

static void append_decimal_2(char *dest, size_t *len, size_t max_len, uint32_t whole, uint32_t frac) {
    append_uint(dest, len, max_len, whole);
    append_char(dest, len, max_len, '.');
    append_padded_uint(dest, len, max_len, frac, 2);
}

static void append_memory_amount(char *dest, size_t *len, size_t max_len, uint32_t bytes) {
    if (bytes >= 1024u * 1024u * 1024u) {
        uint32_t whole = bytes / (1024u * 1024u * 1024u);
        uint32_t frac = (bytes % (1024u * 1024u * 1024u)) / ((1024u * 1024u * 1024u) / 100u);
        append_decimal_2(dest, len, max_len, whole, frac);
        append_char(dest, len, max_len, ' ');
        append_char(dest, len, max_len, 'G');
        append_char(dest, len, max_len, 'B');
    } else {
        uint32_t whole = bytes / (1024u * 1024u);
        uint32_t frac = (bytes % (1024u * 1024u)) / ((1024u * 1024u) / 100u);
        append_decimal_2(dest, len, max_len, whole, frac);
        append_char(dest, len, max_len, ' ');
        append_char(dest, len, max_len, 'M');
        append_char(dest, len, max_len, 'B');
    }
}

static void format_single_memory_amount(char *buffer, size_t max_len, uint32_t bytes) {
    size_t len = 0;
    append_memory_amount(buffer, &len, max_len, bytes);
}

static void append_frequency_label(char *dest, size_t *len, size_t max_len, uint32_t mhz) {
    if (mhz >= 1000u) {
        append_decimal_2(dest, len, max_len, mhz / 1000u, (mhz % 1000u) / 10u);
        append_char(dest, len, max_len, ' ');
        append_char(dest, len, max_len, 'G');
        append_char(dest, len, max_len, 'H');
        append_char(dest, len, max_len, 'z');
    } else {
        append_uint(dest, len, max_len, mhz);
        append_char(dest, len, max_len, ' ');
        append_char(dest, len, max_len, 'M');
        append_char(dest, len, max_len, 'H');
        append_char(dest, len, max_len, 'z');
    }
}

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA 0x01CF
#define VBE_DISPI_INDEX_ID 0x0
#define VBE_DISPI_INDEX_XRES 0x1
#define VBE_DISPI_INDEX_YRES 0x2
#define VBE_DISPI_INDEX_BPP 0x3
#define VBE_DISPI_INDEX_ENABLE 0x4
#define VBE_DISPI_INDEX_BANK 0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH 0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET 0x8
#define VBE_DISPI_INDEX_Y_OFFSET 0x9
#define VBE_DISPI_ID0 0xB0C0
#define VBE_DISPI_ID5 0xB0C5
#define VBE_DISPI_DISABLED 0x00
#define VBE_DISPI_ENABLED 0x01
#define VBE_DISPI_LFB_ENABLED 0x40
#define VBE_DISPI_NOCLEARMEM 0x80

#define PCI_VENDOR_ID_VMWARE 0x15ADu
#define PCI_DEVICE_ID_VMWARE_SVGA 0x0710u
#define PCI_DEVICE_ID_VMWARE_SVGA2 0x0405u
#define PCI_DEVICE_ID_VMWARE_SVGA3 0x0406u
#define PCI_DEVICE_ID_VMWARE_SVGA_EFI 0x0407u
#define PCI_DEVICE_ID_VMWARE_SVGA4 0x0408u
#define PCI_DEVICE_ID_VMWARE_SVGA4_EFI 0x0409u
#define PCI_DEVICE_ID_VMWARE_SVGA4_RO 0x0410u
#define VMWARE_SVGA_LEGACY_BASE_PORT 0x4560

#define SVGA_MAGIC 0x900000UL
#define SVGA_MAKE_ID(ver) ((SVGA_MAGIC << 8) | (ver))
#define SVGA_ID_0 SVGA_MAKE_ID(0)
#define SVGA_ID_1 SVGA_MAKE_ID(1)
#define SVGA_ID_2 SVGA_MAKE_ID(2)

#define SVGA_INDEX_PORT 0
#define SVGA_VALUE_PORT 1

#define SVGA_REG_ID 0
#define SVGA_REG_ENABLE 1
#define SVGA_REG_WIDTH 2
#define SVGA_REG_HEIGHT 3
#define SVGA_REG_MAX_WIDTH 4
#define SVGA_REG_MAX_HEIGHT 5
#define SVGA_REG_DEPTH 6
#define SVGA_REG_BITS_PER_PIXEL 7
#define SVGA_REG_PSEUDOCOLOR 8
#define SVGA_REG_RED_MASK 9
#define SVGA_REG_GREEN_MASK 10
#define SVGA_REG_BLUE_MASK 11
#define SVGA_REG_BYTES_PER_LINE 12
#define SVGA_REG_FB_START 13
#define SVGA_REG_FB_OFFSET 14
#define SVGA_REG_VRAM_SIZE 15
#define SVGA_REG_FB_SIZE 16
#define SVGA_REG_CAPABILITIES 17
#define SVGA_REG_MEM_START 18
#define SVGA_REG_MEM_SIZE 19
#define SVGA_REG_CONFIG_DONE 20
#define SVGA_REG_SYNC 21
#define SVGA_REG_BUSY 22
#define SVGA_REG_GUEST_ID 23
#define SVGA_REG_HOST_BITS_PER_PIXEL 28
#define SVGA_REG_MEM_REGS 30

#define SVGA_REG_ENABLE_DISABLE 0
#define SVGA_REG_ENABLE_ENABLE 1

#define SVGA_CAP_8BIT_EMULATION 0x00000100u

#define SVGA_PALETTE_BASE 1024

#define SVGA_FIFO_MIN 0
#define SVGA_FIFO_MAX 1
#define SVGA_FIFO_NEXT_CMD 2
#define SVGA_FIFO_STOP 3

#define SVGA_CMD_UPDATE 1

#define VMWARE_GUEST_ID_OTHER 0x500A

static void build_system_palette(void) {
    int index = 0;
    const uint8_t cube[] = {0, 51, 102, 153, 204, 255};

    for (int r = 0; r < 6; ++r) {
        for (int g = 0; g < 6; ++g) {
            for (int b = 0; b < 6; ++b) {
                palette[index++] = (Color){cube[r], cube[g], cube[b]};
            }
        }
    }

    for (int i = 0; i < 40; ++i) {
        uint8_t shade = (uint8_t)((i * 255) / 39);
        palette[index++] = (Color){shade, shade, shade};
    }
}

static Color quantize_color_16(Color input) {
    static const Color ega16[16] = {
        {0, 0, 0},       {0, 0, 170},     {0, 170, 0},    {0, 170, 170},
        {170, 0, 0},     {170, 0, 170},   {170, 85, 0},   {170, 170, 170},
        {85, 85, 85},    {85, 85, 255},   {85, 255, 85},  {85, 255, 255},
        {255, 85, 85},   {255, 85, 255},  {255, 255, 85}, {255, 255, 255}
    };
    uint32_t best_distance = 0xFFFFFFFFu;
    Color best = ega16[0];

    for (int i = 0; i < 16; ++i) {
        int dr = (int)ega16[i].r - input.r;
        int dg = (int)ega16[i].g - input.g;
        int db = (int)ega16[i].b - input.b;
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = ega16[i];
        }
    }

    return best;
}

static void update_present_maps(void) {
    if (fb.width == 0 || fb.height == 0) {
        return;
    }

    for (uint32_t x = 0; x < fb.width && x < MAX_OUTPUT_WIDTH; ++x) {
        uint32_t source = (x * OS_WIDTH) / fb.width;
        if (source >= OS_WIDTH) {
            source = OS_WIDTH - 1;
        }
        present_x_map[x] = (uint16_t)source;
    }

    for (uint32_t y = 0; y < fb.height && y < MAX_OUTPUT_HEIGHT; ++y) {
        uint32_t source = (y * OS_HEIGHT) / fb.height;
        if (source >= OS_HEIGHT) {
            source = OS_HEIGHT - 1;
        }
        present_y_map[y] = (uint16_t)source;
    }
}

static uint16_t output_width_for_settings(const SettingsState *state) {
    static const uint16_t four_three[] = {960, 640, 480, 320, 192};
    static const uint16_t wide[] = {1280, 854, 640, 426, 256};
    return (state->widescreen ? wide : four_three)[state->resolution_mode % 5];
}

static uint16_t output_height_for_settings(const SettingsState *state) {
    static const uint16_t heights[] = {720, 480, 360, 240, 144};
    return heights[state->resolution_mode % 5];
}

static uint8_t output_bpp_for_settings(const SettingsState *state) {
    return state->palette_mode == 2 ? 16 : 8;
}

static void framebuffer_mode_string(char *buffer, size_t max_len, uint32_t width, uint32_t height, uint8_t bpp) {
    size_t len = 0;
    append_uint(buffer, &len, max_len, width);
    append_char(buffer, &len, max_len, 'x');
    append_uint(buffer, &len, max_len, height);
    append_char(buffer, &len, max_len, 'x');
    append_uint(buffer, &len, max_len, bpp);
}

static uint16_t bga_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

static void bga_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t vmware_port(uint8_t offset) {
    return (uint16_t)(vmware_svga.io_base + (uint16_t)vmware_svga.port_stride * offset);
}

static uint32_t vmware_read_reg(uint32_t index) {
    outl(vmware_port(SVGA_INDEX_PORT), index);
    return inl(vmware_port(SVGA_VALUE_PORT));
}

static void vmware_write_reg(uint32_t index, uint32_t value) {
    outl(vmware_port(SVGA_INDEX_PORT), index);
    outl(vmware_port(SVGA_VALUE_PORT), value);
}

static void clear_boot_status(void) {
    boot_status_text[0] = '\0';
    boot_menu_dirty = true;
}

static void set_boot_status(const char *text) {
    copy_string(boot_status_text, text, sizeof(boot_status_text));
    boot_menu_dirty = true;
}

static bool is_vmware_svga_device(uint16_t device_id) {
    return device_id == PCI_DEVICE_ID_VMWARE_SVGA ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA2 ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA3 ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA_EFI ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA4 ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA4_EFI ||
           device_id == PCI_DEVICE_ID_VMWARE_SVGA4_RO;
}

static bool find_vmware_svga_device(uint8_t *bus_out,
                                    uint8_t *slot_out,
                                    uint8_t *function_out,
                                    uint16_t *device_id_out,
                                    uint32_t *bar0_out) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t function = 0; function < 8; ++function) {
                uint32_t id = pci_config_read32((uint8_t)bus, slot, function, 0x00);
                uint16_t vendor_id;
                uint16_t device_id;
                uint32_t class_reg;

                if (id == 0xFFFFFFFFu) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                vendor_id = (uint16_t)(id & 0xFFFFu);
                device_id = (uint16_t)((id >> 16) & 0xFFFFu);
                if (vendor_id != PCI_VENDOR_ID_VMWARE || !is_vmware_svga_device(device_id)) {
                    continue;
                }

                class_reg = pci_config_read32((uint8_t)bus, slot, function, 0x08);
                if (((class_reg >> 24) & 0xFFu) != 0x03u || ((class_reg >> 16) & 0xFFu) != 0x00u) {
                    continue;
                }

                *bus_out = (uint8_t)bus;
                *slot_out = slot;
                *function_out = function;
                *device_id_out = device_id;
                *bar0_out = pci_config_read32((uint8_t)bus, slot, function, 0x10);
                return true;
            }
        }
    }

    return false;
}

static void pci_enable_device(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t command = pci_config_read32(bus, slot, function, 0x04);
    command |= 0x00000007u;
    pci_config_write32(bus, slot, function, 0x04, command);
}

static bool vmware_fifo_sync(void) {
    if (!vmware_svga.fifo_ready) {
        return false;
    }

    vmware_write_reg(SVGA_REG_SYNC, 1);
    while (vmware_read_reg(SVGA_REG_BUSY) != 0) {
    }
    return true;
}

static bool vmware_fifo_write_words(const uint32_t *words, uint32_t count) {
    volatile uint32_t *fifo = vmware_svga.fifo;
    uint32_t bytes_needed = count * 4u;

    if (!vmware_svga.fifo_ready || fifo == NULL || count == 0) {
        return false;
    }

    for (;;) {
        uint32_t min = fifo[SVGA_FIFO_MIN];
        uint32_t max = fifo[SVGA_FIFO_MAX];
        uint32_t next = fifo[SVGA_FIFO_NEXT_CMD];
        uint32_t stop = fifo[SVGA_FIFO_STOP];
        uint32_t free_bytes;

        if (next >= stop) {
            free_bytes = (max - next) + (stop - min);
        } else {
            free_bytes = stop - next;
        }

        if (free_bytes >= bytes_needed + 4u) {
            for (uint32_t i = 0; i < count; ++i) {
                fifo[next / 4u] = words[i];
                next += 4u;
                if (next == max) {
                    next = min;
                }
            }
            fifo[SVGA_FIFO_NEXT_CMD] = next;
            return true;
        }

        if (!vmware_fifo_sync()) {
            return false;
        }
    }
}

static void vmware_update_screen(void) {
    const uint32_t command[5] = {SVGA_CMD_UPDATE, 0, 0, fb.width, fb.height};

    if (!vmware_fifo_write_words(command, 5)) {
        return;
    }

    vmware_fifo_sync();
}

static bool init_vmware_svga_backend(void) {
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t function = 0;
    uint16_t device_id = 0;
    uint32_t bar0 = 0;
    uint32_t id = SVGA_ID_2;
    uint32_t fifo_regs_bytes;

    memset_local(&vmware_svga, 0, sizeof(vmware_svga));

    if (!find_vmware_svga_device(&bus, &slot, &function, &device_id, &bar0)) {
        return false;
    }

    pci_enable_device(bus, slot, function);

    if (device_id == PCI_DEVICE_ID_VMWARE_SVGA) {
        vmware_svga.io_base = VMWARE_SVGA_LEGACY_BASE_PORT;
        vmware_svga.port_stride = 4;
    } else {
        if ((bar0 & 0x1u) == 0) {
            return false;
        }
        vmware_svga.io_base = (uint16_t)(bar0 & ~0x3u);
        vmware_svga.port_stride = 1;
    }

    while (true) {
        vmware_write_reg(SVGA_REG_ID, id);
        if (vmware_read_reg(SVGA_REG_ID) == id) {
            break;
        }
        if (id == SVGA_ID_0) {
            return false;
        }
        --id;
    }

    vmware_svga.fb_start = vmware_read_reg(SVGA_REG_FB_START);
    vmware_svga.fb_size = vmware_read_reg(SVGA_REG_FB_SIZE);
    vmware_svga.mem_start = vmware_read_reg(SVGA_REG_MEM_START);
    vmware_svga.mem_size = vmware_read_reg(SVGA_REG_MEM_SIZE);
    vmware_svga.fifo_num_regs = vmware_read_reg(SVGA_REG_MEM_REGS);
    vmware_svga.caps = vmware_read_reg(SVGA_REG_CAPABILITIES);
    vmware_svga.host_bpp = vmware_read_reg(SVGA_REG_HOST_BITS_PER_PIXEL);
    if (vmware_svga.host_bpp == 0) {
        vmware_svga.host_bpp = vmware_read_reg(SVGA_REG_BITS_PER_PIXEL);
    }
    vmware_svga.max_width = vmware_read_reg(SVGA_REG_MAX_WIDTH);
    vmware_svga.max_height = vmware_read_reg(SVGA_REG_MAX_HEIGHT);

    if (vmware_svga.fb_start == 0 || vmware_svga.mem_start == 0 || vmware_svga.mem_size == 0) {
        return false;
    }

    if (vmware_svga.fifo_num_regs < 4) {
        vmware_svga.fifo_num_regs = 4;
    }

    fifo_regs_bytes = vmware_svga.fifo_num_regs * 4u;
    if (fifo_regs_bytes >= vmware_svga.mem_size) {
        return false;
    }

    vmware_svga.fifo = (volatile uint32_t *)(uintptr_t)vmware_svga.mem_start;
    vmware_svga.fifo[SVGA_FIFO_MIN] = fifo_regs_bytes;
    vmware_svga.fifo[SVGA_FIFO_MAX] = vmware_svga.mem_size;
    vmware_svga.fifo[SVGA_FIFO_NEXT_CMD] = fifo_regs_bytes;
    vmware_svga.fifo[SVGA_FIFO_STOP] = fifo_regs_bytes;
    vmware_write_reg(SVGA_REG_CONFIG_DONE, 1);
    vmware_write_reg(SVGA_REG_GUEST_ID, VMWARE_GUEST_ID_OTHER);
    vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
    vmware_svga.fifo_ready = true;

    fb.address = (uint8_t *)(uintptr_t)vmware_svga.fb_start;
    fb.width = OS_WIDTH;
    fb.height = OS_HEIGHT;
    fb.pitch = OS_WIDTH;
    fb.bpp = 8;
    update_present_maps();
    video_backend = VIDEO_BACKEND_VMWARE_SVGA;
    return true;
}

static bool detect_bga_backend(void) {
    uint16_t id = bga_read(VBE_DISPI_INDEX_ID);
    return id >= VBE_DISPI_ID0 && id <= VBE_DISPI_ID5;
}

static bool detect_video_mode_switch(void) {
    return video_backend == VIDEO_BACKEND_BGA || video_backend == VIDEO_BACKEND_VMWARE_SVGA;
}

static bool set_framebuffer_mode_raw(uint16_t width, uint16_t height, uint16_t bpp) {
    uint16_t actual_bpp = bpp;

    if (width > MAX_OUTPUT_WIDTH || height > MAX_OUTPUT_HEIGHT) {
        return false;
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        if (width > vmware_svga.max_width || height > vmware_svga.max_height) {
            return false;
        }

        if (bpp == 8) {
            if ((vmware_svga.caps & SVGA_CAP_8BIT_EMULATION) != 0) {
                actual_bpp = 8;
            } else if (vmware_svga.host_bpp >= 16) {
                actual_bpp = (uint16_t)vmware_svga.host_bpp;
            } else {
                return false;
            }
        } else if (bpp == 16) {
            if (vmware_svga.host_bpp < 16) {
                return false;
            }
            actual_bpp = (uint16_t)vmware_svga.host_bpp;
        } else if (bpp != vmware_svga.host_bpp) {
            return false;
        }

        vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
        vmware_write_reg(SVGA_REG_WIDTH, width);
        vmware_write_reg(SVGA_REG_HEIGHT, height);
        vmware_write_reg(SVGA_REG_BITS_PER_PIXEL, actual_bpp);
        vmware_svga.fb_offset = vmware_read_reg(SVGA_REG_FB_OFFSET);
        fb.pitch = vmware_read_reg(SVGA_REG_BYTES_PER_LINE);
        fb.width = vmware_read_reg(SVGA_REG_WIDTH);
        fb.height = vmware_read_reg(SVGA_REG_HEIGHT);
        fb.bpp = (uint8_t)vmware_read_reg(SVGA_REG_BITS_PER_PIXEL);
        fb.address = (uint8_t *)(uintptr_t)(vmware_svga.fb_start + vmware_svga.fb_offset);
        vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_ENABLE);

        if (fb.width != width || fb.height != height || fb.bpp != actual_bpp) {
            return false;
        }

        update_present_maps();
        return true;
    }

    if (video_backend == VIDEO_BACKEND_MULTIBOOT || video_backend == VIDEO_BACKEND_NONE) {
        return width == fb.width && height == fb.height && bpp == fb.bpp;
    }

    if (!video_mode_switch_available) {
        return false;
    }

    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, width);
    bga_write(VBE_DISPI_INDEX_YRES, height);
    bga_write(VBE_DISPI_INDEX_BPP, bpp);
    bga_write(VBE_DISPI_INDEX_BANK, 0);
    bga_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, height);
    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);

    fb.width = bga_read(VBE_DISPI_INDEX_XRES);
    fb.height = bga_read(VBE_DISPI_INDEX_YRES);
    fb.bpp = (uint8_t)bga_read(VBE_DISPI_INDEX_BPP);
    fb.pitch = fb.width * ((fb.bpp + 7u) / 8u);

    if (fb.width != width || fb.height != height || fb.bpp != bpp) {
        return false;
    }

    update_present_maps();
    return true;
}

static bool set_output_mode(const SettingsState *state) {
    return set_framebuffer_mode_raw(output_width_for_settings(state),
                                    output_height_for_settings(state),
                                    output_bpp_for_settings(state));
}

static void enter_boot_text_mode(void) {
    boot_text_mode = true;
    if (video_backend == VIDEO_BACKEND_BGA && video_mode_switch_available) {
        bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    } else if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        vmware_write_reg(SVGA_REG_ENABLE, SVGA_REG_ENABLE_DISABLE);
    }
}

static void enter_main_graphics_mode(void) {
    if (!set_framebuffer_mode_raw(OS_WIDTH, OS_HEIGHT, 8)) {
        return;
    }
    boot_text_mode = false;
    program_vga_palette();
}

static void set_idt_gate(uint8_t index, void (*handler)(void), uint8_t type_attr) {
    uintptr_t address = (uintptr_t)handler;
    idt[index].offset_low = (uint16_t)(address & 0xFFFFu);
    idt[index].selector = 0x10;
    idt[index].zero = 0;
    idt[index].type_attr = type_attr;
    idt[index].offset_high = (uint16_t)((address >> 16) & 0xFFFFu);
}

static void init_pic(void) {
    uint8_t master_mask = 0xF8;
    uint8_t slave_mask = 0xEF;

    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();
    outb(0x21, master_mask);
    outb(0xA1, slave_mask);
}

static void init_pit(void) {
    uint16_t divisor = (uint16_t)(1193182u / TIMER_HZ);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));
}

static void init_interrupts(void) {
    IdtPointer pointer;

    for (int i = 0; i < 256; ++i) {
        set_idt_gate((uint8_t)i, isr_default_stub, 0x8E);
    }
    set_idt_gate(32, irq0_stub, 0x8E);
    for (int i = 33; i <= 47; ++i) {
        set_idt_gate((uint8_t)i, irq_default_stub, 0x8E);
    }

    pointer.limit = (uint16_t)(sizeof(idt) - 1);
    pointer.base = (uint32_t)(uintptr_t)&idt[0];

    idt_load(&pointer);
    init_pic();
    init_pit();
}

static uint8_t nearest_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t best_distance = 0xFFFFFFFFu;
    uint8_t best_index = 0;

    for (int i = 0; i < 256; ++i) {
        int dr = (int)palette[i].r - r;
        int dg = (int)palette[i].g - g;
        int db = (int)palette[i].b - b;
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = (uint8_t)i;
        }
    }

    return best_index;
}

static void init_theme_colors(void) {
    color_black = nearest_color(0, 0, 0);
    color_white = nearest_color(255, 255, 255);
    color_gray_dark = nearest_color(64, 64, 64);
    color_gray = nearest_color(128, 128, 128);
    color_gray_light = nearest_color(210, 210, 210);
    color_green = nearest_color(0, 170, 0);
    color_green_dark = nearest_color(0, 100, 0);
    color_blue = nearest_color(60, 110, 220);
    color_blue_dark = nearest_color(20, 40, 100);
    color_red = nearest_color(180, 40, 40);
    color_yellow = nearest_color(230, 210, 40);
    color_orange = nearest_color(230, 130, 40);
    color_pink = nearest_color(220, 140, 180);
    color_desktop_icon = nearest_color(245, 245, 245);
    paint_color = color_black;
}

static void program_vga_palette(void) {
    if (fb.bpp != 8) {
        return;
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        for (int i = 0; i < 256; ++i) {
            Color output = settings_applied.palette_mode == 1 ? quantize_color_16(palette[i]) : palette[i];
            vmware_write_reg(SVGA_PALETTE_BASE + i * 3 + 0, output.r);
            vmware_write_reg(SVGA_PALETTE_BASE + i * 3 + 1, output.g);
            vmware_write_reg(SVGA_PALETTE_BASE + i * 3 + 2, output.b);
        }
        return;
    }

    outb(0x3C8, 0);
    for (int i = 0; i < 256; ++i) {
        Color output = settings_applied.palette_mode == 1 ? quantize_color_16(palette[i]) : palette[i];
        outb(0x3C9, output.r / 4);
        outb(0x3C9, output.g / 4);
        outb(0x3C9, output.b / 4);
    }
}

static bool init_framebuffer(uint32_t magic, const MultibootInfo *mbi) {
    uint32_t bar0 = 0;

    memset_local(&fb, 0, sizeof(fb));
    video_backend = VIDEO_BACKEND_NONE;
    memset_local(&vmware_svga, 0, sizeof(vmware_svga));

    if (magic != 0x2BADB002 || mbi == NULL) {
        return true;
    }

    if ((mbi->flags & (1u << 12)) != 0) {
        fb.address = (uint8_t *)(uintptr_t)mbi->framebuffer_addr;
        fb.width = mbi->framebuffer_width;
        fb.height = mbi->framebuffer_height;
        fb.pitch = mbi->framebuffer_pitch;
        fb.bpp = mbi->framebuffer_bpp;
        if (fb.address != NULL &&
            fb.width >= OS_WIDTH &&
            fb.height >= OS_HEIGHT &&
            fb.width <= MAX_OUTPUT_WIDTH &&
            fb.height <= MAX_OUTPUT_HEIGHT) {
            video_backend = VIDEO_BACKEND_MULTIBOOT;
            update_present_maps();
            return true;
        }
    }

    if (init_vmware_svga_backend()) {
        return true;
    }

    if (!detect_bga_backend()) {
        return true;
    }

    if (!find_vga_framebuffer_bar(&bar0)) {
        return true;
    }

    fb.address = (uint8_t *)(uintptr_t)bar0;
    fb.width = OS_WIDTH;
    fb.height = OS_HEIGHT;
    fb.pitch = OS_WIDTH;
    fb.bpp = 8;
    video_backend = VIDEO_BACKEND_BGA;
    update_present_maps();
    return true;
}

static void present(void) {
    if (boot_text_mode) {
        return;
    }

    if (fb.bpp == 8 && fb.width == OS_WIDTH && fb.height == OS_HEIGHT) {
        for (int y = 0; y < OS_HEIGHT; ++y) {
            uint8_t *dest = fb.address + (size_t)y * fb.pitch;
            memcpy_local(dest, &backbuffer[y * OS_WIDTH], OS_WIDTH);
        }
        if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
            vmware_update_screen();
        }
        return;
    }

    for (uint32_t y = 0; y < fb.height; ++y) {
        uint16_t sy = present_y_map[y];
        const uint8_t *src = &backbuffer[sy * OS_WIDTH];

        if (fb.bpp == 8) {
            uint8_t *dest = fb.address + (size_t)y * fb.pitch;
            for (uint32_t x = 0; x < fb.width; ++x) {
                dest[x] = src[present_x_map[x]];
            }
            continue;
        }

        if (fb.bpp == 16) {
            uint16_t *dest = (uint16_t *)(fb.address + (size_t)y * fb.pitch);
            for (uint32_t x = 0; x < fb.width; ++x) {
                Color c = palette[src[present_x_map[x]]];
                dest[x] = (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
            }
            continue;
        }

        if (fb.bpp == 24) {
            uint8_t *dest = fb.address + (size_t)y * fb.pitch;
            for (uint32_t x = 0; x < fb.width; ++x) {
                Color c = palette[src[present_x_map[x]]];
                dest[x * 3 + 0] = c.b;
                dest[x * 3 + 1] = c.g;
                dest[x * 3 + 2] = c.r;
            }
            continue;
        }

        {
            uint32_t *dest = (uint32_t *)(fb.address + (size_t)y * fb.pitch);
            for (uint32_t x = 0; x < fb.width; ++x) {
                Color c = palette[src[present_x_map[x]]];
                dest[x] = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
            }
        }
    }

    if (video_backend == VIDEO_BACKEND_VMWARE_SVGA) {
        vmware_update_screen();
    }
}

static void clear_screen(uint8_t color) {
    memset_local(backbuffer, color, sizeof(backbuffer));
}

static void draw_pixel(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= OS_WIDTH || y >= OS_HEIGHT) {
        return;
    }
    backbuffer[y * OS_WIDTH + x] = color;
}

static void fill_rect(int x, int y, int w, int h, uint8_t color) {
    int x0 = clampi(x, 0, OS_WIDTH);
    int y0 = clampi(y, 0, OS_HEIGHT);
    int x1 = clampi(x + w, 0, OS_WIDTH);
    int y1 = clampi(y + h, 0, OS_HEIGHT);

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            backbuffer[py * OS_WIDTH + px] = color;
        }
    }
}

static void draw_rect(int x, int y, int w, int h, uint8_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

static void draw_char(int x, int y, char ch, uint8_t fg, uint8_t bg, bool transparent) {
    uint8_t glyph_index;

    if ((unsigned char)ch < 32 || (unsigned char)ch > 127) {
        glyph_index = 0;
    } else {
        glyph_index = (uint8_t)((unsigned char)ch - 32);
    }

    for (int row = 0; row < 8; ++row) {
        uint8_t bits = font8x8_basic[glyph_index][row];
        for (int col = 0; col < 8; ++col) {
            if ((bits >> col) & 1u) {
                draw_pixel(x + col, y + row, fg);
            } else if (!transparent) {
                draw_pixel(x + col, y + row, bg);
            }
        }
    }
}

static void draw_char_scaled(int x, int y, char ch, uint8_t fg, uint8_t bg, bool transparent, int scale) {
    uint8_t glyph_index;
    if (scale < 1) {
        scale = 1;
    }

    if ((unsigned char)ch < 32 || (unsigned char)ch > 127) {
        glyph_index = 0;
    } else {
        glyph_index = (uint8_t)((unsigned char)ch - 32);
    }

    for (int row = 0; row < 8; ++row) {
        uint8_t bits = font8x8_basic[glyph_index][row];
        for (int col = 0; col < 8; ++col) {
            bool set = ((bits >> col) & 1u) != 0;
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    if (set) {
                        draw_pixel(x + col * scale + sx, y + row * scale + sy, fg);
                    } else if (!transparent) {
                        draw_pixel(x + col * scale + sx, y + row * scale + sy, bg);
                    }
                }
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent) {
    int cursor_x = x;
    int cursor_y = y;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\n') {
            cursor_x = x;
            cursor_y += 10;
            continue;
        }
        draw_char(cursor_x, cursor_y, text[i], fg, bg, transparent);
        cursor_x += 8;
    }
}

static void draw_text_scaled(int x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent, int scale) {
    int cursor_x = x;
    int cursor_y = y;

    if (scale < 1) {
        scale = 1;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\n') {
            cursor_x = x;
            cursor_y += 10 * scale;
            continue;
        }
        draw_char_scaled(cursor_x, cursor_y, text[i], fg, bg, transparent, scale);
        cursor_x += 8 * scale;
    }
}

static void draw_text_center(int center_x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent) {
    int x = center_x - (int)(strlen_local(text) * 8) / 2;
    draw_text(x, y, text, fg, bg, transparent);
}

static void draw_text_center_scaled(int center_x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent, int scale) {
    if (scale < 1) {
        scale = 1;
    }
    int x = center_x - (int)(strlen_local(text) * 8 * scale) / 2;
    draw_text_scaled(x, y, text, fg, bg, transparent, scale);
}

static void vga_text_write_at(int col, int row, const char *text, uint8_t attr) {
    if (row < 0 || row >= VGA_TEXT_ROWS || col >= VGA_TEXT_COLS) {
        return;
    }

    if (col < 0) {
        text -= col;
        col = 0;
    }

    for (int i = 0; text[i] != '\0' && col + i < VGA_TEXT_COLS; ++i) {
        vga_text_buffer[row * VGA_TEXT_COLS + col + i] = ((uint16_t)attr << 8) | (uint8_t)text[i];
    }
}

static void vga_text_clear(uint8_t attr) {
    for (int i = 0; i < VGA_TEXT_COLS * VGA_TEXT_ROWS; ++i) {
        vga_text_buffer[i] = ((uint16_t)attr << 8) | ' ';
    }
}

static void vga_text_disable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static void vga_text_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (uint8_t)((inb(0x3D5) & 0xC0) | cursor_start));
    outb(0x3D4, 0x0B);
    outb(0x3D5, (uint8_t)((inb(0x3D5) & 0xE0) | cursor_end));
}

static void vga_text_set_cursor(int col, int row) {
    uint16_t pos = (uint16_t)(row * VGA_TEXT_COLS + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFFu));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFFu));
}

static void draw_text_mode_row(int row, int col, const char *text, uint8_t attr) {
    vga_text_write_at(col, row, text, attr);
}

static void draw_text_mode_center(int row, const char *text, uint8_t attr) {
    int col = (VGA_TEXT_COLS - (int)strlen_local(text)) / 2;
    if (col < 0) {
        col = 0;
    }
    vga_text_write_at(col, row, text, attr);
}

static void draw_text_clipped(int x, int y, int max_w, const char *text, uint8_t fg, uint8_t bg, bool transparent) {
    int max_chars = max_w / 8;
    int cursor_x = x;

    if (max_chars <= 0) {
        return;
    }

    for (int i = 0; text[i] != '\0' && i < max_chars; ++i) {
        draw_char(cursor_x, y, text[i], fg, bg, transparent);
        cursor_x += 8;
    }
}

static void draw_text_block(int x, int y, int w, int h, const char *text, uint8_t fg, uint8_t bg, bool transparent) {
    int max_cols = w / 8;
    int max_rows = h / 10;
    int row = 0;
    int col = 0;

    if (max_cols <= 0 || max_rows <= 0) {
        return;
    }

    for (int i = 0; text[i] != '\0'; ++i) {
        char ch = text[i];
        if (ch == '\n') {
            ++row;
            col = 0;
            if (row >= max_rows) {
                break;
            }
            continue;
        }

        if (col >= max_cols) {
            ++row;
            col = 0;
            if (row >= max_rows) {
                break;
            }
        }

        draw_char(x + col * 8, y + row * 10, ch, fg, bg, transparent);
        ++col;
    }
}

static bool point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static uint16_t image_width(const uint8_t *image) {
    return (uint16_t)(image[0] | ((uint16_t)image[1] << 8));
}

static uint16_t image_height(const uint8_t *image) {
    return (uint16_t)(image[2] | ((uint16_t)image[3] << 8));
}

static const uint8_t *image_pixels(const uint8_t *image) {
    return image + 4;
}

static const uint8_t *image_alpha(const uint8_t *image) {
    size_t count = (size_t)image_width(image) * image_height(image);
    return image + 4 + count;
}

static int text_pixel_width(const char *text) {
    return (int)strlen_local(text) * 8;
}

static void draw_image_at(const uint8_t *image, int x, int y, bool transparent) {
    uint16_t width = image_width(image);
    uint16_t height = image_height(image);
    const uint8_t *pixels = image_pixels(image);
    const uint8_t *alpha = image_alpha(image);

    for (uint16_t py = 0; py < height; ++py) {
        for (uint16_t px = 0; px < width; ++px) {
            size_t index = (size_t)py * width + px;
            if (!transparent || alpha[index] >= 128) {
                draw_pixel(x + px, y + py, pixels[index]);
            }
        }
    }
}

static void draw_image(const uint8_t *image) {
    draw_image_at(image, 0, 0, false);
}

static const uint8_t *desktop_icon_image(int index) {
    return index == 0 ? _binary_build_notepad_icon_bin_start : _binary_build_terminal_icon_bin_start;
}

static const char *desktop_icon_label(int index) {
    return index == 0 ? "Notepad" : "Terminal";
}

static AppId desktop_icon_app(int index) {
    return index == 0 ? APP_NOTEPAD : APP_CMD;
}

static void init_desktop_icon_defaults(void) {
    desktop_icons[0].x = 18;
    desktop_icons[0].y = 24;
    desktop_icons[1].x = 18;
    desktop_icons[1].y = 104;
}

static uint32_t desktop_layout_checksum(const DesktopLayoutSector *layout) {
    return (uint32_t)(layout->magic ^
                      (uint32_t)layout->icon_x[0] ^ (uint32_t)layout->icon_y[0] ^
                      (uint32_t)layout->icon_x[1] ^ (uint32_t)layout->icon_y[1]);
}

static void desktop_icon_bounds(int x, int y, const uint8_t *image, const char *label, int *out_x, int *out_y, int *out_w, int *out_h) {
    int icon_w = image_width(image);
    int icon_h = image_height(image);
    int label_w = text_pixel_width(label);
    int width = (icon_w > label_w ? icon_w : label_w) + 8;

    *out_x = x;
    *out_y = y;
    *out_w = width;
    *out_h = icon_h + 24;
}

static void draw_dither_rect(int x, int y, int w, int h, uint8_t a, uint8_t b) {
    int x0 = clampi(x, 0, OS_WIDTH);
    int y0 = clampi(y, 0, OS_HEIGHT);
    int x1 = clampi(x + w, 0, OS_WIDTH);
    int y1 = clampi(y + h, 0, OS_HEIGHT);

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            draw_pixel(px, py, ((px + py) & 1) == 0 ? a : b);
        }
    }
}

static bool load_desktop_icon_positions(void) {
    uint8_t sector[512];
    const DesktopLayoutSector *layout = (const DesktopLayoutSector *)(const void *)sector;

    if (!boot_drive_valid || boot_drive_number < 0x80u) {
        return false;
    }
    if (!ata_pio_read_sector(DESKTOP_LAYOUT_LBA, sector)) {
        return false;
    }
    if (layout->magic != DESKTOP_LAYOUT_MAGIC || layout->checksum != desktop_layout_checksum(layout)) {
        return false;
    }

    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        if (layout->icon_x[i] < 0 || layout->icon_x[i] > OS_WIDTH - 64 ||
            layout->icon_y[i] < 0 || layout->icon_y[i] > OS_HEIGHT - TASKBAR_H - 64) {
            return false;
        }
        desktop_icons[i].x = layout->icon_x[i];
        desktop_icons[i].y = layout->icon_y[i];
    }

    desktop_icon_persistence_enabled = true;
    return true;
}

static void save_desktop_icon_positions(void) {
    uint8_t sector[512];
    DesktopLayoutSector *layout = (DesktopLayoutSector *)(void *)sector;

    if (!desktop_icon_persistence_enabled) {
        return;
    }

    memset_local(sector, 0, sizeof(sector));
    layout->magic = DESKTOP_LAYOUT_MAGIC;
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        layout->icon_x[i] = desktop_icons[i].x;
        layout->icon_y[i] = desktop_icons[i].y;
    }
    layout->checksum = desktop_layout_checksum(layout);
    ata_pio_write_sector(DESKTOP_LAYOUT_LBA, sector);
}

static void draw_desktop_icon(int index, bool selected) {
    int box_x;
    int box_y;
    int box_w;
    int box_h;
    int label_x;
    int label_y;
    int icon_x;
    int x = desktop_icons[index].x;
    int y = desktop_icons[index].y;
    const uint8_t *image = desktop_icon_image(index);
    const char *label = desktop_icon_label(index);
    int icon_w = image_width(image);
    int label_w = text_pixel_width(label);

    desktop_icon_bounds(x, y, image, label, &box_x, &box_y, &box_w, &box_h);
    icon_x = box_x + (box_w - icon_w) / 2;
    label_x = box_x + (box_w - label_w) / 2;
    label_y = box_y + image_height(image) + 10;

    if (selected) {
        draw_dither_rect(box_x - 2, box_y - 2, box_w + 4, image_height(image) + 6, color_blue, color_blue_dark);
        fill_rect(label_x - 4, label_y - 2, label_w + 8, 12, color_blue);
        draw_text(label_x, label_y, label, color_white, color_blue, true);
    } else {
        draw_text(label_x, label_y, label, color_black, 0, true);
    }

    draw_image_at(image, icon_x, box_y, true);
}

static int desktop_icon_hit_test(int x, int y) {
    for (int i = DESKTOP_ICON_COUNT - 1; i >= 0; --i) {
        int icon_x;
        int icon_y;
        int icon_w;
        int icon_h;

        desktop_icon_bounds(desktop_icons[i].x, desktop_icons[i].y,
                            desktop_icon_image(i), desktop_icon_label(i),
                            &icon_x, &icon_y, &icon_w, &icon_h);
        if (point_in_rect(x, y, icon_x, icon_y, icon_w, icon_h)) {
            return i;
        }
    }
    return -1;
}

static uint8_t solid_color_index(uint8_t mode) {
    switch (mode) {
        case 2: return color_red;
        case 3: return color_orange;
        case 4: return color_yellow;
        case 5: return color_green;
        case 6: return color_blue;
        case 7: return color_pink;
        case 8: return color_white;
        case 9: return color_black;
        default: return color_blue_dark;
    }
}

static void draw_desktop_background(void) {
    if (settings_applied.background_mode == 0 || settings_applied.background_mode == 1) {
        draw_image(_binary_build_desktop_bin_start);
    } else {
        clear_screen(solid_color_index(settings_applied.background_mode));
    }
}

static uint8_t cmos_read(uint8_t index) {
    outb(0x70, index);
    io_wait();
    return inb(0x71);
}

static uint8_t from_bcd(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value / 16) * 10));
}

static void read_datetime(char *buffer, size_t max_len) {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t reg_b;
    size_t len = 0;

    while (cmos_read(0x0A) & 0x80) {
    }

    second = cmos_read(0x00);
    minute = cmos_read(0x02);
    hour = cmos_read(0x04);
    day = cmos_read(0x07);
    month = cmos_read(0x08);
    year = cmos_read(0x09);
    reg_b = cmos_read(0x0B);

    if ((reg_b & 0x04) == 0) {
        second = from_bcd(second);
        minute = from_bcd(minute);
        hour = from_bcd((uint8_t)(hour & 0x7F));
        day = from_bcd(day);
        month = from_bcd(month);
        year = from_bcd(year);
    } else {
        hour &= 0x7F;
    }

    if ((reg_b & 0x02) == 0 && (cmos_read(0x04) & 0x80)) {
        hour = (uint8_t)(((hour + 12) % 24));
    }

    append_uint(buffer, &len, max_len, 2000u + year);
    append_char(buffer, &len, max_len, '-');
    append_padded_uint(buffer, &len, max_len, month, 2);
    append_char(buffer, &len, max_len, '-');
    append_padded_uint(buffer, &len, max_len, day, 2);
    append_char(buffer, &len, max_len, ' ');
    append_padded_uint(buffer, &len, max_len, hour, 2);
    append_char(buffer, &len, max_len, ':');
    append_padded_uint(buffer, &len, max_len, minute, 2);
    append_char(buffer, &len, max_len, ':');
    append_padded_uint(buffer, &len, max_len, second, 2);
}

static uint32_t rand_next(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static int rand_range(int max_value) {
    if (max_value <= 0) {
        return 0;
    }
    return (int)((rand_next() >> 16) % (uint32_t)max_value);
}

static int open_window_count(void) {
    int count = 0;
    for (int app = 0; app < APP_COUNT; ++app) {
        if (windows[app].open) {
            ++count;
        }
    }
    return count;
}

static int task_manager_process_count(void) {
    return 3 + open_window_count();
}

static bool task_manager_process_info(int index, const char **name_out, const char **type_out, AppId *app_out) {
    static const char *system_names[] = {"HaloxOS Kernel", "Timer Service", "Input Service"};
    static const char *system_types[] = {"System", "System", "System"};
    int app_index = 0;

    if (index < 0) {
        return false;
    }

    if (index < 3) {
        *name_out = system_names[index];
        *type_out = system_types[index];
        *app_out = APP_NOTEPAD;
        return false;
    }

    for (int app = 0; app < APP_COUNT; ++app) {
        if (!windows[app].open) {
            continue;
        }
        if (app_index == index - 3) {
            *name_out = windows[app].title != NULL ? windows[app].title : app_titles[app];
            *type_out = app == active_window ? "Window (Active)" : "Window";
            *app_out = (AppId)app;
            return true;
        }
        ++app_index;
    }

    return false;
}

static bool task_manager_selected_is_killable(AppId *app_out) {
    const char *name;
    const char *type;
    AppId app = APP_NOTEPAD;
    bool killable = task_manager_process_info(task_manager_selected_process, &name, &type, &app);

    if (app_out != NULL) {
        *app_out = app;
    }
    return killable;
}

static uint32_t gpu_palette_entry_count(void) {
    if (fb.bpp == 8) {
        return settings_applied.palette_mode == 1 ? 16u : 256u;
    }
    if (fb.bpp >= 16) {
        return 65536u;
    }
    return 0;
}

static uint8_t gpu_palette_entry_color(uint32_t index) {
    if (fb.bpp == 8) {
        if (index >= gpu_palette_entry_count()) {
            return color_gray_light;
        }
        return (uint8_t)index;
    }

    if (fb.bpp == 16) {
        uint8_t r = (uint8_t)(((index >> 11) & 0x1Fu) * 255u / 31u);
        uint8_t g = (uint8_t)(((index >> 5) & 0x3Fu) * 255u / 63u);
        uint8_t b = (uint8_t)((index & 0x1Fu) * 255u / 31u);
        return nearest_color(r, g, b);
    }

    return nearest_color((uint8_t)((index >> 11) & 0xF8u),
                         (uint8_t)((index >> 5) & 0xFCu),
                         (uint8_t)((index << 3) & 0xF8u));
}

static uint32_t gpu_palette_max_scroll(void) {
    uint32_t total = gpu_palette_entry_count();

    if (total <= GPU_GRID_PAGE_ENTRIES) {
        return 0;
    }
    return total - GPU_GRID_PAGE_ENTRIES;
}

static void gpu_palette_scroll_geometry(int track_y,
                                        int *thumb_y_out,
                                        int *thumb_h_out,
                                        int *thumb_track_out) {
    uint32_t max_scroll = gpu_palette_max_scroll();
    int thumb_h = 20;
    int thumb_track = 98 - thumb_h;
    int thumb_y = track_y + 1;

    if (max_scroll != 0) {
        thumb_y += (int)((gpu_palette_scroll * (uint32_t)thumb_track) / max_scroll);
    }

    if (thumb_y_out != NULL) {
        *thumb_y_out = thumb_y;
    }
    if (thumb_h_out != NULL) {
        *thumb_h_out = thumb_h;
    }
    if (thumb_track_out != NULL) {
        *thumb_track_out = thumb_track;
    }
}

static const char *disk_physical_type_label(void) {
    if (boot_drive_valid) {
        if (boot_drive_number >= 0xE0u || boot_drive_number >= 0x9Fu) {
            return "CD-ROM";
        }
        if (boot_drive_number >= 0x80u) {
            return "Hard Disk";
        }
        return "USB";
    }
    return "CD-ROM";
}

static uint32_t detect_total_ram_bytes(const MultibootInfo *mbi) {
    if (mbi != NULL && (mbi->flags & (1u << 6)) != 0 && mbi->mmap_addr != 0 && mbi->mmap_length != 0) {
        uint64_t total = 0;
        uintptr_t cursor = (uintptr_t)mbi->mmap_addr;
        uintptr_t end = cursor + mbi->mmap_length;

        while (cursor + sizeof(uint32_t) <= end) {
            const MultibootMmapEntry *entry = (const MultibootMmapEntry *)cursor;
            if (cursor + entry->size + sizeof(uint32_t) > end) {
                break;
            }
            if (entry->type == 1u || entry->type == 3u) {
                total += entry->length;
            }
            cursor += entry->size + sizeof(uint32_t);
        }

        if (total > 0xFFFFFFFFu) {
            total = 0xFFFFFFFFu;
        }
        if (total >= 4u * 1024u * 1024u) {
            return (uint32_t)total;
        }
    }

    if (mbi != NULL && (mbi->flags & 1u) != 0) {
        uint32_t total_kb = mbi->mem_lower + mbi->mem_upper;
        return total_kb * 1024u;
    }

    return 32u * 1024u * 1024u;
}

static bool desktop_should_redraw(void) {
    uint32_t second = timer_ticks / TIMER_HZ;
    uint32_t perf_phase = timer_ticks / PERF_UPDATE_TICKS;
    uint32_t blink_phase = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;

    if (last_desktop_redraw_input_tick != last_input_tick) {
        return true;
    }
    if (last_desktop_redraw_second != second) {
        return true;
    }
    if (windows[APP_TASK_MANAGER].open && task_manager_tab == 1 && last_desktop_redraw_perf_phase != perf_phase) {
        return true;
    }
    if (windows[APP_CMD].open && last_desktop_redraw_terminal_blink != blink_phase) {
        return true;
    }
    if (windows[APP_TASK_MANAGER].open && task_manager_tab == 2 && task_manager_gpu_scroll_drag) {
        return true;
    }
    if (windows[APP_SNAKE].open && last_desktop_redraw_snake_tick != snake_last_step_tick) {
        return true;
    }
    return false;
}

static void mark_desktop_redrawn(void) {
    last_desktop_redraw_input_tick = last_input_tick;
    last_desktop_redraw_second = timer_ticks / TIMER_HZ;
    last_desktop_redraw_perf_phase = timer_ticks / PERF_UPDATE_TICKS;
    last_desktop_redraw_terminal_blink = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;
    last_desktop_redraw_snake_tick = snake_last_step_tick;
}

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

static void enqueue_key(KeyCode code, char ch) {
    int next = (key_tail + 1) % 64;
    if (next == key_head) {
        return;
    }
    last_input_tick = timer_ticks;
    key_queue[key_tail].code = code;
    key_queue[key_tail].ch = ch;
    key_tail = next;
}

static bool dequeue_key(KeyEvent *event) {
    if (key_head == key_tail) {
        return false;
    }
    *event = key_queue[key_head];
    key_head = (key_head + 1) % 64;
    return true;
}

static char scancode_to_char(uint8_t scancode, bool shifted) {
    static const char normal[] =
        "\0\0331234567890-=\0\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shifted_map[] =
        "\0\033!@#$%^&*()_+\0\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

    if (scancode >= sizeof(normal) - 1) {
        return 0;
    }
    return shifted ? shifted_map[scancode] : normal[scancode];
}

static void handle_scancode(uint8_t code) {
    if (code == 0xE0) {
        keyboard_extended = true;
        return;
    }

    if (code == 0x2A || code == 0x36) {
        keyboard_shift = true;
        return;
    }

    if (code == 0x38) {
        keyboard_alt = true;
        return;
    }

    if (code == 0xAA || code == 0xB6) {
        keyboard_shift = false;
        return;
    }

    if (code == 0xB8) {
        keyboard_alt = false;
        return;
    }

    if (code & 0x80) {
        keyboard_extended = false;
        return;
    }

    if (keyboard_extended) {
        keyboard_extended = false;
        switch (code) {
            case 0x48: enqueue_key(KEY_UP, 0); return;
            case 0x50: enqueue_key(KEY_DOWN, 0); return;
            case 0x4B: enqueue_key(KEY_LEFT, 0); return;
            case 0x4D: enqueue_key(KEY_RIGHT, 0); return;
            default: return;
        }
    }

    switch (code) {
        case 0x01: enqueue_key(KEY_ESC, 0); return;
        case 0x0E: enqueue_key(KEY_BACKSPACE, 0); return;
        case 0x0F: enqueue_key(KEY_TAB, 0); return;
        case 0x1C: enqueue_key(KEY_ENTER, '\n'); return;
        case 0x3E: enqueue_key(KEY_F4, 0); return;
        default: {
            char ch = scancode_to_char(code, keyboard_shift);
            if (ch) {
                enqueue_key(KEY_NONE, ch);
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

static void init_mouse(void) {
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x64, 0xA8);
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x64, 0x20);
    if (!ps2_wait_read()) {
        return;
    }
    uint8_t status = inb(0x60);
    status |= 0x02;
    status &= (uint8_t)~0x20;
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x64, 0x60);
    if (!ps2_wait_write()) {
        return;
    }
    outb(0x60, status);

    ps2_write_mouse(0xF6);
    if (ps2_wait_read()) {
        inb(0x60);
    }
    ps2_write_mouse(0xF4);
    if (ps2_wait_read()) {
        inb(0x60);
    }
}

static void poll_input(void) {
    mouse.prev_left = mouse.left;
    mouse.prev_right = mouse.right;
    mouse.prev_middle = mouse.middle;

    while (inb(0x64) & 0x01) {
        uint8_t status = inb(0x64);
        uint8_t data = inb(0x60);

        if (status & 0x20) {
            if (mouse_packet_index == 0 && (data & 0x08) == 0) {
                continue;
            }

            mouse_packet[mouse_packet_index++] = data;
            if (mouse_packet_index == 3) {
                int dx = (mouse_packet[0] & 0x10) ? (int)mouse_packet[1] - 256 : (int)mouse_packet[1];
                int dy = (mouse_packet[0] & 0x20) ? (int)mouse_packet[2] - 256 : (int)mouse_packet[2];
                int next_x = clampi(mouse.x + dx, 0, OS_WIDTH - 1);
                int next_y = clampi(mouse.y - dy, 0, OS_HEIGHT - 1);
                bool next_left = (mouse_packet[0] & 0x01) != 0;
                bool next_right = (mouse_packet[0] & 0x02) != 0;
                bool next_middle = (mouse_packet[0] & 0x04) != 0;
                if (next_x != mouse.x || next_y != mouse.y ||
                    next_left != mouse.left || next_right != mouse.right || next_middle != mouse.middle) {
                    last_input_tick = timer_ticks;
                }
                mouse.x = next_x;
                mouse.y = next_y;
                mouse.left = next_left;
                mouse.right = next_right;
                mouse.middle = next_middle;
                mouse_packet_index = 0;
            }
        } else {
            handle_scancode(data);
        }
    }
}

static void terminal_reset(Terminal *term) {
    memset_local(term, 0, sizeof(*term));
}

static void terminal_add_line(Terminal *term, const char *text) {
    if (term->line_count == TERM_MAX_LINES) {
        for (int i = 1; i < TERM_MAX_LINES; ++i) {
            memcpy_local(term->lines[i - 1], term->lines[i], TERM_LINE_LEN);
        }
        --term->line_count;
    }
    copy_string(term->lines[term->line_count], text, TERM_LINE_LEN);
    ++term->line_count;
}

static void reset_snake(void) {
    snake_length = 4;
    snake_dir = 1;
    snake_next_dir = 1;
    snake_last_step_tick = timer_ticks;
    snake_score = 0;
    snake_dead = false;
    for (int i = 0; i < snake_length; ++i) {
        snake_x[i] = 5 - i;
        snake_y[i] = 5;
    }
    snake_food_x = 12;
    snake_food_y = 7;
}

static void reset_guess(void) {
    guess_input_len = 0;
    guess_input[0] = '\0';
    copy_string(guess_message, "Guess a number from 1 to 100.", sizeof(guess_message));
    guess_target = rand_range(100) + 1;
}

static void mines_place(void) {
    memset_local(mines_value, 0, sizeof(mines_value));
    memset_local(mines_revealed, 0, sizeof(mines_revealed));
    memset_local(mines_flagged, 0, sizeof(mines_flagged));
    mines_lost = false;
    mines_won = false;

    int placed = 0;
    while (placed < MINES_COUNT) {
        int x = rand_range(MINES_SIZE);
        int y = rand_range(MINES_SIZE);
        if (mines_value[y][x] == 9) {
            continue;
        }
        mines_value[y][x] = 9;
        ++placed;
    }

    for (int y = 0; y < MINES_SIZE; ++y) {
        for (int x = 0; x < MINES_SIZE; ++x) {
            if (mines_value[y][x] == 9) {
                continue;
            }
            uint8_t count = 0;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    int nx = x + ox;
                    int ny = y + oy;
                    if (nx >= 0 && ny >= 0 && nx < MINES_SIZE && ny < MINES_SIZE && mines_value[ny][nx] == 9) {
                        ++count;
                    }
                }
            }
            mines_value[y][x] = count;
        }
    }
}

static void open_window(AppId app) {
    Window *window = &windows[app];
    menu_open = false;
    context_menu_open = false;
    task_manager_confirm_kill = false;
    if (!window->open) {
        window->open = true;
        window->title = app == APP_GAME_CENTER ? "Game Center" :
                        (app == APP_TASK_MANAGER ? "Task Manager" : app_titles[app]);
        window->w = (app == APP_SETTINGS) ? 400 :
                    (app == APP_TASK_MANAGER ? 440 :
                    (app == APP_POWER ? 220 :
                    (app == APP_GAME_CENTER ? 360 :
                    (app == APP_PAINT ? 340 :
                    (app == APP_EXPLORER ? 336 : 300)))));
        window->h = (app == APP_SETTINGS) ? 260 :
                    (app == APP_TASK_MANAGER ? 360 :
                    (app == APP_POWER ? 160 :
                    (app == APP_GAME_CENTER ? 230 :
                    (app == APP_MINES ? 250 :
                    (app == APP_PAINT ? 240 :
                    (app == APP_EXPLORER ? 220 : 200))))));
        window->x = 70 + app * 18;
        window->y = 40 + app * 12;
        if (window->x + window->w > OS_WIDTH - 10) {
            window->x = 20;
        }
        if (window->y + window->h > OS_HEIGHT - TASKBAR_H - 10) {
            window->y = 40;
        }
    }

    if (app == APP_SNAKE) {
        reset_snake();
    } else if (app == APP_GUESS) {
        reset_guess();
    } else if (app == APP_MINES) {
        mines_place();
    } else if (app == APP_SETTINGS) {
        settings_pending = settings_applied;
        settings_tab = 0;
    } else if (app == APP_TASK_MANAGER) {
        task_manager_tab = 0;
        task_manager_selected_process = 0;
        task_manager_confirm_kill = false;
        task_manager_kill_target = -1;
    }

    active_window = app;
}

static void set_active_window(AppId app) {
    active_window = app;
    menu_open = false;
    context_menu_open = false;
}

static void close_window(AppId app) {
    windows[app].open = false;
    if (app == APP_TASK_MANAGER) {
        task_manager_confirm_kill = false;
        task_manager_kill_target = -1;
        task_manager_gpu_scroll_drag = false;
    }
    if (active_window == (int)app) {
        active_window = -1;
        for (int i = APP_COUNT - 1; i >= 0; --i) {
            if (windows[i].open) {
                active_window = i;
                break;
            }
        }
    }
}

static void open_desktop(void) {
    boot_text_mode = false;
    update_present_maps();
    system_state = STATE_DESKTOP;
    menu_open = false;
    context_menu_open = false;
    cpu_halted_overlay = false;
    shutdown_pending = false;
    active_window = -1;
    task_manager_gpu_scroll_drag = false;
    last_desktop_redraw_input_tick = 0xFFFFFFFFu;
    last_desktop_redraw_second = 0xFFFFFFFFu;
    last_desktop_redraw_perf_phase = 0xFFFFFFFFu;
    last_desktop_redraw_terminal_blink = 0xFFFFFFFFu;
    last_desktop_redraw_snake_tick = 0xFFFFFFFFu;
}

static void attempt_poweroff(void) {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
}

static void shutdown_system(void) {
    shutdown_pending = true;
    system_state = STATE_SHUTDOWN;
}

static void restart_system(void) {
    while (inb(0x64) & 0x02) {
    }
    outb(0x64, 0xFE);
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void execute_terminal_command(Terminal *term, bool boot_console) {
    char command[TERM_LINE_LEN];
    copy_string(command, term->input, sizeof(command));

    term->input_len = 0;
    term->input[0] = '\0';

    if (streq(command, "")) {
        return;
    }

    terminal_add_line(term, command);
    if (boot_console) {
        boot_terminal_dirty = true;
    }

    if (streq(command, "help")) {
        terminal_add_line(term, "help clear cls date time boot shutdown restart halt echo");
    } else if (streq(command, "clear") || streq(command, "cls")) {
        terminal_reset(term);
    } else if (streq(command, "boot")) {
        if (boot_console) {
            enter_main_graphics_mode();
            if (boot_text_mode) {
                terminal_add_line(term, "ERROR: Graphics mode unavailable!!!");
                return;
            }
        }
        open_desktop();
    } else if (streq(command, "shutdown")) {
        shutdown_system();
    } else if (streq(command, "restart")) {
        restart_system();
    } else if (streq(command, "halt")) {
        cpu_halted_overlay = true;
        terminal_reset(term);
        terminal_add_line(term, "GAME OVER!");
        terminal_add_line(term, "Your world's halting!!! so unpromising...");
    } else if (streq(command, "date") || streq(command, "time")) {
        char buffer[40] = {0};
        read_datetime(buffer, sizeof(buffer));
        terminal_add_line(term, buffer);
    } else if (starts_with(command, "echo ")) {
        terminal_add_line(term, command + 5);
    } else if (streq(command, "about")) {
        terminal_add_line(term, boot_console ? "HaloxOS boot console" : "HaloxOS command prompt");
    } else {
        terminal_add_line(term, "Unknown command. Type help.");
    }
}

static void terminal_handle_key(Terminal *term, KeyEvent event, bool boot_console) {
    if (event.code == KEY_BACKSPACE) {
        if (term->input_len > 0) {
            --term->input_len;
            term->input[term->input_len] = '\0';
            if (boot_console) {
                boot_terminal_dirty = true;
            }
        }
        return;
    }

    if (event.code == KEY_ENTER) {
        execute_terminal_command(term, boot_console);
        return;
    }

    if (event.code == KEY_ESC && boot_console) {
        enter_boot_text_mode();
        system_state = STATE_BOOT_MENU;
        boot_menu_dirty = true;
        return;
    }

    if (event.ch >= 32 && event.ch <= 126 && term->input_len + 1 < TERM_LINE_LEN) {
        term->input[term->input_len++] = event.ch;
        term->input[term->input_len] = '\0';
        if (boot_console) {
            boot_terminal_dirty = true;
        }
    }
}

static void apply_settings(void) {
    SettingsState next = settings_pending;
    bool video_changed = settings_applied.palette_mode != next.palette_mode ||
                         settings_applied.resolution_mode != next.resolution_mode ||
                         settings_applied.widescreen != next.widescreen;

    if (video_changed && !set_output_mode(&next)) {
        next.palette_mode = settings_applied.palette_mode;
        next.resolution_mode = settings_applied.resolution_mode;
        next.widescreen = settings_applied.widescreen;
    }

    settings_applied = next;
    settings_pending = next;
    program_vga_palette();
}

static bool settings_dirty(void) {
    return settings_applied.palette_mode != settings_pending.palette_mode ||
           settings_applied.resolution_mode != settings_pending.resolution_mode ||
           settings_applied.widescreen != settings_pending.widescreen ||
           settings_applied.background_mode != settings_pending.background_mode;
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
        "desktop.png",
        //"Import as Custom",
        "Solid Red",
        "Solid Orange",
        "Solid Yellow",
        "Solid Green",
        "Solid Blue",
        "Solid Pink",
        "Solid White",
        "Solid Black"
    };
    return names[index % 9];
}

static void resolution_string(char *buffer, size_t max_len, const SettingsState *state) {
    size_t len = 0;
    append_uint(buffer, &len, max_len, output_width_for_settings(state));
    append_char(buffer, &len, max_len, 'x');
    append_uint(buffer, &len, max_len, output_height_for_settings(state));
}

static void reveal_mines(int x, int y) {
    if (x < 0 || y < 0 || x >= MINES_SIZE || y >= MINES_SIZE) {
        return;
    }
    if (mines_revealed[y][x] || mines_flagged[y][x]) {
        return;
    }
    mines_revealed[y][x] = true;
    if (mines_value[y][x] == 0) {
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox != 0 || oy != 0) {
                    reveal_mines(x + ox, y + oy);
                }
            }
        }
    }
}

static void update_mines_win(void) {
    int revealed = 0;
    for (int y = 0; y < MINES_SIZE; ++y) {
        for (int x = 0; x < MINES_SIZE; ++x) {
            if (mines_revealed[y][x]) {
                ++revealed;
            }
        }
    }
    if (revealed == MINES_SIZE * MINES_SIZE - MINES_COUNT) {
        mines_won = true;
    }
}

static void handle_guess_submit(void) {
    int value = 0;
    for (int i = 0; i < guess_input_len; ++i) {
        value = value * 10 + (guess_input[i] - '0');
    }
    if (guess_input_len == 0) {
        copy_string(guess_message, "Enter a number first.", sizeof(guess_message));
        return;
    }

    if (value == guess_target) {
        copy_string(guess_message, "Correct! New number ready.", sizeof(guess_message));
        guess_target = rand_range(100) + 1;
    } else if (value < guess_target) {
        copy_string(guess_message, "Too low. Try again.", sizeof(guess_message));
    } else {
        copy_string(guess_message, "Too high. Try again.", sizeof(guess_message));
    }

    guess_input_len = 0;
    guess_input[0] = '\0';
}

static void snake_spawn_food(void) {
    bool occupied = true;
    while (occupied) {
        occupied = false;
        snake_food_x = rand_range(20);
        snake_food_y = rand_range(14);
        for (int i = 0; i < snake_length; ++i) {
            if (snake_x[i] == snake_food_x && snake_y[i] == snake_food_y) {
                occupied = true;
                break;
            }
        }
    }
}

static void update_snake(void) {
    if (!windows[APP_SNAKE].open || snake_dead || cpu_halted_overlay) {
        return;
    }

    if (timer_ticks - snake_last_step_tick < SNAKE_STEP_TICKS) {
        return;
    }
    snake_last_step_tick = timer_ticks;

    if ((snake_dir == 0 && snake_next_dir != 2) || (snake_dir == 2 && snake_next_dir != 0) ||
        (snake_dir == 1 && snake_next_dir != 3) || (snake_dir == 3 && snake_next_dir != 1)) {
        snake_dir = snake_next_dir;
    }

    int next_x = snake_x[0];
    int next_y = snake_y[0];
    if (snake_dir == 0) --next_y;
    if (snake_dir == 1) ++next_x;
    if (snake_dir == 2) ++next_y;
    if (snake_dir == 3) --next_x;

    if (next_x < 0 || next_y < 0 || next_x >= 20 || next_y >= 14) {
        snake_dead = true;
        return;
    }

    for (int i = 0; i < snake_length; ++i) {
        if (snake_x[i] == next_x && snake_y[i] == next_y) {
            snake_dead = true;
            return;
        }
    }

    for (int i = snake_length; i > 0; --i) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }
    snake_x[0] = next_x;
    snake_y[0] = next_y;

    if (next_x == snake_food_x && next_y == snake_food_y) {
        if (snake_length < SNAKE_MAX_SEGMENTS - 1) {
            ++snake_length;
        }
        snake_score += 10;
        snake_spawn_food();
    }
}

static void handle_text_target(KeyEvent event) {
    if (active_window == APP_NOTEPAD) {
        if (event.code == KEY_BACKSPACE) {
            if (notepad_len > 0) {
                --notepad_len;
                notepad_text[notepad_len] = '\0';
            }
        } else if (event.code == KEY_ENTER) {
            append_char(notepad_text, &notepad_len, sizeof(notepad_text), '\n');
        } else if (event.ch >= 32 && event.ch <= 126) {
            append_char(notepad_text, &notepad_len, sizeof(notepad_text), event.ch);
        }
    } else if (active_window == APP_CMD) {
        terminal_handle_key(&cmd_term, event, false);
    } else if (active_window == APP_GUESS) {
        if (event.code == KEY_BACKSPACE) {
            if (guess_input_len > 0) {
                --guess_input_len;
                guess_input[guess_input_len] = '\0';
            }
        } else if (event.code == KEY_ENTER) {
            handle_guess_submit();
        } else if (event.ch >= '0' && event.ch <= '9' && guess_input_len + 1 < (int)sizeof(guess_input)) {
            guess_input[guess_input_len++] = event.ch;
            guess_input[guess_input_len] = '\0';
        }
    } else if (active_window == APP_SNAKE) {
        if (event.code == KEY_UP) snake_next_dir = 0;
        if (event.code == KEY_RIGHT) snake_next_dir = 1;
        if (event.code == KEY_DOWN) snake_next_dir = 2;
        if (event.code == KEY_LEFT) snake_next_dir = 3;
        if (event.code == KEY_ENTER && snake_dead) {
            reset_snake();
            snake_spawn_food();
        }
    } else if (active_window == APP_MINES) {
        if (event.code == KEY_ENTER && (mines_lost || mines_won)) {
            mines_place();
        }
    }
}

static void draw_button(int x, int y, int w, int h, const char *label, uint8_t fill, uint8_t border, uint8_t text) {
    fill_rect(x, y, w, h, fill);
    draw_rect(x, y, w, h, border);
    draw_text_center(x + w / 2, y + (h - 8) / 2, label, text, fill, true);
}

static bool button_clicked(int x, int y, int w, int h) {
    return point_in_rect(mouse.x, mouse.y, x, y, w, h) && mouse.left && !mouse.prev_left;
}

static bool button_right_clicked(int x, int y, int w, int h) {
    return point_in_rect(mouse.x, mouse.y, x, y, w, h) && mouse.right && !mouse.prev_right;
}

static void draw_window_chrome(const Window *window) {
    fill_rect(window->x, window->y, window->w, window->h, color_gray_light);
    draw_rect(window->x, window->y, window->w, window->h, color_black);
    fill_rect(window->x, window->y, window->w, 18, active_window == (int)(window - windows) ? color_blue : color_gray);
    draw_text(window->x + 4, window->y + 5, window->title, color_white, color_blue, true);
    fill_rect(window->x + window->w - 18, window->y + 3, 12, 12, color_red);
    draw_rect(window->x + window->w - 18, window->y + 3, 12, 12, color_black);
    draw_text(window->x + window->w - 15, window->y + 5, "x", color_white, color_red, true);
}

static void render_terminal(const Terminal *term, int x, int y, int w, int h) {
    fill_rect(x, y, w, h, color_black);
    draw_rect(x, y, w, h, color_gray);
    int max_lines = (h - 20) / 10;
    int start = term->line_count > max_lines ? term->line_count - max_lines : 0;
    int line_y = y + 4;
    int text_w = w - 8;
    for (int i = start; i < term->line_count; ++i) {
        draw_text_clipped(x + 4, line_y, text_w, term->lines[i], color_green, color_black, true);
        line_y += 10;
    }
    draw_text(x + 4, y + h - 14, "A:\\>", color_green, color_black, true);
    {
        int prompt_w = 36;
        int input_w = w - prompt_w - 8;
        int max_chars = input_w / 8;
        int input_start = term->input_len > max_chars ? term->input_len - max_chars : 0;
        draw_text_clipped(x + prompt_w, y + h - 14, input_w, &term->input[input_start], color_green, color_black, true);
        if (((timer_ticks / TERMINAL_CURSOR_BLINK_TICKS) & 1u) == 0) {
            int cursor_chars = term->input_len - input_start;
            if (cursor_chars > max_chars) {
                cursor_chars = max_chars;
            }
            draw_text(x + prompt_w + cursor_chars * 8, y + h - 14, "_", color_green, color_black, true);
        }
    }
}

static void render_notepad(const Window *window) {
    int tx = window->x + 6;
    int ty = window->y + 24;
    int max_h = window->h - 30;
    fill_rect(tx, ty, window->w - 12, max_h, color_white);
    draw_rect(tx, ty, window->w - 12, max_h, color_gray_dark);
    draw_text_block(tx + 4, ty + 4, window->w - 20, max_h - 8, notepad_text[0] ? notepad_text : "Type here...", color_black, color_white, true);
}

static void render_explorer(const Window *window) {
    static const char *names[] = {
        "Desktop",
        "Notepad",
        "Command Prompt",
        "Paint",
        "Games",
        "Settings"
    };
    static const char *types[] = {
        "Folder",
        "Text App",
        "Console",
        "Image App",
        "Folder",
        "Control"
    };
    int left_x = window->x + 8;
    int left_w = 82;
    int right_x = left_x + left_w + 8;
    int content_y = window->y + 26;
    int content_h = window->h - 34;
    int footer_h = 38;
    int body_h = content_h - footer_h;
    int header_y = content_y + 8;
    int row_start_y = content_y + 26;
    int row_h = 16;
    int name_w = 124;
    int type_w = window->w - left_w - 34 - name_w;

    fill_rect(window->x + 8, content_y, window->w - 16, content_h, color_white);
    fill_rect(left_x, content_y, left_w, content_h, color_gray_light);
    draw_rect(left_x, content_y, left_w, content_h, color_gray_dark);
    draw_text(left_x + 6, header_y, "Places", color_blue_dark, color_gray_light, true);
    draw_text(left_x + 6, content_y + 24, "A:\\", color_black, color_gray_light, true);
    draw_text(left_x + 6, content_y + 40, "Desktop", color_black, color_gray_light, true);
    draw_text(left_x + 6, content_y + 56, "System", color_black, color_gray_light, true);

    draw_text(right_x, header_y, "Name", color_blue_dark, color_white, true);
    draw_text(right_x + name_w, header_y, "Type", color_blue_dark, color_white, true);
    draw_rect(right_x - 2, content_y + 18, window->w - left_w - 22, 1, color_gray_dark);

    for (int i = 0; i < 6; ++i) {
        int row_y = row_start_y + i * row_h;
        uint8_t row_fill = i == explorer_selected ? color_blue : color_white;
        uint8_t row_text = i == explorer_selected ? color_white : color_black;
        fill_rect(right_x - 2, row_y - 2, window->w - left_w - 22, 14, row_fill);
        draw_text_clipped(right_x, row_y, name_w - 6, names[i], row_text, row_fill, true);
        draw_text_clipped(right_x + name_w, row_y, type_w, types[i], row_text, row_fill, true);
    }

    draw_rect(right_x - 2, content_y + body_h, window->w - left_w - 22, 1, color_gray_dark);
    draw_text(right_x, content_y + body_h + 8, "Click an item to select it.", color_black, color_white, true);
    draw_text(right_x, content_y + body_h + 20, "Second click opens it.", color_black, color_white, true);
}

static void render_paint(const Window *window) {
    int canvas_x = window->x + 8;
    int canvas_y = window->y + 52;
    int canvas_w = PAINT_CANVAS_W;
    int canvas_h = PAINT_CANVAS_H;
    int palette_y = window->y + 26;
    int brush_y = window->y + 40;
    uint8_t colors[8] = {color_black, color_red, color_orange, color_yellow, color_green, color_blue, color_pink, color_white};

    fill_rect(canvas_x, canvas_y, canvas_w, canvas_h, color_white);
    draw_rect(canvas_x, canvas_y, canvas_w, canvas_h, color_gray_dark);

    for (int i = 0; i < 8; ++i) {
        fill_rect(canvas_x + i * 18, palette_y, 14, 8, colors[i]);
        draw_rect(canvas_x + i * 18, palette_y, 14, 8, color_black);
    }

    draw_text(window->x + 176, window->y + 28, "Brush", color_black, color_gray_light, true);
    for (int i = 0; i < 3; ++i) {
        int bx = window->x + 220 + i * 28;
        int size = i * 2 + 1;
        fill_rect(bx, brush_y - 2, 20, 12, paint_brush_size == (uint8_t)(i + 1) ? color_blue : color_white);
        draw_rect(bx, brush_y - 2, 20, 12, color_black);
        fill_rect(bx + 10 - size / 2, brush_y + 4 - size / 2, size, size, color_black);
    }

    for (int y = 0; y < canvas_h; ++y) {
        for (int x = 0; x < canvas_w; ++x) {
            draw_pixel(canvas_x + x, canvas_y + y, paint_canvas[y * PAINT_CANVAS_W + x]);
        }
    }

    draw_text(window->x + 176, window->y + 58, "Left click to draw.", color_black, color_gray_light, true);
    draw_text(window->x + 176, window->y + 72, "Paper sheet fills", color_black, color_gray_light, true);
    draw_text(window->x + 176, window->y + 84, "the whole page now.", color_black, color_gray_light, true);
}

static void render_snake(const Window *window) {
    char score[24] = {0};
    int ox = window->x + 20;
    int oy = window->y + 34;
    fill_rect(ox, oy, 200, 140, color_black);
    draw_rect(ox, oy, 200, 140, color_gray);
    for (int y = 0; y < 14; ++y) {
        for (int x = 0; x < 20; ++x) {
            draw_rect(ox + x * 10, oy + y * 10, 10, 10, color_gray_dark);
        }
    }
    for (int i = 0; i < snake_length; ++i) {
        fill_rect(ox + snake_x[i] * 10 + 1, oy + snake_y[i] * 10 + 1, 8, 8, i == 0 ? color_green : color_green_dark);
    }
    fill_rect(ox + snake_food_x * 10 + 1, oy + snake_food_y * 10 + 1, 8, 8, color_red);
    draw_text(window->x + 223, window->y + 46, "Use arrow", color_black, color_gray_light, true);
    draw_text(window->x + 223, window->y + 55, "keys.", color_black, color_gray_light, true);
    format_snake_score(score, sizeof(score));
    draw_text(window->x + 20, window->y + 182, "SCORE:", color_black, color_gray_light, true);
    draw_text(window->x + 76, window->y + 182, score, color_blue_dark, color_gray_light, true);
    if (snake_dead) {
        draw_text(window->x + 221, window->y + 90, "GAME OVER!", color_red, color_gray_light, true);
        draw_text(window->x + 222, window->y + 105, "ENTER to", color_red, color_gray_light, true);
        draw_text(window->x + 223, window->y + 115, "restart.", color_red, color_gray_light, true);
    }
}

static void render_guess(const Window *window) {
    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_white);
    draw_text(window->x + 14, window->y + 36, guess_message, color_black, color_white, true);
    draw_text(window->x + 14, window->y + 62, "Your guess:", color_black, color_white, true);
    fill_rect(window->x + 14, window->y + 76, 80, 18, color_gray_light);
    draw_rect(window->x + 14, window->y + 76, 80, 18, color_black);
    draw_text(window->x + 18, window->y + 81, guess_input, color_black, color_gray_light, true);
    draw_text(window->x + 14, window->y + 104, "Press ENTER to submit.", color_black, color_white, true);
}

static void render_mines(const Window *window) {
    int gx = window->x + 18;
    int gy = window->y + 34;
    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_gray_light);
    for (int y = 0; y < MINES_SIZE; ++y) {
        for (int x = 0; x < MINES_SIZE; ++x) {
            int cell_x = gx + x * 22;
            int cell_y = gy + y * 22;
            uint8_t fill = mines_revealed[y][x] ? color_white : color_gray;
            fill_rect(cell_x, cell_y, 20, 20, fill);
            draw_rect(cell_x, cell_y, 20, 20, color_black);
            if (mines_flagged[y][x]) {
                draw_text(cell_x + 6, cell_y + 6, "F", color_red, fill, true);
            } else if (mines_revealed[y][x]) {
                if (mines_value[y][x] == 9) {
                    draw_text(cell_x + 6, cell_y + 6, "*", color_red, fill, true);
                } else if (mines_value[y][x] > 0) {
                    char num[2] = {(char)('0' + mines_value[y][x]), '\0'};
                    draw_text(cell_x + 6, cell_y + 6, num, color_blue_dark, fill, true);
                }
            }
        }
    }
    if (mines_lost) {
        draw_text(window->x + 200, window->y + 42, "GAME OVER!", color_red, color_gray_light, true);
        draw_text(window->x + 200, window->y + 52, "*boom*", color_red, color_gray_light, true);
        draw_text(window->x + 200, window->y + 70, "Press ENTER", color_red, color_gray_light, true);
        draw_text(window->x + 200, window->y + 80, "to restart", color_red, color_gray_light, true);
    } else if (mines_won) {
        draw_text(window->x + 200, window->y + 42, "You win!", color_green, color_gray_light, true);
        draw_text(window->x + 200, window->y + 60, "Press ENTER", color_green, color_gray_light, true);
        draw_text(window->x + 200, window->y + 70, "to replay", color_green, color_gray_light, true);
    } else {
        draw_text(window->x + 200, window->y + 42, "LC=Reveal", color_black, color_gray_light, true);
        draw_text(window->x + 200, window->y + 52, "LR=Flag", color_black, color_gray_light, true);
    }
}

static void render_game_center(const Window *window) {
    static const char *games[] = {"Minesweeper", "Snake", "Guess Number"};
    int header_icon_w = image_width(_binary_build_game_icon_bin_start);
    int header_text_w = text_pixel_width("Game Center");
    int header_x = window->x + (window->w - (header_icon_w + 8 + header_text_w)) / 2;
    int list_y = window->y + 112;

    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_white);
    draw_image_at(_binary_build_game_icon_bin_start, header_x, window->y + 36, true);
    draw_text(header_x + header_icon_w + 8, window->y + 48, "Game Center", color_black, color_white, true);
    draw_text(window->x + 20, window->y + 80, "Welcome games! Choose one to play:", color_black, color_white, true);

    for (int i = 0; i < 3; ++i) {
        int row_y = list_y + i * 34;
        draw_image_at(_binary_build_program_icon_bin_start, window->x + 24, row_y, true);
        draw_text(window->x + 64, row_y + 12, games[i], color_black, color_white, true);
    }
}

static void render_power(const Window *window) {
    int x = window->x + 20;
    int y = window->y + 36;
    draw_button(x, y, 160, 24, "Shutdown", color_gray_light, color_black, color_black);
    draw_button(x, y + 32, 160, 24, "Restart", color_gray_light, color_black, color_black);
    draw_button(x, y + 64, 160, 24, "Halt", color_gray_light, color_black, color_black);
    draw_button(x, y + 96, 160, 24, "x Close", color_gray_light, color_black, color_black);
}

static void render_settings(const Window *window) {
    char resolution[24] = {0};
    char current_mode[24] = {0};
    int icon_w = image_width(_binary_build_settings_icon_bin_start);
    int header_text_w = text_pixel_width("Settings");
    int header_x = window->x + (window->w - (icon_w + 8 + header_text_w)) / 2;
    bool dirty = settings_dirty();
    resolution_string(resolution, sizeof(resolution), &settings_pending);
    framebuffer_mode_string(current_mode, sizeof(current_mode), fb.width, fb.height, fb.bpp);

    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_white);
    draw_image_at(_binary_build_settings_icon_bin_start, header_x, window->y + 24, true);
    draw_text(header_x + icon_w + 8, window->y + 36, "Settings", color_black, color_white, true);
    draw_button(window->x + 14, window->y + 56, 70, 20, "Video", settings_tab == 0 ? color_blue : color_gray_light, color_black, settings_tab == 0 ? color_white : color_black);
    draw_button(window->x + 90, window->y + 56, 80, 20, "Desktop", settings_tab == 1 ? color_blue : color_gray_light, color_black, settings_tab == 1 ? color_white : color_black);

    if (settings_tab == 0) {
        draw_text(window->x + 20, window->y + 92, "Set to Color Modes:", color_black, color_white, true);
        draw_button(window->x + 178, window->y + 86, 20, 18, "<", live_palette_supported(settings_pending.palette_mode) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 202, window->y + 86, 140, 18, palette_name(settings_pending.palette_mode), live_palette_supported(settings_pending.palette_mode) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 346, window->y + 86, 20, 18, ">", live_palette_supported(settings_pending.palette_mode) ? color_gray_light : color_gray, color_black, color_black);

        draw_text(window->x + 20, window->y + 122, "Screen Resolution:", color_black, color_white, true);
        draw_button(window->x + 178, window->y + 116, 20, 18, "<", live_resolution_supported(settings_pending.resolution_mode) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 202, window->y + 116, 140, 18, resolution_name(settings_pending.resolution_mode), live_resolution_supported(settings_pending.resolution_mode) ? color_gray_light : color_gray, color_black, color_black);
        draw_button(window->x + 346, window->y + 116, 20, 18, ">", live_resolution_supported(settings_pending.resolution_mode) ? color_gray_light : color_gray, color_black, color_black);

        fill_rect(window->x + 20, window->y + 148, 12, 12, settings_pending.widescreen ? color_green : color_white);
        draw_rect(window->x + 20, window->y + 148, 12, 12, color_black);
        draw_text(window->x + 40, window->y + 150, "Set to Widescreen", color_black, color_white, true);
        draw_text(window->x + 20, window->y + 174, "Total Screen:", color_black, color_white, true);
        draw_text(window->x + 128, window->y + 174, resolution, color_blue_dark, color_white, true);
        draw_text(window->x + 20, window->y + 192, "Current Output:", color_black, color_white, true);
        draw_text(window->x + 143, window->y + 192, current_mode, color_blue_dark, color_white, true);
        draw_text(window->x + 20, window->y + 206, video_mode_switch_available ? "Apply switches the video mode." : "ERROR: video mode switch is unavailable!", color_black, color_white, true);
    } else {
        draw_text(window->x + 20, window->y + 92, "Desktop Background", color_black, color_white, true);
        draw_button(window->x + 170, window->y + 86, 20, 18, "<", color_gray_light, color_black, color_black);
        draw_button(window->x + 194, window->y + 86, 160, 18, background_name(settings_pending.background_mode), color_gray_light, color_black, color_black);
        draw_button(window->x + 358, window->y + 86, 20, 18, ">", color_gray_light, color_black, color_black);
        draw_text(window->x + 20, window->y + 120, "Import as Custom is reserved", color_black, color_white, true);
        draw_text(window->x + 20, window->y + 132, "for a later file browser build.", color_black, color_white, true);
    }

    draw_button(window->x + 120, window->y + window->h - 36, 60, 22, "Cancel", dirty ? color_gray_light : color_gray, color_black, color_black);
    draw_button(window->x + 188, window->y + window->h - 36, 60, 22, "Apply", dirty ? color_gray_light : color_gray, color_black, color_black);
    draw_button(window->x + 256, window->y + window->h - 36, 60, 22, "OK", dirty ? color_gray_light : color_gray, color_black, color_black);
}

static void render_task_manager(const Window *window) {
    char cpu_speed[32] = {0};
    char ram_total[32] = {0};
    char ram_used[32] = {0};
    char gpu_memory[32] = {0};
    char disk_io[24] = {0};
    char temp[24] = {0};
    char page_text[24] = {0};
    int process_count = task_manager_process_count();
    int content_x = window->x + 18;
    int grid_x = content_x;
    int grid_y = window->y + 64;
    int scroll_x = grid_x + 132;
    uint32_t total_gpu_entries = gpu_palette_entry_count();
    uint32_t max_gpu_scroll = gpu_palette_max_scroll();

    if (task_manager_selected_process >= process_count) {
        task_manager_selected_process = process_count > 0 ? process_count - 1 : 0;
    }
    if (gpu_palette_scroll > max_gpu_scroll) {
        gpu_palette_scroll = max_gpu_scroll;
    }

    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_white);
    draw_button(window->x + 14, window->y + 30, 84, 20, "Processes", task_manager_tab == 0 ? color_blue : color_gray_light, color_black, task_manager_tab == 0 ? color_white : color_black);
    draw_button(window->x + 104, window->y + 30, 96, 20, "Performance", task_manager_tab == 1 ? color_blue : color_gray_light, color_black, task_manager_tab == 1 ? color_white : color_black);
    draw_button(window->x + 206, window->y + 30, 54, 20, "GPU", task_manager_tab == 2 ? color_blue : color_gray_light, color_black, task_manager_tab == 2 ? color_white : color_black);

    if (task_manager_tab == 0) {
        int list_x = window->x + 18;
        int list_y = window->y + 62;
        int list_w = window->w - 36;
        int rows = process_count;

        fill_rect(list_x, list_y, list_w, 162, color_gray_light);
        draw_rect(list_x, list_y, list_w, 162, color_gray_dark);
        draw_text(list_x + 6, list_y + 6, "Process", color_blue_dark, color_gray_light, true);
        draw_text(list_x + 210, list_y + 6, "Type", color_blue_dark, color_gray_light, true);

        for (int i = 0; i < rows && i < 9; ++i) {
            const char *name;
            const char *type;
            AppId app;
            bool killable = task_manager_process_info(i, &name, &type, &app);
            int row_y = list_y + 24 + i * 15;
            uint8_t fill = i == task_manager_selected_process ? color_blue : color_white;
            uint8_t text = i == task_manager_selected_process ? color_white : (killable ? color_black : color_blue_dark);
            fill_rect(list_x + 4, row_y - 2, list_w - 8, 13, fill);
            draw_text_clipped(list_x + 8, row_y, 194, name, text, fill, true);
            draw_text_clipped(list_x + 210, row_y, 140, type, text, fill, true);
        }

        {
            AppId selected_app;
            bool killable = task_manager_selected_is_killable(&selected_app);
            draw_button(window->x + 18, window->y + 232, 100, 22, "Kill Process", killable ? color_gray_light : color_gray, color_black, color_black);
        }
    } else if (task_manager_tab == 1) {
        format_cpu_speed(cpu_speed, sizeof(cpu_speed));
        format_ram_total(ram_total, sizeof(ram_total));
        format_ram_used(ram_used, sizeof(ram_used));
        format_gpu_memory(gpu_memory, sizeof(gpu_memory));
        format_disk_io(disk_io, sizeof(disk_io));

        draw_text(window->x + 20, window->y + 62, "CPU:", color_black, color_white, true);
        draw_usage_graph(window->x + 20, window->y + 74, cpu_usage_history, color_green);
        draw_text(window->x + 104, window->y + 76, "CPU Usage :", color_black, color_white, true);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), cpu_usage_percent);
            append_char(temp, &len, sizeof(temp), '%');
        }
        draw_text(window->x + 200, window->y + 76, temp, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 90, "CPU Speed :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 90, cpu_speed, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 104, "CPU State :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 104, cpu_state_label(), cpu_state_color(), color_white, true);

        draw_text(window->x + 20, window->y + 128, "RAM:", color_black, color_white, true);
        draw_usage_graph(window->x + 20, window->y + 140, ram_usage_history, color_yellow);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), ram_usage_percent);
            append_char(temp, &len, sizeof(temp), '%');
        }
        draw_text(window->x + 104, window->y + 142, "RAM Usage :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 142, temp, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 156, "RAM Bytes :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 156, ram_total, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 170, "RAM Used  :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 170, ram_used, color_blue_dark, color_white, true);

        draw_text(window->x + 20, window->y + 194, "GPU:", color_black, color_white, true);
        draw_usage_graph(window->x + 20, window->y + 206, gpu_usage_history, color_orange);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), gpu_usage_percent);
            append_char(temp, &len, sizeof(temp), '%');
        }
        draw_text(window->x + 104, window->y + 208, "GPU Usage :", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 208, temp, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 222, "GPU Memory:", color_black, color_white, true);
        draw_text(window->x + 200, window->y + 222, gpu_memory, color_blue_dark, color_white, true);

        draw_text(window->x + 20, window->y + 254, "Disk:", color_black, color_white, true);
        draw_usage_graph(window->x + 20, window->y + 266, disk_usage_history, color_white);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), disk_usage_percent);
            append_char(temp, &len, sizeof(temp), '%');
        }
        draw_text(window->x + 104, window->y + 268, "Disk Usage        :", color_black, color_white, true);
        draw_text(window->x + 262, window->y + 268, temp, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 282, "Disk I/O Activity :", color_black, color_white, true);
        draw_text(window->x + 262, window->y + 282, disk_io, color_blue_dark, color_white, true);
        draw_text(window->x + 104, window->y + 296, "Disk Physical Type:", color_black, color_white, true);
        draw_text(window->x + 262, window->y + 296, disk_physical_type_label(), color_blue_dark, color_white, true);
    } else {
        draw_text(window->x + 20, window->y + 62, "GPU:", color_black, color_white, true);
        fill_rect(grid_x - 2, grid_y - 2, 132, 132, color_gray_light);
        draw_rect(grid_x - 2, grid_y - 2, 132, 132, color_gray_dark);
        for (int index = 0; index < GPU_GRID_PAGE_ENTRIES; ++index) {
            uint32_t entry = gpu_palette_scroll + (uint32_t)index;
            int gx = grid_x + (index % 16) * 8;
            int gy = grid_y + (index / 16) * 8;
            if (entry < total_gpu_entries) {
                fill_rect(gx, gy, 7, 7, gpu_palette_entry_color(entry));
                draw_rect(gx, gy, 7, 7, color_black);
            } else {
                fill_rect(gx, gy, 7, 7, color_gray_light);
                draw_rect(gx, gy, 7, 7, color_gray_dark);
                draw_pixel(gx + 1, gy + 1, color_red);
                draw_pixel(gx + 5, gy + 5, color_red);
                draw_pixel(gx + 1, gy + 5, color_red);
                draw_pixel(gx + 5, gy + 1, color_red);
            }
        }

        draw_button(scroll_x, grid_y - 2, 16, 16, "^", gpu_palette_scroll > 0 ? color_gray_light : color_gray, color_black, color_black);
        fill_rect(scroll_x, grid_y + 16, 16, 98, color_gray_light);
        draw_rect(scroll_x, grid_y + 16, 16, 98, color_gray_dark);
        if (total_gpu_entries > GPU_GRID_PAGE_ENTRIES) {
            int thumb_h = 20;
            int thumb_track = 98 - thumb_h;
            int thumb_y = grid_y + 17 + (int)((gpu_palette_scroll * (uint32_t)thumb_track) / max_gpu_scroll);
            fill_rect(scroll_x + 2, thumb_y, 12, thumb_h, color_blue);
            draw_rect(scroll_x + 2, thumb_y, 12, thumb_h, color_black);
        } else {
            fill_rect(scroll_x + 2, grid_y + 18, 12, 20, color_gray);
            draw_rect(scroll_x + 2, grid_y + 18, 12, 20, color_black);
        }
        draw_button(scroll_x, grid_y + 114, 16, 16, "v", gpu_palette_scroll < max_gpu_scroll ? color_gray_light : color_gray, color_black, color_black);

        page_text[0] = '\0';
        {
            size_t len = 0;
            uint32_t current_page = (gpu_palette_scroll / GPU_GRID_PAGE_ENTRIES) + 1u;
            uint32_t page_count = (total_gpu_entries + GPU_GRID_PAGE_ENTRIES - 1u) / GPU_GRID_PAGE_ENTRIES;
            append_uint(page_text, &len, sizeof(page_text), current_page);
            append_char(page_text, &len, sizeof(page_text), '/');
            append_uint(page_text, &len, sizeof(page_text), page_count);
        }

        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), total_gpu_entries);
        }
        draw_text(grid_x + 2, grid_y + 240, "Total GPU Palettes:", color_black, color_white, true);
        draw_text(grid_x + 160, grid_y + 240, temp, color_blue_dark, color_white, true);
        temp[0] = '\0';
        {
            size_t len = 0;
            append_uint(temp, &len, sizeof(temp), fb.bpp);
        }
        draw_text(grid_x + 2, grid_y + 252, "Video Bit Depth   :", color_black, color_white, true);
        draw_text(grid_x + 160, grid_y + 252, temp, color_blue_dark, color_white, true);
        draw_text(grid_x + 2, grid_y + 270, "Palette Page      :", color_black, color_white, true);
        draw_text(grid_x + 160, grid_y + 270, page_text, color_blue_dark, color_white, true);
    }

    if (task_manager_confirm_kill) {
        int box_x = window->x + 90;
        int box_y = window->y + 92;
        fill_rect(box_x, box_y, 250, 96, color_gray_light);
        draw_rect(box_x, box_y, 250, 96, color_black);
        draw_text_center(box_x + 125, box_y + 18, "Kill selected process?", color_black, color_gray_light, true);
        draw_text_center(box_x + 125, box_y + 34, "This will close the window.", color_black, color_gray_light, true);
        draw_button(box_x + 34, box_y + 60, 78, 22, "Proceed", color_gray_light, color_black, color_black);
        draw_button(box_x + 138, box_y + 60, 78, 22, "Cancel", color_gray_light, color_black, color_black);
    }
}

static void render_app_window(AppId app) {
    Window *window = &windows[app];
    if (!window->open) {
        return;
    }

    draw_window_chrome(window);

    switch (app) {
        case APP_NOTEPAD: render_notepad(window); break;
        case APP_CMD: render_terminal(&cmd_term, window->x + 8, window->y + 26, window->w - 16, window->h - 34); break;
        case APP_PAINT: render_paint(window); break;
        case APP_EXPLORER: render_explorer(window); break;
        case APP_SNAKE: render_snake(window); break;
        case APP_GUESS: render_guess(window); break;
        case APP_MINES: render_mines(window); break;
        case APP_GAME_CENTER: render_game_center(window); break;
        case APP_POWER: render_power(window); break;
        case APP_SETTINGS: render_settings(window); break;
        case APP_TASK_MANAGER: render_task_manager(window); break;
    }
}

static void render_taskbar(void) {
    char datetime[40] = {0};
    int tab_x = 58;

    fill_rect(0, OS_HEIGHT - TASKBAR_H, OS_WIDTH, TASKBAR_H, color_gray_dark);
    draw_rect(0, OS_HEIGHT - TASKBAR_H, OS_WIDTH, TASKBAR_H, color_black);
    draw_button(4, OS_HEIGHT - 24, 46, 18, "MENU", color_green, color_black, color_black);

    for (int app = 0; app < APP_COUNT; ++app) {
        if (!windows[app].open) {
            continue;
        }
        draw_button(tab_x, OS_HEIGHT - 24, 84, 18, app_titles[app], active_window == app ? color_blue : color_gray_light, color_black, active_window == app ? color_white : color_black);
        tab_x += 88;
    }

    read_datetime(datetime, sizeof(datetime));
    draw_text(OS_WIDTH - (int)strlen_local(datetime) * 8 - 8, OS_HEIGHT - 20, datetime, color_white, color_gray_dark, true);
}

static void render_start_menu(void) {
    if (!menu_open) {
        return;
    }
    int x = 0;
    int y = OS_HEIGHT - TASKBAR_H - 230;
    fill_rect(x, y, 180, 230, color_gray_light);
    draw_rect(x, y, 180, 230, color_black);
    draw_text(x + 8, y + 8, "Tools:", color_blue_dark, color_gray_light, true);
    draw_text(x + 8, y + 24, "Notepad", color_black, color_gray_light, true);
    draw_text(x + 8, y + 40, "Command Prompt", color_black, color_gray_light, true);
    draw_text(x + 8, y + 56, "Paint", color_black, color_gray_light, true);
    draw_text(x + 8, y + 72, "Explorer", color_black, color_gray_light, true);
    draw_text(x + 8, y + 88, "Task Manager", color_black, color_gray_light, true);
    draw_text(x + 8, y + 112, "Games:", color_blue_dark, color_gray_light, true);
    draw_text(x + 8, y + 128, "Game Center", color_black, color_gray_light, true);
    draw_text(x + 8, y + 160, "Options:", color_blue_dark, color_gray_light, true);
    draw_text(x + 8, y + 176, "Power Options", color_black, color_gray_light, true);
    draw_text(x + 8, y + 192, "Settings", color_black, color_gray_light, true);
    draw_text(x + 8, y + 208, "x Close", color_red, color_gray_light, true);
}

static void render_context_menu(void) {
    if (!context_menu_open) {
        return;
    }
    fill_rect(context_menu_x, context_menu_y, 120, 60, color_gray_light);
    draw_rect(context_menu_x, context_menu_y, 120, 60, color_black);
    draw_text(context_menu_x + 8, context_menu_y + 8, "Refresh", color_black, color_gray_light, true);
    draw_text(context_menu_x + 8, context_menu_y + 24, "Explorer", color_black, color_gray_light, true);
    draw_text(context_menu_x + 8, context_menu_y + 40, "Settings", color_black, color_gray_light, true);
}

static void render_desktop_icons(void) {
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        draw_desktop_icon(i, i == selected_desktop_icon);
    }
}

static void render_cursor(void) {
    /* black outline */
    static const uint16_t outline[12] = {
        0b1000000000000000,
        0b1100000000000000,
        0b1110000000000000,
        0b1111000000000000,
        0b1111100000000000,
        0b1111110000000000,
        0b1111111000000000,
        0b1111111100000000,
        0b1111100000000000,
        0b1101100000000000,
        0b1000110000000000,
        0b0000011000000000
    };

    /* white interior */
    static const uint16_t fill[12] = {
        0b0000000000000000,
        0b0100000000000000,
        0b0110000000000000,
        0b0111000000000000,
        0b0111100000000000,
        0b0111110000000000,
        0b0111111000000000,
        0b0111111100000000,
        0b0111100000000000,
        0b0100100000000000,
        0b0000010000000000,
        0b0000000000000000
    };

    /* draw outline */
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 16; ++col) {
            if (outline[row] & (1u << (15 - col))) {
                draw_pixel(mouse.x + col, mouse.y + row, color_black);
            }
        }
    }

    /* draw fill */
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 16; ++col) {
            if (fill[row] & (1u << (15 - col))) {
                draw_pixel(mouse.x + col, mouse.y + row, color_white);
            }
        }
    }
}

static void render_boot_terminal_text(void) {
    uint32_t blink_phase = timer_ticks / TERMINAL_CURSOR_BLINK_TICKS;

    if (!boot_terminal_dirty && blink_phase == boot_terminal_last_blink) {
        return;
    }

    const int top_row = 4;
    const int max_rows = 18;
    int start = boot_term.line_count > max_rows ? boot_term.line_count - max_rows : 0;
    int row = top_row;

    vga_text_clear(VGA_TEXT_ATTR_GRAY);
    draw_text_mode_row(0, 0, "HaloxOS Command Prompt", VGA_TEXT_ATTR_GRAY);
    draw_text_mode_row(1, 0, "Type 'boot' to start the desktop. ESC returns to boot menu.", VGA_TEXT_ATTR_GRAY);

    for (int i = start; i < boot_term.line_count && row < top_row + max_rows; ++i, ++row) {
        draw_text_mode_row(row, 0, boot_term.lines[i], VGA_TEXT_ATTR_GRAY);
    }

    draw_text_mode_row(23, 0, "A:\\>", VGA_TEXT_ATTR_GRAY);
    draw_text_mode_row(23, 4, boot_term.input, VGA_TEXT_ATTR_GRAY);
    vga_text_enable_cursor(14, 15);
    vga_text_set_cursor(clampi(4 + boot_term.input_len, 0, VGA_TEXT_COLS - 1), 23);
    boot_terminal_dirty = false;
    boot_terminal_last_blink = blink_phase;
}

static void render_boot_menu(void) {
    if (!boot_menu_dirty) {
        return;
    }
    vga_text_clear(VGA_TEXT_ATTR_GRAY);
    vga_text_disable_cursor();
    draw_text_mode_center(8, "Hello World! Greetings HaloxOS!", VGA_TEXT_ATTR_BLUE);
    draw_text_mode_center(9, "Select an option to choose boot:", VGA_TEXT_ATTR_BLUE);
    draw_text_mode_center(12, "1) Main Boot", VGA_TEXT_ATTR_GRAY);
    draw_text_mode_center(14, "2) Command Prompt", VGA_TEXT_ATTR_GRAY);
    if (boot_status_text[0] != '\0') {
        draw_text_mode_center(21, boot_status_text, VGA_TEXT_ATTR_GRAY);
    }
    boot_menu_dirty = false;
}

static void render_login(void) {
    const char *line1 = "Welcome! Enter to your login access:";
    const char *line2 = "Press ENTER to start";
    const char *line3 = "Press ESC to shutdown";
    int icon_w = image_width(_binary_build_user_frame_bin_start);
    int text_w = text_pixel_width(line1);
    int text_x;
    int icon_x;

    if (text_pixel_width(line2) > text_w) {
        text_w = text_pixel_width(line2);
    }
    if (text_pixel_width(line3) > text_w) {
        text_w = text_pixel_width(line3);
    }

    icon_x = (OS_WIDTH - (icon_w + 16 + text_w)) / 2;
    text_x = icon_x + icon_w + 16;

    draw_image(_binary_build_login_bin_start);
    draw_image_at(_binary_build_user_frame_bin_start, icon_x, 182, true);
    draw_text(text_x, 190, line1, color_black, 0, true);
    draw_text(text_x, 216, line2, color_black, 0, true);
    draw_text(text_x, 227, line3, color_black, 0, true);
    draw_text(505, 10, HALOXOS_BUILD_TEXT, color_black, color_gray_light, true);
    draw_text_center(OS_WIDTH / 2, 460, "For Development Purposes only!", color_black, color_gray_light, true);
    draw_text_center(OS_WIDTH / 2, 450, "Available here! https://github.com/svh03ra/HaloxOS", color_black, color_gray_light, true);
}

static void render_desktop(void) {
    draw_desktop_background();
    render_desktop_icons();
    render_taskbar();
    render_start_menu();
    render_context_menu();
    for (int app = 0; app < APP_COUNT; ++app) {
        if (app == active_window) {
            continue;
        }
        render_app_window((AppId)app);
    }
    if (active_window >= 0 && active_window < APP_COUNT) {
        render_app_window((AppId)active_window);
    }

    if (cpu_halted_overlay) {
        fill_rect(160, 180, 320, 80, color_gray_light);
        draw_rect(160, 180, 320, 80, color_black);
        draw_text_center(OS_WIDTH / 2, 215, "Halting CPU... *boom* DONE!", color_black, color_gray_light, true);
    }

    render_cursor();
    draw_text(438, 430,  "HaloxOS Version 1.0D", color_black, color_gray_light, true);
    draw_text(438, 440,  "For Testing purposes only",             color_black, color_gray_light, true);
}

static void render_shutdown(void) {
    if (boot_text_mode) {
        vga_text_clear(VGA_TEXT_ATTR_GRAY);
        vga_text_disable_cursor();
        draw_text_mode_center(12, "Shutting down...", VGA_TEXT_ATTR_GRAY);
    } else {
        clear_screen(color_black);
        draw_text_center_scaled(OS_WIDTH / 2, 212, "Shutting down...", color_white, color_black, true, 2);
    }
}

static void draw_everything(void) {
    switch (system_state) {
        case STATE_BOOT_MENU: render_boot_menu(); break;
        case STATE_BOOT_TERMINAL: render_boot_terminal_text(); break;
        case STATE_LOGIN: render_login(); break;
        case STATE_DESKTOP: render_desktop(); break;
        case STATE_SHUTDOWN: render_shutdown(); break;
    }
}

static void handle_boot_menu_keys(KeyEvent event) {
    if (event.ch == '1') {
        clear_boot_status();
        enter_main_graphics_mode();
        if (!boot_text_mode) {
            system_state = STATE_LOGIN;
        } else {
            set_boot_status("ERROR: Graphics mode unavailable!!!");
        }
    } else if (event.ch == '2') {
        clear_boot_status();
        enter_boot_text_mode();
        system_state = STATE_BOOT_TERMINAL;
        boot_terminal_dirty = true;
    }
}

static void handle_login_keys(KeyEvent event) {
    if (event.code == KEY_ENTER) {
        open_desktop();
    } else if (event.code == KEY_ESC) {
        shutdown_system();
    }
}

static void handle_start_menu_click(void) {
    int x = 0;
    int y = OS_HEIGHT - TASKBAR_H - 230;

    if (!menu_open || !mouse.left || mouse.prev_left) {
        return;
    }

    if (!point_in_rect(mouse.x, mouse.y, x, y, 180, 230)) {
        menu_open = false;
        return;
    }

    int row = (mouse.y - y - 24) / 16;
    switch (row) {
        case 0: open_window(APP_NOTEPAD); break;
        case 1: open_window(APP_CMD); break;
        case 2: open_window(APP_PAINT); break;
        case 3: open_window(APP_EXPLORER); break;
        case 4: open_window(APP_TASK_MANAGER); break;
        case 6: open_window(APP_GAME_CENTER); break;
        case 9: open_window(APP_POWER); break;
        case 10: open_window(APP_SETTINGS); break;
        case 11: menu_open = false; break;
        default: break;
    }
}

static void handle_context_menu_click(void) {
    if (!context_menu_open || !mouse.left || mouse.prev_left) {
        return;
    }

    if (!point_in_rect(mouse.x, mouse.y, context_menu_x, context_menu_y, 120, 60)) {
        context_menu_open = false;
        return;
    }

    int row = (mouse.y - context_menu_y) / 16;
    if (row == 0) {
        context_menu_open = false;
    } else if (row == 1) {
        open_window(APP_EXPLORER);
        context_menu_open = false;
    } else if (row == 2) {
        open_window(APP_SETTINGS);
        context_menu_open = false;
    }
}

static void handle_desktop_mouse(void) {
    bool pointer_over_window = false;
    bool clicked = mouse.left && !mouse.prev_left;

    if (button_clicked(4, OS_HEIGHT - 24, 46, 18)) {
        menu_open = !menu_open;
        context_menu_open = false;
        return;
    }

    for (int app = 0, tab_x = 58; app < APP_COUNT; ++app) {
        if (!windows[app].open) {
            continue;
        }
        if (button_clicked(tab_x, OS_HEIGHT - 24, 84, 18)) {
            set_active_window((AppId)app);
            return;
        }
        tab_x += 88;
    }

    handle_start_menu_click();
    handle_context_menu_click();

    if (mouse.left && drag_desktop_icon >= 0 && drag_desktop_icon < DESKTOP_ICON_COUNT) {
        int box_x;
        int box_y;
        int box_w;
        int box_h;

        desktop_icon_bounds(0, 0, desktop_icon_image(drag_desktop_icon), desktop_icon_label(drag_desktop_icon),
                            &box_x, &box_y, &box_w, &box_h);
        desktop_icons[drag_desktop_icon].x = clampi(mouse.x - desktop_icon_drag_offset_x, 0, OS_WIDTH - box_w);
        desktop_icons[drag_desktop_icon].y = clampi(mouse.y - desktop_icon_drag_offset_y, 0, OS_HEIGHT - TASKBAR_H - box_h);
        return;
    }

    if (mouse.left && drag_window >= 0 && drag_window < APP_COUNT && windows[drag_window].open) {
        Window *window = &windows[drag_window];
        window->x = clampi(mouse.x - drag_offset_x, 0, OS_WIDTH - window->w);
        window->y = clampi(mouse.y - drag_offset_y, 0, OS_HEIGHT - TASKBAR_H - window->h);
        return;
    }

    if (!mouse.left) {
        drag_window = -1;
        if (drag_desktop_icon >= 0 && desktop_icon_drag_moved) {
            save_desktop_icon_positions();
        } else if (desktop_icon_press >= 0 && desktop_icon_press_was_selected) {
            open_window(desktop_icon_app(desktop_icon_press));
            desktop_icon_press = -1;
            drag_desktop_icon = -1;
            desktop_icon_drag_moved = false;
            desktop_icon_press_was_selected = false;
            return;
        }
        desktop_icon_press = -1;
        drag_desktop_icon = -1;
        desktop_icon_drag_moved = false;
        desktop_icon_press_was_selected = false;
    }

    {
        int first = active_window;
        for (int pass = 0; pass <= APP_COUNT; ++pass) {
            int app = pass == 0 ? first : (APP_COUNT - pass);
            Window *window;

            if (app < 0 || app >= APP_COUNT) {
                continue;
            }
            if (pass > 0 && app == first) {
                continue;
            }

            window = &windows[app];
            if (!window->open) {
                continue;
            }
            if (!point_in_rect(mouse.x, mouse.y, window->x, window->y, window->w, window->h)) {
                continue;
            }

            pointer_over_window = true;
            if (clicked) {
                if (point_in_rect(mouse.x, mouse.y, window->x + window->w - 18, window->y + 3, 12, 12)) {
                    close_window((AppId)app);
                    return;
                }
                if (point_in_rect(mouse.x, mouse.y, window->x, window->y, window->w, 18)) {
                    set_active_window((AppId)app);
                    drag_window = app;
                    drag_offset_x = mouse.x - window->x;
                    drag_offset_y = mouse.y - window->y;
                    return;
                }
                if (active_window != app) {
                    set_active_window((AppId)app);
                    return;
                }
            }
            break;
        }
    }

    if (!pointer_over_window && button_right_clicked(0, 0, OS_WIDTH, OS_HEIGHT - TASKBAR_H)) {
        context_menu_open = true;
        menu_open = false;
        context_menu_x = clampi(mouse.x, 0, OS_WIDTH - 120);
        context_menu_y = clampi(mouse.y, 0, OS_HEIGHT - TASKBAR_H - 60);
        return;
    }

    if (!pointer_over_window) {
        int hit = desktop_icon_hit_test(mouse.x, mouse.y);

        if (clicked && hit >= 0) {
            desktop_icon_press = hit;
            desktop_icon_press_x = mouse.x;
            desktop_icon_press_y = mouse.y;
            desktop_icon_drag_offset_x = mouse.x - desktop_icons[hit].x;
            desktop_icon_drag_offset_y = mouse.y - desktop_icons[hit].y;
            desktop_icon_drag_moved = false;
            desktop_icon_press_was_selected = selected_desktop_icon == hit;
            selected_desktop_icon = hit;
            return;
        }

        if (mouse.left && desktop_icon_press >= 0) {
            int dx = mouse.x - desktop_icon_press_x;
            int dy = mouse.y - desktop_icon_press_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > 2 || dy > 2) {
                drag_desktop_icon = desktop_icon_press;
                desktop_icon_drag_moved = true;
            }
            if (drag_desktop_icon >= 0) {
                int box_x;
                int box_y;
                int box_w;
                int box_h;

                desktop_icon_bounds(0, 0, desktop_icon_image(drag_desktop_icon), desktop_icon_label(drag_desktop_icon),
                                    &box_x, &box_y, &box_w, &box_h);
                desktop_icons[drag_desktop_icon].x = clampi(mouse.x - desktop_icon_drag_offset_x, 0, OS_WIDTH - box_w);
                desktop_icons[drag_desktop_icon].y = clampi(mouse.y - desktop_icon_drag_offset_y, 0, OS_HEIGHT - TASKBAR_H - box_h);
                return;
            }
        } else if (clicked && hit < 0) {
            selected_desktop_icon = -1;
            return;
        }
    }

    if (active_window == APP_PAINT && windows[APP_PAINT].open) {
        Window *window = &windows[APP_PAINT];
        int canvas_x = window->x + 8;
        int canvas_y = window->y + 52;
        uint8_t colors[8] = {color_black, color_red, color_orange, color_yellow, color_green, color_blue, color_pink, color_white};
        for (int i = 0; i < 8; ++i) {
            if (button_clicked(canvas_x + i * 18, window->y + 24, 14, 8)) {
                paint_color = colors[i];
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (button_clicked(window->x + 220 + i * 28, window->y + 38, 20, 12)) {
                paint_brush_size = (uint8_t)(i + 1);
                set_active_window(APP_PAINT);
            }
        }
        if (point_in_rect(mouse.x, mouse.y, canvas_x, canvas_y, PAINT_CANVAS_W, PAINT_CANVAS_H) && mouse.left) {
            int local_x = mouse.x - canvas_x;
            int local_y = mouse.y - canvas_y;
            int radius = (int)paint_brush_size - 1;
            for (int oy = -radius; oy <= radius; ++oy) {
                for (int ox = -radius; ox <= radius; ++ox) {
                    int px = local_x + ox;
                    int py = local_y + oy;
                    if (px >= 0 && py >= 0 && px < PAINT_CANVAS_W && py < PAINT_CANVAS_H) {
                        paint_canvas[py * PAINT_CANVAS_W + px] = paint_color;
                    }
                }
            }
            set_active_window(APP_PAINT);
        }
    }

    if (active_window == APP_EXPLORER && windows[APP_EXPLORER].open) {
        Window *window = &windows[APP_EXPLORER];
        int row_x = window->x + 96;
        int row_w = window->w - 110;
        for (int i = 0; i < 6; ++i) {
            int row_y = window->y + 52 + i * 16;
            if (button_clicked(row_x, row_y - 2, row_w, 14)) {
                set_active_window(APP_EXPLORER);
                if (explorer_selected == i) {
                    switch (i) {
                        case 0: break;
                        case 1: open_window(APP_NOTEPAD); break;
                        case 2: open_window(APP_CMD); break;
                        case 3: open_window(APP_PAINT); break;
                        case 4: open_window(APP_GAME_CENTER); break;
                        case 5: open_window(APP_SETTINGS); break;
                        default: break;
                    }
                } else {
                    explorer_selected = i;
                }
            }
        }
    }

    if (active_window == APP_MINES && windows[APP_MINES].open) {
        Window *window = &windows[APP_MINES];
        int gx = window->x + 18;
        int gy = window->y + 34;
        if (point_in_rect(mouse.x, mouse.y, gx, gy, MINES_SIZE * 22, MINES_SIZE * 22)) {
            int cell_x = (mouse.x - gx) / 22;
            int cell_y = (mouse.y - gy) / 22;
            if (mouse.left && !mouse.prev_left && !mines_lost && !mines_won) {
                set_active_window(APP_MINES);
                if (mines_value[cell_y][cell_x] == 9) {
                    mines_lost = true;
                    for (int y = 0; y < MINES_SIZE; ++y) {
                        for (int x = 0; x < MINES_SIZE; ++x) {
                            if (mines_value[y][x] == 9) {
                                mines_revealed[y][x] = true;
                            }
                        }
                    }
                } else {
                    reveal_mines(cell_x, cell_y);
                    update_mines_win();
                }
            } else if (mouse.right && !mouse.prev_right && !mines_lost && !mines_won) {
                set_active_window(APP_MINES);
                if (!mines_revealed[cell_y][cell_x]) {
                    mines_flagged[cell_y][cell_x] = !mines_flagged[cell_y][cell_x];
                }
            }
        }
    }

    if (windows[APP_GAME_CENTER].open && active_window == APP_GAME_CENTER && mouse.left && !mouse.prev_left) {
        Window *window = &windows[APP_GAME_CENTER];
        int row_x = window->x + 20;
        int row_y = window->y + 108;
        int row_w = window->w - 40;

        if (point_in_rect(mouse.x, mouse.y, row_x, row_y, row_w, 36)) {
            open_window(APP_MINES);
        } else if (point_in_rect(mouse.x, mouse.y, row_x, row_y + 34, row_w, 36)) {
            open_window(APP_SNAKE);
        } else if (point_in_rect(mouse.x, mouse.y, row_x, row_y + 68, row_w, 36)) {
            open_window(APP_GUESS);
        }
    }

    if (windows[APP_POWER].open && active_window == APP_POWER && mouse.left && !mouse.prev_left) {
        Window *window = &windows[APP_POWER];
        int x = window->x + 20;
        int y = window->y + 36;
        if (point_in_rect(mouse.x, mouse.y, x, y, 160, 24)) {
            shutdown_system();
        } else if (point_in_rect(mouse.x, mouse.y, x, y + 32, 160, 24)) {
            restart_system();
        } else if (point_in_rect(mouse.x, mouse.y, x, y + 64, 160, 24)) {
            cpu_halted_overlay = true;
        } else if (point_in_rect(mouse.x, mouse.y, x, y + 96, 160, 24)) {
            close_window(APP_POWER);
        }
    }

    if (windows[APP_SETTINGS].open && active_window == APP_SETTINGS && mouse.left && !mouse.prev_left) {
        Window *window = &windows[APP_SETTINGS];
        if (button_clicked(window->x + 14, window->y + 56, 70, 20)) {
            settings_tab = 0;
        } else if (button_clicked(window->x + 90, window->y + 56, 80, 20)) {
            settings_tab = 1;
        } else if (settings_tab == 0) {
            if (button_clicked(window->x + 178, window->y + 86, 20, 18) && live_palette_supported((uint8_t)((settings_pending.palette_mode + 2) % 3))) {
                settings_pending.palette_mode = (settings_pending.palette_mode + 2) % 3;
            } else if (button_clicked(window->x + 346, window->y + 86, 20, 18) && live_palette_supported((uint8_t)((settings_pending.palette_mode + 1) % 3))) {
                settings_pending.palette_mode = (settings_pending.palette_mode + 1) % 3;
            } else if (button_clicked(window->x + 178, window->y + 116, 20, 18) && live_resolution_supported((uint8_t)((settings_pending.resolution_mode + 4) % 5))) {
                settings_pending.resolution_mode = (settings_pending.resolution_mode + 4) % 5;
            } else if (button_clicked(window->x + 346, window->y + 116, 20, 18) && live_resolution_supported((uint8_t)((settings_pending.resolution_mode + 1) % 5))) {
                settings_pending.resolution_mode = (settings_pending.resolution_mode + 1) % 5;
            } else if (button_clicked(window->x + 20, window->y + 148, 12, 12)) {
                settings_pending.widescreen = !settings_pending.widescreen;
            }
        } else {
            if (button_clicked(window->x + 170, window->y + 86, 20, 18)) {
                settings_pending.background_mode = (settings_pending.background_mode + 9) % 10;
            } else if (button_clicked(window->x + 358, window->y + 86, 20, 18)) {
                settings_pending.background_mode = (settings_pending.background_mode + 1) % 10;
            }
        }

        if (settings_dirty()) {
            if (button_clicked(window->x + 120, window->y + window->h - 36, 60, 22)) {
                settings_pending = settings_applied;
                close_window(APP_SETTINGS);
            } else if (button_clicked(window->x + 188, window->y + window->h - 36, 60, 22)) {
                apply_settings();
            } else if (button_clicked(window->x + 256, window->y + window->h - 36, 60, 22)) {
                apply_settings();
                close_window(APP_SETTINGS);
            }
        }
    }

    if (windows[APP_TASK_MANAGER].open && active_window == APP_TASK_MANAGER && mouse.left && !mouse.prev_left) {
        Window *window = &windows[APP_TASK_MANAGER];
        if (task_manager_confirm_kill) {
            int box_x = window->x + 90;
            int box_y = window->y + 92;
            if (button_clicked(box_x + 34, box_y + 60, 78, 22)) {
                if (task_manager_kill_target >= 0 && task_manager_kill_target < APP_COUNT) {
                    close_window((AppId)task_manager_kill_target);
                }
                task_manager_confirm_kill = false;
                task_manager_kill_target = -1;
                return;
            }
            if (button_clicked(box_x + 138, box_y + 60, 78, 22)) {
                task_manager_confirm_kill = false;
                task_manager_kill_target = -1;
                return;
            }
            return;
        }

        if (button_clicked(window->x + 14, window->y + 30, 84, 20)) {
            task_manager_tab = 0;
            task_manager_gpu_scroll_drag = false;
        } else if (button_clicked(window->x + 104, window->y + 30, 96, 20)) {
            task_manager_tab = 1;
            task_manager_gpu_scroll_drag = false;
        } else if (button_clicked(window->x + 206, window->y + 30, 54, 20)) {
            task_manager_tab = 2;
        } else if (task_manager_tab == 0) {
            int rows = task_manager_process_count();
            for (int i = 0; i < rows && i < 9; ++i) {
                int row_y = window->y + 86 + i * 15;
                if (button_clicked(window->x + 22, row_y - 2, window->w - 44, 13)) {
                    task_manager_selected_process = i;
                }
            }
            if (button_clicked(window->x + 18, window->y + 232, 100, 22)) {
                AppId kill_target;
                if (task_manager_selected_is_killable(&kill_target)) {
                    task_manager_confirm_kill = true;
                    task_manager_kill_target = kill_target;
                }
            }
        } else if (task_manager_tab == 2) {
            int scroll_x = window->x + 150;
            int track_y = window->y + 80;
            int thumb_y;
            int thumb_h;
            int thumb_track;

            gpu_palette_scroll_geometry(track_y, &thumb_y, &thumb_h, &thumb_track);

            if (button_clicked(scroll_x, window->y + 62, 16, 16) && gpu_palette_scroll >= GPU_GRID_COLS) {
                gpu_palette_scroll -= GPU_GRID_COLS;
            } else if (button_clicked(scroll_x, window->y + 178, 16, 16) && gpu_palette_scroll < gpu_palette_max_scroll()) {
                gpu_palette_scroll += GPU_GRID_COLS;
                if (gpu_palette_scroll > gpu_palette_max_scroll()) {
                    gpu_palette_scroll = gpu_palette_max_scroll();
                }
            } else if (button_clicked(scroll_x + 2, thumb_y, 12, thumb_h)) {
                task_manager_gpu_scroll_drag = true;
                task_manager_gpu_scroll_drag_offset = mouse.y - thumb_y;
            } else if (button_clicked(scroll_x, track_y + 16, 16, 98)) {
                if (mouse.y < thumb_y && gpu_palette_scroll >= GPU_GRID_PAGE_ENTRIES) {
                    gpu_palette_scroll -= GPU_GRID_PAGE_ENTRIES;
                } else if (mouse.y > thumb_y + thumb_h && gpu_palette_scroll < gpu_palette_max_scroll()) {
                    gpu_palette_scroll += GPU_GRID_PAGE_ENTRIES;
                    if (gpu_palette_scroll > gpu_palette_max_scroll()) {
                        gpu_palette_scroll = gpu_palette_max_scroll();
                    }
                }
            }
        }
    }

    if (task_manager_gpu_scroll_drag && (!mouse.left || !windows[APP_TASK_MANAGER].open || active_window != APP_TASK_MANAGER || task_manager_tab != 2)) {
        task_manager_gpu_scroll_drag = false;
    } else if (task_manager_gpu_scroll_drag && mouse.left) {
        Window *window = &windows[APP_TASK_MANAGER];
        int track_y = window->y + 80;
        int thumb_y;
        int thumb_h;
        int thumb_track;
        int thumb_min = track_y + 1;
        int desired_y;
        uint32_t max_scroll = gpu_palette_max_scroll();

        gpu_palette_scroll_geometry(track_y, &thumb_y, &thumb_h, &thumb_track);
        desired_y = clampi(mouse.y - task_manager_gpu_scroll_drag_offset, thumb_min, thumb_min + thumb_track);
        if (max_scroll != 0) {
            uint32_t raw_scroll = (uint32_t)((desired_y - thumb_min) * (int)max_scroll / thumb_track);
            gpu_palette_scroll = (raw_scroll / GPU_GRID_COLS) * GPU_GRID_COLS;
            if (gpu_palette_scroll > max_scroll) {
                gpu_palette_scroll = max_scroll;
            }
        }
    }
}

static void update_state(void) {
    KeyEvent event;

    if (cpu_halted_overlay) {
        __asm__ volatile("cli");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    while (dequeue_key(&event)) {
        switch (system_state) {
            case STATE_BOOT_MENU:
                handle_boot_menu_keys(event);
                break;
            case STATE_BOOT_TERMINAL:
                terminal_handle_key(&boot_term, event, true);
                break;
            case STATE_LOGIN:
                handle_login_keys(event);
                break;
            case STATE_DESKTOP:
                if (event.code == KEY_F4 && keyboard_alt && active_window >= 0 && active_window < APP_COUNT) {
                    close_window((AppId)active_window);
                } else if (event.code == KEY_ESC && active_window == -1) {
                    menu_open = false;
                    context_menu_open = false;
                } else {
                    handle_text_target(event);
                }
                break;
            case STATE_SHUTDOWN:
                break;
        }
    }

    if (system_state == STATE_DESKTOP) {
        handle_desktop_mouse();
        update_snake();
    }
}

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
    load_desktop_icon_positions();
    clear_boot_status();
    boot_menu_dirty = true;
    boot_terminal_dirty = true;
    boot_terminal_last_blink = 0xFFFFFFFFu;
    last_input_tick = 0;
    keyboard_alt = false;
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
    terminal_add_line(&boot_term, "help clear cls date time boot shutdown restart halt echo");
    terminal_add_line(&cmd_term, "help clear cls date time boot shutdown restart halt echo");
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

    boot_drive_valid = false;
    boot_drive_number = 0;
    if (magic == 0x2BADB002 && mbi != NULL && (mbi->flags & (1u << 1)) != 0) {
        boot_drive_valid = true;
        boot_drive_number = (uint8_t)(mbi->boot_device & 0xFFu);
    }
    ram_total_bytes = detect_total_ram_bytes(mbi);

    init_framebuffer(magic, mbi);
    video_mode_switch_available = detect_video_mode_switch();
    init_interrupts();
    init_state();
    enter_boot_text_mode();
    draw_everything();
    present();
    init_mouse();
    __asm__ volatile ("sti");
    init_cpu_monitoring();
    calibrate_cpu_speed();
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
            attempt_poweroff();
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

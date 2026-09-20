/*
    HaloxOS - Version 1.0 Release!
    Copyright Svh03ra (C) 2026, All rights reserved.
    Source File: kernel.c, main core.
    Build: 19th June 2026.

    Made in AI used: GPT-5.5 for Visual Code Editor at Codex.
*/

/*  This repository is licensed under the GNU General Public License.
        --------------------------------------------------------------
    Free to use, modify, or create your own fork,
    provided that you agree to and comply with the terms of the license. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "build_info.h"
#include "../../config/config.h"

// Backgrounds
extern const uint8_t _binary_build_login_bin_start[];
extern const uint8_t _binary_build_theme1_bin_start[];
extern const uint8_t _binary_build_theme2_bin_start[];
extern const uint8_t _binary_build_user_frame_bin_start[];
extern const uint8_t _binary_build_notepad_icon_bin_start[];
extern const uint8_t _binary_build_terminal_icon_bin_start[];
extern const uint8_t _binary_build_game_icon_bin_start[];
extern const uint8_t _binary_build_demo_icon_bin_start[];
extern const uint8_t _binary_build_program_icon_bin_start[];
extern const uint8_t _binary_build_settings_icon_bin_start[];
extern const uint8_t _binary_build_explorer_icon_bin_start[];
extern const uint8_t _binary_build_taskmgr_icon_bin_start[];
extern const uint8_t _binary_build_mines_icon_bin_start[];
extern const uint8_t _binary_build_snake_icon_bin_start[];
extern const uint8_t _binary_build_guessnum_icon_bin_start[];
extern const uint8_t _binary_build_paint_icon_bin_start[];
extern const uint8_t _binary_build_power_icon_bin_start[];
extern const uint8_t _binary_build_box3d_icon_bin_start[];
extern const uint8_t _binary_build_firecracker_icon_bin_start[];
extern const uint8_t _binary_build_doom_icon_bin_start[];

/*
 * The logical desktop canvas.
 *
 * Every desktop coordinate is a canvas pixel: the taskbar sits on
 * canvas_height - TASKBAR_H, windows and icons are clamped to the canvas, the
 * cursor lives inside it. The present step then maps the canvas onto whatever
 * output mode is active.
 *
 * The canvas follows the output mode, so a bigger mode buys desktop ROOM
 * instead of magnified pixels: at 1024x768 the desktop really is 1024x768
 * with the same 28px taskbar, the same 8px text and the same icon sizes, and
 * the wallpaper is tiled at its native 640x480 pitch. Only modes smaller than
 * the 640x480 base keep the base canvas and get sampled down, which is the
 * historical low-resolution behaviour.
 *
 * The canvas is capped because the two shadow planes are one statically
 * sized buffer: the cap is what holds the kernel's RAM span - and therefore
 * the boot floor - where it is. A mode larger than the cap still works; its
 * canvas is centred 1:1 inside it.
 */
#define OS_BASE_WIDTH 640
#define OS_BASE_HEIGHT 480
#define OS_MAX_CANVAS_WIDTH 1440
#define OS_MAX_CANVAS_HEIGHT 900
#define MAX_OUTPUT_WIDTH 1920
#define MAX_OUTPUT_HEIGHT 1200

static int canvas_width = OS_BASE_WIDTH;
static int canvas_height = OS_BASE_HEIGHT;

/* Desktop size the resolution menu asked for (see set_desktop_size and
 * update_canvas_for_framebuffer). It starts at the base canvas and is set to
 * the mode the boot ended up in, so a boot at a bigger mode fills it. */
static int canvas_request_width = OS_BASE_WIDTH;
static int canvas_request_height = OS_BASE_HEIGHT;

#define OS_WIDTH (canvas_width)
#define OS_HEIGHT (canvas_height)

#if HALOXOS_CONFIG_DEBUG != 0 && HALOXOS_CONFIG_DEBUG != 1
#error "HALOXOS_CONFIG_DEBUG must be 0 or 1"
#endif

#if !((HALOXOS_CONFIG_SCREEN_WIDTH == 192 && HALOXOS_CONFIG_SCREEN_HEIGHT == 144) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 256 && HALOXOS_CONFIG_SCREEN_HEIGHT == 144) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 320 && HALOXOS_CONFIG_SCREEN_HEIGHT == 240) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 426 && HALOXOS_CONFIG_SCREEN_HEIGHT == 240) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 480 && HALOXOS_CONFIG_SCREEN_HEIGHT == 360) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 640 && HALOXOS_CONFIG_SCREEN_HEIGHT == 360) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 640 && HALOXOS_CONFIG_SCREEN_HEIGHT == 480) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 854 && HALOXOS_CONFIG_SCREEN_HEIGHT == 480) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 800 && HALOXOS_CONFIG_SCREEN_HEIGHT == 600) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 960 && HALOXOS_CONFIG_SCREEN_HEIGHT == 720) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 1280 && HALOXOS_CONFIG_SCREEN_HEIGHT == 720) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 1024 && HALOXOS_CONFIG_SCREEN_HEIGHT == 768) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 1366 && HALOXOS_CONFIG_SCREEN_HEIGHT == 768) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 1280 && HALOXOS_CONFIG_SCREEN_HEIGHT == 800))
#error "Unsupported HALOXOS_CONFIG_SCREEN_WIDTH/HALOXOS_CONFIG_SCREEN_HEIGHT pair"
#endif

#if HALOXOS_CONFIG_SCREEN_BPP != 4 && HALOXOS_CONFIG_SCREEN_BPP != 8 && HALOXOS_CONFIG_SCREEN_BPP != 16
#error "HALOXOS_CONFIG_SCREEN_BPP must be 4, 8, or 16"
#endif

/*
 * The mode table: one entry per click of the resolution arrows, ordered by
 * size inside each column. The widescreen checkbox picks the column, so an
 * index means "that size class" in either one - index 5 is the 960x720
 * desktop in 4:3 and the 1280x800 one in widescreen.
 *
 * The entries above the 640x480 base are the "gap" modes real hardware and
 * VMs actually offer (800x600, 1024x768, 1280x800, 1366x768) - none of them
 * is a scaled copy of another, each one is rendered natively at 1:1.
 */
typedef struct {
    uint16_t width;
    uint16_t height;
} VideoModeOption;

#define VIDEO_MODE_COUNT 7

static const VideoModeOption video_modes_4_3[VIDEO_MODE_COUNT] = {
    {192, 144},
    {320, 240},
    {480, 360},
    {640, 480},
    {800, 600},
    {960, 720},
    {1024, 768}
};

static const VideoModeOption video_modes_wide[VIDEO_MODE_COUNT] = {
    {256, 144},
    {426, 240},
    {640, 360},
    {854, 480},
    {1280, 720},
    {1280, 800},
    {1366, 768}
};

#if (HALOXOS_CONFIG_SCREEN_WIDTH == 192 && HALOXOS_CONFIG_SCREEN_HEIGHT == 144) || \
    (HALOXOS_CONFIG_SCREEN_WIDTH == 256 && HALOXOS_CONFIG_SCREEN_HEIGHT == 144)
#define HALOXOS_CONFIG_RESOLUTION_MODE 0
#elif (HALOXOS_CONFIG_SCREEN_WIDTH == 320 && HALOXOS_CONFIG_SCREEN_HEIGHT == 240) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 426 && HALOXOS_CONFIG_SCREEN_HEIGHT == 240)
#define HALOXOS_CONFIG_RESOLUTION_MODE 1
#elif (HALOXOS_CONFIG_SCREEN_WIDTH == 480 && HALOXOS_CONFIG_SCREEN_HEIGHT == 360) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 640 && HALOXOS_CONFIG_SCREEN_HEIGHT == 360)
#define HALOXOS_CONFIG_RESOLUTION_MODE 2
#elif (HALOXOS_CONFIG_SCREEN_WIDTH == 640 && HALOXOS_CONFIG_SCREEN_HEIGHT == 480) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 854 && HALOXOS_CONFIG_SCREEN_HEIGHT == 480)
#define HALOXOS_CONFIG_RESOLUTION_MODE 3
#elif (HALOXOS_CONFIG_SCREEN_WIDTH == 800 && HALOXOS_CONFIG_SCREEN_HEIGHT == 600) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 1280 && HALOXOS_CONFIG_SCREEN_HEIGHT == 720)
#define HALOXOS_CONFIG_RESOLUTION_MODE 4
#elif (HALOXOS_CONFIG_SCREEN_WIDTH == 960 && HALOXOS_CONFIG_SCREEN_HEIGHT == 720) || \
      (HALOXOS_CONFIG_SCREEN_WIDTH == 1280 && HALOXOS_CONFIG_SCREEN_HEIGHT == 800)
#define HALOXOS_CONFIG_RESOLUTION_MODE 5
#else
#define HALOXOS_CONFIG_RESOLUTION_MODE 6
#endif

#if HALOXOS_CONFIG_SCREEN_WIDTH == 1280 || \
    HALOXOS_CONFIG_SCREEN_WIDTH == 1366 || \
    HALOXOS_CONFIG_SCREEN_WIDTH == 854 || \
    (HALOXOS_CONFIG_SCREEN_WIDTH == 640 && HALOXOS_CONFIG_SCREEN_HEIGHT == 360) || \
    HALOXOS_CONFIG_SCREEN_WIDTH == 426 || \
    HALOXOS_CONFIG_SCREEN_WIDTH == 256
#define HALOXOS_CONFIG_WIDESCREEN 1
#else
#define HALOXOS_CONFIG_WIDESCREEN 0
#endif

#if HALOXOS_CONFIG_SCREEN_BPP == 4
#define HALOXOS_CONFIG_PALETTE_MODE 1
#define HALOXOS_CONFIG_OUTPUT_BPP 4
#elif HALOXOS_CONFIG_SCREEN_BPP == 16
#define HALOXOS_CONFIG_PALETTE_MODE 2
#define HALOXOS_CONFIG_OUTPUT_BPP 16
#else
#define HALOXOS_CONFIG_PALETTE_MODE 0
#define HALOXOS_CONFIG_OUTPUT_BPP 8
#endif

#define TASKBAR_H 28
#define DESKTOP_ICON_COUNT 11
#define DESKTOP_ICON_NAME_MAX 50
#define MAX_TEST_WINDOWS 1000
#define TRAIL_COUNT 6
#define MAX_TEXT 4096
#define TERM_MAX_LINES 32
/* 256 so debugger breakpoint traces fit on one long line; the log
 * view pans across them horizontally with Ctrl+Left/Right. */
#define TERM_LINE_LEN 256
#define DEBUG_HISTORY_COUNT 8
#define DEBUG_EDITED_RANGE_COUNT 32
#define DEBUG_MEMORY_BYTES_PER_ROW 16
#define DEBUG_MEMORY_VISUAL_W 430
#define DEBUG_MEMORY_VISUAL_H 240
#define DEBUG_MEMORY_MAX_EDIT_LENGTH 0x10000u
#define APP_COUNT 15
#define SNAKE_MAX_SEGMENTS 128
#define MINES_SIZE 8
#define MINES_COUNT 10
#define TIMER_HZ 60
#define TERMINAL_CURSOR_BLINK_TICKS 30
#define DEBUG_LOG_SCROLL_MAX 1024
#define SNAKE_STEP_TICKS 9
#define PERF_UPDATE_TICKS 6
#define PAINT_CANVAS_W 352
#define PAINT_CANVAS_H 224
#define GPU_GRID_COLS 24
#define GPU_GRID_ROWS 12
#define GPU_SWATCH_SIZE 10
#define GPU_GRID_W (GPU_GRID_COLS * GPU_SWATCH_SIZE)
#define GPU_GRID_H (GPU_GRID_ROWS * GPU_SWATCH_SIZE)
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
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
    KEY_PAUSE,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_DEL,
    KEY_HOME,
    KEY_END
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
    uint8_t framebuffer_red_field_position;
    uint8_t framebuffer_red_mask_size;
    uint8_t framebuffer_green_field_position;
    uint8_t framebuffer_green_mask_size;
    uint8_t framebuffer_blue_field_position;
    uint8_t framebuffer_blue_mask_size;
} __attribute__((packed)) MultibootInfo;

typedef struct {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed)) MultibootMmapEntry;

typedef struct {
    uint32_t size;
    uint8_t drive_number;
    uint8_t drive_mode;
    uint16_t drive_cylinders;
    uint8_t drive_heads;
    uint8_t drive_sectors;
    uint16_t ports[];
} __attribute__((packed)) MultibootDriveInfo;

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
    uint8_t type;
    uint8_t red_position;
    uint8_t red_mask_size;
    uint8_t green_position;
    uint8_t green_mask_size;
    uint8_t blue_position;
    uint8_t blue_mask_size;
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
    APP_TASK_MANAGER,
    APP_DEMO_CENTER,
    APP_3D_BOX,
    APP_FIRECRACKER,
    APP_DOOM
} AppId;

typedef struct {
    char lines[TERM_MAX_LINES][TERM_LINE_LEN];
    int line_count;
    char input[TERM_LINE_LEN];
    int input_len;
    int wrap_chars;
} Terminal;

typedef enum {
    DEBUG_MEMORY_MODE_HEX,
    DEBUG_MEMORY_MODE_VISUAL
} DebugMemoryMode;

typedef struct {
    uint32_t start;
    uint32_t length;
} DebugEditedRange;

typedef struct {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t vector;
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} CpuExceptionFrame;

typedef enum {
    DEBUG_ACTION_NONE,
    DEBUG_ACTION_CRASH,
    DEBUG_ACTION_CRASH_VECTOR,
    DEBUG_ACTION_HALT,
    DEBUG_ACTION_FAULT1,
    DEBUG_ACTION_FAULT2,
    DEBUG_ACTION_FAULT3
} DebugAction;

typedef struct {
    uint8_t palette_mode;
    uint8_t resolution_mode;
    bool widescreen;
    uint8_t background_mode;
    bool window_fade;
    bool window_trails;
    /* Wallpaper fit: false scales the art to the canvas (Stretch, the
     * default), true centres it at its own pixel size (Fit). */
    bool wallpaper_fit;
} SettingsState;

typedef enum {
    VIDEO_BACKEND_NONE,
    VIDEO_BACKEND_MULTIBOOT,
    VIDEO_BACKEND_VGA,
    VIDEO_BACKEND_BGA,
    VIDEO_BACKEND_VMWARE_SVGA
} VideoBackend;

enum {
    MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED = 0,
    MULTIBOOT_FRAMEBUFFER_TYPE_RGB = 1,
    MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT = 2
};

typedef struct {
    uint16_t io_base;
    uint8_t port_stride;
    bool device_present;
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
    "TaskMgr",
    "Demos",
    "3D Box",
    "Firecr..",
    "DOOM"
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
/*
 * Desktop shadow storage.
 *
 * The desktop is kept in two interchangeable formats - 8-bit palette
 * indices and RGB565 - and exactly one of them is ever read: present()
 * scans out whichever plane update_present_maps() elected for the active
 * output format, and every writer in graphics.c goes through the
 * plane-aware helpers so the other one is never touched (see
 * present_need_index_plane in vga.c).
 *
 * They therefore overlay one buffer instead of occupying two: the kernel
 * reserves the larger plane rather than both. The names below keep every
 * existing reference working unchanged.
 *
 * The storage is sized for the largest canvas the OS can grow to
 * (OS_MAX_CANVAS_*), not for the current mode: the canvas is re-picked on
 * every video mode change (see update_canvas_for_framebuffer below), and
 * that cap is what keeps this buffer - and the boot RAM floor - bounded.
 */
typedef union {
    uint8_t index[OS_MAX_CANVAS_WIDTH * OS_MAX_CANVAS_HEIGHT];
    uint16_t rgb565[OS_MAX_CANVAS_WIDTH * OS_MAX_CANVAS_HEIGHT];
} ShadowPlaneStorage;

static ShadowPlaneStorage shadow_planes;

#define backbuffer (shadow_planes.index)
#define backbuffer_rgb565 (shadow_planes.rgb565)
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
static bool keyboard_ctrl = false;
static bool keyboard_capslock = false;
static bool keyboard_numlock = false;
static bool menu_open = false;
static bool context_menu_open = false;
static int context_menu_x = 0;
static int context_menu_y = 0;
static bool desktop_icon_menu_open = false;
static int desktop_icon_menu_x = 0;
static int desktop_icon_menu_y = 0;
static int desktop_icon_menu_target = -1;
static bool start_app_menu_open = false;
static int start_app_menu_x = 0;
static int start_app_menu_y = 0;
static AppId start_app_menu_app = APP_NOTEPAD;
static bool power_menu_open = false;
static int start_menu_hover_row = -1;
static uint32_t start_menu_hover_tick = 0;
static int game_center_hover_row = -1;
static uint32_t game_center_hover_tick = 0;
static int demo_center_hover_row = -1;
static uint32_t demo_center_hover_tick = 0;

/* 3D Box demo: rotating cube, mode 0 = wireframe, mode 1 = solid. */
static uint8_t box3d_mode = 0;
static int box3d_angle = 0;      /* yaw (Y axis) */
static int box3d_angle_x = 0;    /* pitch (X axis): cube tumbles on XY */
static uint32_t box3d_last_tick = 0;

/* Firecracker demo: particle bursts. */
#define FIRE_PARTICLES 96
static int fire_x[FIRE_PARTICLES];
static int fire_y[FIRE_PARTICLES];
static int fire_vx[FIRE_PARTICLES];
static int fire_vy[FIRE_PARTICLES];
static uint8_t fire_color[FIRE_PARTICLES];
static bool fire_alive[FIRE_PARTICLES];
static uint32_t fire_last_tick = 0;

static uint32_t button_hover_id = 0;
static uint32_t button_hover_tick = 0;
/* Set by draw_hover_fade_rect on any frame it paints a partial dither
 * (the fade is mid-animation); redraw.c keeps full-desktop repaints
 * running until it clears, so a hover fade can animate even when the
 * pointer and every other input have gone still - exactly the case the
 * dirty-redraw gate was written to skip. */
static bool ui_hover_fade_active = false;
/* Any input change since the last presented frame.
 *
 * A flag, not a tick stamp. The redraw gate used to compare timer_ticks,
 * so two input edges inside one 60Hz tick (16.6ms - a fast click, or a
 * release sharing a tick with the frame that already recorded it) were
 * indistinguishable from a single one. The second edge then caused no
 * repaint at all, which is how a released button kept its pressed look
 * and a closed Start menu stayed painted until unrelated input arrived. */
static bool ui_input_dirty = false;
/* A pointer button changed state (press, release, or the end of a replayed
 * one-frame click) during this frame's poll_input. Everything on screen
 * was painted for the other state, so this frame must be a full repaint -
 * the pointer-only path must not swallow it. */
static bool ui_pointer_state_changed = false;
/* Rectangles of the widgets that react to the pointer (buttons and
 * interactive rows), recorded by the last full desktop render as it drew
 * them. The pointer-only repaint path consults this list: the desktop
 * image is only a valid stand-in for the background under the pointer if
 * the pointer stayed off every one of these. Entering or leaving a widget
 * therefore always forces a full repaint, which is what makes its hover
 * highlight and dithered fade appear. Without it, a move onto a button
 * whose cursor shape did not change was taken as "only the pointer
 * moved", so the button never learned the pointer had arrived and its
 * hover stayed dark until some unrelated repaint. */
#define HOVER_RECT_MAX 128
static int hover_rect_x[HOVER_RECT_MAX];
static int hover_rect_y[HOVER_RECT_MAX];
static int hover_rect_w[HOVER_RECT_MAX];
static int hover_rect_h[HOVER_RECT_MAX];
static int hover_rect_count = 0;
static bool cursor_hand_hint = false;
static int active_window = -1;
static int drag_window = -1;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static bool cpu_halted_overlay = false;
static bool shutdown_pending = false;
static bool shutdown_poweroff_failed = false;
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
static uint8_t color_crash_blue;
static uint8_t color_red;
static uint8_t color_yellow;
static uint8_t color_orange;
static uint8_t color_pink;
static uint8_t color_desktop_icon;

static char notepad_text[MAX_TEXT];
static size_t notepad_len = 0;

static Terminal boot_term;
static Terminal cmd_term;
static Terminal debug_term;
static bool debug = HALOXOS_CONFIG_DEBUG != 0;
static bool debug_overlay_open = false;
static DebugAction debug_pending_action = DEBUG_ACTION_NONE;
static uint8_t debug_crash_vector = 0xFF;
static char debug_history[DEBUG_HISTORY_COUNT][TERM_LINE_LEN];
static int debug_history_count = 0;
static int debug_history_cursor = 0;
static bool debug_memory_view_open = false;
static DebugMemoryMode debug_memory_mode = DEBUG_MEMORY_MODE_HEX;
static uint32_t debug_memory_base = 0;
static uint32_t debug_memory_cursor = 0;
static int debug_memory_edit_nibble = -1;
static DebugEditedRange debug_edited_ranges[DEBUG_EDITED_RANGE_COUNT];
static int debug_edited_range_count = 0;
static const char *debug_forced_fault_reason = NULL;
static int debug_log_scroll_x = 0;   /* Ctrl+Left/Right log panning */

/* FPS overlay (debugger "show fps" command): frame counting happens in
 * kernel_main; this only stores what the overlay renders. */
static bool fps_overlay_on = false;
static uint32_t fps_display = 0;        /* frames in the current window */
static uint32_t fps_current = 0;       /* final FPS of the last window */
static uint32_t fps_avg_ticks_x10 = 0;  /* avg frame time, ticks * 10 */
static bool fps_lagging = false;        /* current frame dropped/lagged */
static uint32_t fps_frames_total = 0;   /* frame counter for breakpoints */

/* Breakpoints (debugger "breakpoint" command): one bit per AppId, plus
 * bit 15 = "all". When a targeted app renders, the debugger terminal
 * opens itself with a code + opcode trace. */
static uint16_t debug_bp_mask = 0;
static bool debug_bp_caught = false;
static uint32_t debug_bp_frame_base = 0;   /* frame count when resumed */
static uint32_t debug_bp_catch_count = 0;   /* catches since armed */
static uint32_t debug_bp_last_catch_tick = 0; /* rate limit: 1 catch per frame */
static bool debug_bp_armed = false;         /* armed AND resumed */

static uint8_t paint_canvas[PAINT_CANVAS_W * PAINT_CANVAS_H];
static uint8_t paint_color = 1;
static uint8_t paint_brush_size = 1;
static uint8_t paint_tool = 0;
static int paint_text_x = -1;
static int paint_text_y = -1;
static uint8_t paint_clipboard[PAINT_CANVAS_W * PAINT_CANVAS_H];
static uint8_t paint_undo_buffer[PAINT_CANVAS_W * PAINT_CANVAS_H];
static bool paint_clipboard_valid = false;
static bool paint_undo_valid = false;

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

static SettingsState settings_applied = {
    HALOXOS_CONFIG_PALETTE_MODE,
    HALOXOS_CONFIG_RESOLUTION_MODE,
    HALOXOS_CONFIG_WIDESCREEN != 0,
    0,
    false,
    false,
    false   /* Stretch: the wallpaper fills the desktop */
};
static SettingsState settings_pending = {
    HALOXOS_CONFIG_PALETTE_MODE,
    HALOXOS_CONFIG_RESOLUTION_MODE,
    HALOXOS_CONFIG_WIDESCREEN != 0,
    0,
    false,
    false,
    false   /* Stretch: the wallpaper fills the desktop */
};
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

/* WAD multiboot module: GRUB loads the game data into RAM via BIOS, so
 * the kernel reads it straight from memory on ANY boot media (USB stick,
 * CD, disk) - the ATA/ATAPI bus is not involved at all. Filled in
 * kernel_main from the multiboot module list. */
static uint32_t doom_wad_module_addr = 0;
static uint32_t doom_wad_module_bytes = 0;
static uintptr_t crash_mmap_addr = 0;
static uint32_t crash_mmap_length = 0;
static bool boot_drive_info_available = false;
static uint8_t boot_drive_mode = 0;
static uint16_t boot_drive_cylinders = 0;
static uint8_t boot_drive_heads = 0;
static uint8_t boot_drive_sectors = 0;
static uint32_t boot_drive_storage_bytes = 0;
static bool desktop_icon_persistence_enabled = false;
static DesktopIconPosition desktop_icons[DESKTOP_ICON_COUNT];
static bool desktop_icon_visible[DESKTOP_ICON_COUNT];
static AppId desktop_icon_apps[DESKTOP_ICON_COUNT];
static char desktop_icon_names[DESKTOP_ICON_COUNT][DESKTOP_ICON_NAME_MAX + 1];
static int selected_desktop_icon = -1;
static int desktop_icon_press = -1;
static int drag_desktop_icon = -1;
static int desktop_icon_press_x = 0;
static int desktop_icon_press_y = 0;
static int desktop_icon_drag_offset_x = 0;
static int desktop_icon_drag_offset_y = 0;
static bool desktop_icon_drag_moved = false;
static bool desktop_icon_press_was_selected = false;
static bool desktop_clipboard_valid = false;
static bool desktop_clipboard_cut = false;
static int desktop_clipboard_count = 0;
static AppId desktop_clipboard_apps[DESKTOP_ICON_COUNT];
static char desktop_clipboard_names[DESKTOP_ICON_COUNT][DESKTOP_ICON_NAME_MAX + 1];
static bool desktop_rename_active = false;
static int desktop_rename_icon = -1;
static char desktop_rename_buffer[DESKTOP_ICON_NAME_MAX + 1];
static int desktop_rename_len = 0;
static bool video_mode_switch_available = false;
static bool boot_text_mode = true;
static volatile uint16_t *const vga_text_buffer = (volatile uint16_t *)(uintptr_t)0xB8000;
static uint16_t present_x_map[MAX_OUTPUT_WIDTH];
static uint16_t present_y_map[MAX_OUTPUT_HEIGHT];
static uint32_t present_content_width = OS_BASE_WIDTH;
static uint32_t present_content_height = OS_BASE_HEIGHT;
static uint32_t present_offset_x = 0;
static uint32_t present_offset_y = 0;
/*
 * "The bands outside the content box still need painting black."
 *
 * Raised by update_present_maps() - i.e. on every mode or canvas change -
 * and lowered by present_fill_margins() once the bands for the geometry now
 * in effect have been blanked.
 *
 * This has to be a dirty flag rather than a cache keyed on the geometry. A
 * keyed cache answers "have I ever cleared bands this shape", but the answer
 * a scanout needs is "are the bands black right now": presenting a LARGER
 * content box paints over the bands - and a full-screen box has no bands to
 * clear, so nothing updated the cache - which left the next shrink served
 * from the stale entry and the previous frame's desktop (taskbar, clock and
 * version text included) still visible outside the new one.
 */
static bool present_margins_dirty = true;
/* Pointer position at the last desktop repaint, and the tick of the last
 * handled key press. Together with the redraw flags below they let the
 * desktop tell "only the pointer moved" apart from a real repaint, which
 * is what keeps the cursor responsive on slow machines. */
static int last_redraw_mouse_x = -1;
static int last_redraw_mouse_y = -1;

/* Where render_cursor() actually stamped the pointer pixels for the frame
 * currently on screen (or -1 when the pointer is not composited). IRQ12
 * moves the real pointer asynchronously mid-frame, so by the time the
 * frame is booked the live mouse.x/y can disagree with the painted pixels;
 * the redraw gate must compare against the PAINTED position, otherwise a
 * raced packet makes it believe the cursor was already drawn at the new
 * spot and the on-screen cursor freezes until the next input. */
static int cursor_painted_x = -1;
static int cursor_painted_y = -1;
static uint32_t last_key_input_tick = 0xFFFFFFFFu;
static uint32_t last_desktop_redraw_second = 0xFFFFFFFFu;
static uint32_t last_desktop_redraw_perf_phase = 0xFFFFFFFFu;
static uint32_t last_desktop_redraw_terminal_blink = 0xFFFFFFFFu;
static uint32_t last_desktop_redraw_snake_tick = 0xFFFFFFFFu;
static uint32_t last_desktop_redraw_demos_tick = 0xFFFFFFFFu;
static uint32_t last_performance_sample_phase = 0xFFFFFFFFu;
static bool task_manager_gpu_scroll_drag = false;
static int task_manager_gpu_scroll_drag_offset = 0;
static uint32_t perf_window_start_cycles = 0;
static uint32_t perf_busy_cycle_accum = 0;
static bool perf_window_ready = false;

static char test_window_titles[MAX_TEST_WINDOWS][32];
static Window test_windows[MAX_TEST_WINDOWS];
static int test_window_count = 0;
static int active_test_window = -1;
static int drag_test_window = -1;
static int drag_test_offset_x = 0;
static int drag_test_offset_y = 0;

static int count_open_test_windows(void) {
    int n = 0;
    for (int i = 0; i < test_window_count; ++i) {
        if (test_windows[i].open) ++n;
    }
    return n;
}

static bool desktop_select_dragging = false;
static int desktop_select_x1 = 0;
static int desktop_select_y1 = 0;
static int desktop_select_x2 = 0;
static int desktop_select_y2 = 0;
static bool desktop_icon_multi_selected[DESKTOP_ICON_COUNT] = {false};
static int desktop_icon_drag_start_x[DESKTOP_ICON_COUNT];
static int desktop_icon_drag_start_y[DESKTOP_ICON_COUNT];
static bool desktop_auto_grid = true;
/* Visual snap grid. The overlay is OFF until the debugger's
 * "show grid" / "app grid" / "desktop grid" command turns it on (it used
 * to paint itself automatically while an icon was dragged, so a fresh
 * desktop already looked like a graph-paper sheet). Auto-Grid keeps
 * snapping icon drags whether or not the overlay is up, because
 * DESKTOP_GRID_CELL - the snap step in pixels - is what locks an icon to
 * the 4x4 grid. */
static bool desktop_grid_overlay_on = false;
#define DESKTOP_GRID_CELL 4
/* True while a window's own controls are being drawn AND that window is
 * the topmost one under the pointer. draw_button / draw_interactive_row
 * read it so the buttons of a window that is covered by another window
 * neither light the hand cursor nor show a press highlight "through" the
 * window on top of them (see desktop_top_window_at in widgets.c). */
static bool ui_window_interactive = true;
static bool desktop_undo_valid = false;
static bool desktop_undo_visible[DESKTOP_ICON_COUNT];
static int desktop_undo_x[DESKTOP_ICON_COUNT];
static int desktop_undo_y[DESKTOP_ICON_COUNT];
static char desktop_undo_names[DESKTOP_ICON_COUNT][DESKTOP_ICON_NAME_MAX + 1];
/* How many open apps the taskbar row shows before the overflow arrows
 * appear. Five buttons (58 + slot * 88, 84 wide) end at x = 494, which
 * still leaves the arrows at clock_widget_x() - 32 = 512 and the clock box
 * at 544 clear. */
#define TASKBAR_APP_SLOTS 5

static int taskbar_scroll = 0;
static bool taskbar_menu_open = false;
static int taskbar_menu_x = 0;
static int taskbar_menu_y = 0;
static int trail_x[TRAIL_COUNT];
static int trail_y[TRAIL_COUNT];
static int trail_w[TRAIL_COUNT];
static int trail_h[TRAIL_COUNT];
static uint32_t trail_tick[TRAIL_COUNT];
static int trail_head = 0;
static bool window_fade_active = false;
static uint32_t window_fade_tick = 0;
static AppId window_fade_app = APP_NOTEPAD;
static int window_fade_x = 0;
static int window_fade_y = 0;
static int window_fade_w = 0;
static int window_fade_h = 0;
static uint32_t drag_anim_tick = 0;
static uint32_t window_close_hover_tick = 0;
static int context_menu_hover_row = -1;
static uint32_t context_menu_hover_tick = 0;
static int desktop_icon_menu_hover_row = -1;
static uint32_t desktop_icon_menu_hover_tick = 0;

extern void irq0_stub(void);
extern void irq12_stub(void);
extern void irq_default_stub(void);
extern void isr_default_stub(void);
extern void isr0_stub(void);
extern void isr1_stub(void);
extern void isr2_stub(void);
extern void isr3_stub(void);
extern void isr4_stub(void);
extern void isr5_stub(void);
extern void isr6_stub(void);
extern void isr7_stub(void);
extern void isr8_stub(void);
extern void isr9_stub(void);
extern void isr10_stub(void);
extern void isr11_stub(void);
extern void isr12_stub(void);
extern void isr13_stub(void);
extern void isr14_stub(void);
extern void isr15_stub(void);
extern void isr16_stub(void);
extern void isr17_stub(void);
extern void isr18_stub(void);
extern void isr19_stub(void);
extern void isr20_stub(void);
extern void isr21_stub(void);
extern void isr22_stub(void);
extern void isr23_stub(void);
extern void isr24_stub(void);
extern void isr25_stub(void);
extern void isr26_stub(void);
extern void isr27_stub(void);
extern void isr28_stub(void);
extern void isr29_stub(void);
extern void isr30_stub(void);
extern void isr31_stub(void);
extern void idt_load(const IdtPointer *pointer);
extern void cpu_halt_once(void);

static void program_vga_palette(void);
static void set_default_framebuffer_format(uint8_t bpp);
static void update_present_maps(void);
static uint16_t palette_rgb565(uint8_t color);
static Color rgb565_to_color(uint16_t value);
static void apply_settings(void);
static void desktop_finish_rename(bool commit);
static void append_char(char *dest, size_t *len, size_t max_len, char ch);
static bool cursor_over_clickable(void);
static void draw_hover_fade_rect(int x, int y, int w, int h, uint8_t base, uint8_t dark, uint32_t hover_tick);
static void hover_widget_reset(void);
static void hover_widget_add(int x, int y, int w, int h);
static bool hover_widget_hit(int x, int y);
static void desktop_fit_to_canvas(void);
static bool vga_probe_present(void);
static bool vga_set_graphics_mode(uint16_t width, uint16_t height, uint16_t bpp);
static void vga_save_and_enter_text_mode(void);
static bool vga_native_text_mode_active(void);
static void present(void);
static void fill_rect(int x, int y, int w, int h, uint8_t color);
static void draw_text(int x, int y, const char *text, uint8_t fg, uint8_t bg, bool transparent);
static void graphics_reset_window_clip(void);
static void draw_char(int x, int y, char ch, uint8_t fg, uint8_t bg, bool transparent);

/* New demo/game apps: defined in files included after their first use
 * (window_manager.c / keyboard_routing.c run before the game logic). */
static void box3d_handle_key(KeyEvent event);

/* DOOM app (apps/DOOM/port/doom_engine.c amalgamation): renderer and
 * frame pump are defined in files included after their first use. */
static void render_doom(const Window *window);
static void update_doom(void);
static void doom_app_handle_key(KeyEvent event);
static bool doom_pointer_locked(void);
static bool doom_point_in_client(int x, int y);
static void doom_set_pointer_lock(bool locked);
static void doom_toggle_pointer_lock(void);
static bool doom_app_needs_redraw(void);
static void doom_app_mark_presented(void);
void doom_wad_selftest(void);

/* Debugger power breakpoint: defined in debugger.c (included after the
 * power handlers); called before every shutdown/restart/halt trigger. */
static void debug_bp_catch_power(const char *action);

/*
 * Pick the desktop canvas for the framebuffer that is (about to be) active.
 *
 * Callers are the paths that establish framebuffer geometry (video_modes.c
 * backend init and set_framebuffer_mode_raw) and update_present_maps(), so
 * the canvas can never disagree with the mode being presented.
 *
 * The canvas is the desktop size that was REQUESTED - the resolution menu's
 * selection (canvas_request_*) - as far as the active mode allows:
 *
 *   - a mode that can hold the request at 1:1 becomes the desktop (native),
 *   - a request smaller than the mode is drawn centred in it at 1:1, which is
 *     what makes the resolution menu work on a backend that cannot set modes:
 *     the desktop is re-sized in software inside the mode we booted with,
 *   - a mode smaller than the 640x480 base keeps the base canvas and is
 *     sampled down by the present maps (the historical low-resolution path).
 *
 * The cap only exists to bound the shadow buffer; a request bigger than it
 * centres a capped canvas at 1:1.
 *
 * Returns true when the size actually changed. That matters to the caller:
 * the shadow planes are indexed with the canvas width, so a resize
 * reinterprets every stored pixel and the screen must be recomposited from
 * scratch rather than patched by the pointer-only path.
 */
static bool update_canvas_for_framebuffer(void) {
    int width = (int)fb.width;
    int height = (int)fb.height;
    int new_width;
    int new_height;
    bool changed;

    if (width < OS_BASE_WIDTH || height < OS_BASE_HEIGHT) {
        new_width = OS_BASE_WIDTH;
        new_height = OS_BASE_HEIGHT;
    } else if (canvas_request_width >= OS_BASE_WIDTH && canvas_request_height >= OS_BASE_HEIGHT) {
        new_width = canvas_request_width < width ? canvas_request_width : width;
        new_height = canvas_request_height < height ? canvas_request_height : height;
        if (new_width > OS_MAX_CANVAS_WIDTH) new_width = OS_MAX_CANVAS_WIDTH;
        if (new_height > OS_MAX_CANVAS_HEIGHT) new_height = OS_MAX_CANVAS_HEIGHT;
    } else {
        /* No usable request (a sub-base desktop size that the hardware then
         * refused to switch to): the mode itself is the desktop. */
        new_width = width > OS_MAX_CANVAS_WIDTH ? OS_MAX_CANVAS_WIDTH : width;
        new_height = height > OS_MAX_CANVAS_HEIGHT ? OS_MAX_CANVAS_HEIGHT : height;
    }

    changed = new_width != canvas_width || new_height != canvas_height;
    canvas_width = new_width;
    canvas_height = new_height;

    if (changed) {
        /* Blank both planes (they overlay one buffer, so one pass covers
         * the union - only the canvas the new mode actually shows, since
         * every writer strides rows by the new canvas width) and ask the
         * frame loop for a full repaint: a pointer-only frame would
         * restore background saved for the old geometry, and the saved
         * cursor box belongs to the old canvas size as well.
         *
         * Written as a loop rather than memset_local(): this runs before
         * runtime.c (where the freestanding string helpers live) is
         * included, and it keeps the mode switch from touching storage the
         * new mode never presents. */
        for (int y = 0; y < new_height; ++y) {
            uint8_t *row = &shadow_planes.index[(size_t)y * (size_t)new_width];
            for (int x = 0; x < new_width; ++x) {
                row[x] = 0;
            }
        }
        /* A smaller canvas can cut windows (and icons) off the desktop: the
         * title bar of a window parked past the new right edge would be
         * unreachable, so nothing could drag it back. Pull everything into
         * the new bounds before the repaint that shows it. */
        desktop_fit_to_canvas();
        ui_input_dirty = true;
    }

    return changed;
}

/*
 * Ask for a desktop size, then re-derive the canvas from it.
 *
 * Called with the mode the settings app is applying (before the hardware
 * switch, so the switch's own update_present_maps() already knows the size
 * the user picked) and with the mode the boot ended up in. On a backend
 * without mode setting this is the whole mechanism: the canvas moves and the
 * present maps centre it inside the fixed framebuffer.
 */
static void set_desktop_size(int width, int height) {
    if (width < OS_BASE_WIDTH || height < OS_BASE_HEIGHT) {
        width = OS_BASE_WIDTH;
        height = OS_BASE_HEIGHT;
    }

    if (width == canvas_request_width && height == canvas_request_height) {
        return;
    }

    canvas_request_width = width;
    canvas_request_height = height;
    update_present_maps();
}

/*
 * These are all locations which kept from source code path:
 */

#include "io_ports.c"
#include "../debug/serial.c"
#include "../../driver/etc/ata.c"
#include "cpu.c"
#include "../../driver/etc/pci.c"
#include "timer.c"
#include "runtime.c"
#include "../../driver/video/vga.c"
#include "../../driver/video/vbe.c"
#include "../../driver/video/video_modes.c"
#include "interrupts.c"
#include "exceptions.c"
#include "../../driver/video/graphics.c"
#include "../../modes/states/graphical/desktop/ui/desktop_icons.c"
#include "rtc.c"
#include "random.c"
#include "../../modes/states/graphical/desktop/apps/task_manager/system_api.c"
#include "disk_info.c"
#include "system_specs.c"
#include "../../modes/states/graphical/desktop/ui/redraw.c"
#include "performance.c"
#include "../../driver/input/keyboard_mouse.c"
#include "../../modes/states/graphical/desktop/apps/terminal/model.c"
#include "../../modes/states/graphical/desktop/apps/snake/logic.c"
#include "../../modes/states/graphical/desktop/apps/guess_number/logic.c"
#include "../../modes/states/graphical/desktop/apps/mines/logic.c"
#include "../../modes/states/graphical/desktop/ui/window_manager.c"
#include "power.c"
#include "../../modes/states/graphical/desktop/apps/terminal/commands.c"
#include "../debug/debugger.c"
#include "../../modes/states/graphical/desktop/apps/settings/logic.c"
#include "../../modes/states/graphical/desktop/apps/mines/handling.c"
#include "../../modes/states/graphical/desktop/apps/guess_number/handling.c"
#include "../../modes/states/graphical/desktop/apps/snake/handling.c"
#include "../../modes/states/graphical/desktop/ui/keyboard_routing.c"
#include "../../modes/states/graphical/desktop/ui/widgets.c"
#include "../../modes/states/graphical/desktop/ui/window_chrome.c"
#include "../../modes/states/graphical/desktop/apps/terminal/render.c"
#include "../../modes/states/graphical/desktop/apps/notepad/notepad.c"
#include "../../modes/states/graphical/desktop/apps/explorer/explorer.c"
#include "../../modes/states/graphical/desktop/apps/paint/paint.c"
#include "../../modes/states/graphical/desktop/apps/snake/render.c"
#include "../../modes/states/graphical/desktop/apps/guess_number/render.c"
#include "../../modes/states/graphical/desktop/apps/mines/render.c"
#include "../../modes/states/graphical/desktop/apps/game_center/game_center.c"
#include "../../modes/states/graphical/desktop/apps/demo_center/demo_center.c"
#include "../../modes/states/graphical/desktop/apps/3DBox/box3d.c"
#include "../../modes/states/graphical/desktop/apps/firecracker/firecracker.c"
#include "../../modes/states/graphical/desktop/apps/power/power.c"
#include "../../modes/states/graphical/desktop/apps/settings/render.c"
#include "../../modes/states/graphical/desktop/apps/task_manager/render.c"
#include "../../modes/states/graphical/desktop/apps/app_dispatch.c"
#include "../../modes/states/graphical/desktop/ui/shell.c"
#include "../debug/render.c"
#include "../../modes/states/graphical/desktop/ui/cursor.c"
#include "../../modes/states/boot/terminal.c"
#include "../../modes/states/boot/menu.c"
#include "../../modes/states/graphical/login/ui/login.c"
#include "../../modes/states/graphical/desktop/ui/desktop.c"
#include "shutdown.c"
#include "render_dispatch.c"
#include "../../modes/states/boot/handling.c"
#include "../../modes/states/graphical/login/ui/handling.c"
#include "../../modes/states/graphical/desktop/ui/start_menu.c"
#include "../../modes/states/graphical/desktop/ui/context_menu.c"
#include "../../modes/states/graphical/desktop/ui/icon_actions.c"
#include "../../modes/states/graphical/desktop/apps/paint/handling.c"
#include "../../modes/states/graphical/desktop/apps/explorer/handling.c"
#include "../../modes/states/graphical/desktop/apps/game_center/handling.c"
#include "../../modes/states/graphical/desktop/apps/demo_center/handling.c"
#include "../../modes/states/graphical/desktop/apps/firecracker/handling.c"
#include "../../modes/states/graphical/desktop/apps/power/handling.c"
#include "../../modes/states/graphical/desktop/apps/settings/handling.c"
#include "../../modes/states/graphical/desktop/apps/task_manager/handling.c"
#include "../../modes/states/graphical/desktop/ui/mouse.c"
#include "state_update.c"
#include "init.c"

/* DOOM engine amalgamation: included LAST. Its headers #define
 * KEY_ENTER/KEY_TAB/etc. as DOOM key codes (doomdef.h), which would
 * clobber the kernel's KeyCode enum names for every file below - so
 * nothing kernel-side may follow this include. */
#include "../../modes/states/graphical/desktop/apps/DOOM/port/doom_engine.c"

// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: power.c, power menu app.

// This repository is licensed under the GNU General Public License.

/*
 * Power Options dialog geometry, in ONE place.
 *
 * The dialog is centred in the desktop area - the canvas, less the taskbar -
 * instead of being parked at the coordinates it was first drawn at.  Those
 * were 212,150, which is the centre of a 640x480 desktop and nothing like the
 * centre of any other size, so on a larger desktop the dialog sat up and to
 * the left of where it belonged.
 *
 * The renderer and the click handler share these numbers, including each
 * row's offset.  They used to carry separate copies of them (plus a third
 * copy of the dialog rectangle in the outside-click test), which is a hit
 * box waiting to drift away from the button it belongs to.
 */
#define POWER_OVERLAY_W      216
#define POWER_OVERLAY_H      158
#define POWER_BUTTON_W       160
#define POWER_BUTTON_H        24
#define POWER_BUTTON_INSET    28   /* button column inside the dialog */
#define POWER_ROW_TITLE       12
#define POWER_ROW_SHUTDOWN    34
#define POWER_ROW_RESTART     66
#define POWER_ROW_HALT        98
#define POWER_ROW_CLOSE      126
#define POWER_ROW_CLOSE_H     20   /* Close is a shorter row than the rest */

/* Top-left corner of the dialog for the desktop that is on screen now. */
static void power_overlay_origin(int *x, int *y) {
    int desktop_h = OS_HEIGHT - TASKBAR_H;

    *x = (OS_WIDTH - POWER_OVERLAY_W) / 2;
    *y = (desktop_h - POWER_OVERLAY_H) / 2;
    if (*x < 0) {
        *x = 0;
    }
    if (*y < 0) {
        *y = 0;
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

static void render_power_overlay(void) {
    int x;
    int y;

    if (!power_menu_open) {
        return;
    }
    power_overlay_origin(&x, &y);
    fill_rect(x, y, POWER_OVERLAY_W, POWER_OVERLAY_H, color_gray_light);
    draw_rect(x, y, POWER_OVERLAY_W, POWER_OVERLAY_H, color_black);
    draw_text_center(x + POWER_OVERLAY_W / 2, y + POWER_ROW_TITLE, "Power Options",
                     color_blue_dark, color_gray_light, true);
    draw_button(x + POWER_BUTTON_INSET, y + POWER_ROW_SHUTDOWN, POWER_BUTTON_W, POWER_BUTTON_H,
                "Shutdown", color_gray_light, color_black, color_black);
    draw_button(x + POWER_BUTTON_INSET, y + POWER_ROW_RESTART, POWER_BUTTON_W, POWER_BUTTON_H,
                "Restart", color_gray_light, color_black, color_black);
    draw_button(x + POWER_BUTTON_INSET, y + POWER_ROW_HALT, POWER_BUTTON_W, POWER_BUTTON_H,
                "Halt", color_gray_light, color_black, color_black);
    draw_button(x + POWER_BUTTON_INSET, y + POWER_ROW_CLOSE, POWER_BUTTON_W, POWER_ROW_CLOSE_H,
                "Close", color_gray_light, color_black, color_black);
}

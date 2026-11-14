// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: palette.c, Coin Run! VGA-16 palette mapping.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

static uint8_t coin_run_colors[16];
static bool coin_run_colors_ready;

/* The original mode-13h game only uses the first 16 VGA palette entries,
 * mapped once onto whatever palette the OS currently presents. */
static void coin_run_prepare_colors(void) {
    static const uint8_t vga[16][3] = {
        {0, 0, 0},       {0, 0, 170},     {0, 170, 0},     {0, 170, 170},
        {170, 0, 0},     {170, 0, 170},   {170, 85, 0},    {170, 170, 170},
        {85, 85, 85},    {85, 85, 255},   {85, 255, 85},   {85, 255, 255},
        {255, 85, 85},   {255, 85, 255},  {255, 255, 85},  {255, 255, 255}
    };

    if (coin_run_colors_ready) {
        return;
    }
    for (int i = 0; i < 16; ++i) {
        coin_run_colors[i] = nearest_color(vga[i][0], vga[i][1], vga[i][2]);
    }
    coin_run_colors_ready = true;
}

static uint8_t coin_run_color(int index) {
    return coin_run_colors[index & 15];
}

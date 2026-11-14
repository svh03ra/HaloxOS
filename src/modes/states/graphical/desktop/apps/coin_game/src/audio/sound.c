// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: sound.c, Coin Run! PC speaker sound effects.

// This is a direct port of the MBR Game into HaloxOS: https://github.com/svh03ra/mbr-demos/tree/main/coin-game
// This repository is licensed under the GNU General Public License.

#include "../coin_run.h"

/*
 * Non-blocking PIT channel 2 tones. The tone divisor is 1193180 / frequency,
 * and the speaker gate lives in port 61h bits 0-1.
 *
 * The cue scheduler is driven by the 60 Hz system tick instead of the
 * 18.2 Hz game tick. That gives every note a steady wall-clock length and
 * stops a slow frame (which runs several game steps back to back) from
 * firing half a cue in one burst. Coin pickups re-trigger the short chime
 * immediately rather than queueing behind it, so collecting fast never
 * leaves the beep trailing the player.
 */
#define COIN_RUN_COIN_NOTE_TICKS    (TIMER_HZ / 20)  /* 2 notes, ~0.10s total */
#define COIN_RUN_HURT_NOTE_TICKS    (TIMER_HZ / 6)   /* 2 notes, ~0.33s total */
#define COIN_RUN_POWERUP_NOTE_TICKS (TIMER_HZ / 20)  /* 4 notes, ~0.20s total */
#define COIN_RUN_COIN_QUEUE_MAX     2

static bool coin_run_beep_active;
static bool coin_run_beep_is_coin;
static bool coin_run_speaker_on;
static int coin_run_beep_note;
static int coin_run_beep_note_count;
static int coin_run_beep_note_ticks;
static uint32_t coin_run_beep_deadline;
static uint16_t coin_run_beep_counts[4];
static int coin_run_coin_queue;

static void coin_run_speaker_program(uint16_t divisor) {
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)(divisor >> 8));
    outb(0x61, (uint8_t)(inb(0x61) | 0x03));
    coin_run_speaker_on = true;
}

static void coin_run_speaker_off(void) {
    if (coin_run_speaker_on) {
        outb(0x61, (uint8_t)(inb(0x61) & 0xFC));
        coin_run_speaker_on = false;
    }
}

static void coin_run_speaker_start_note(void) {
    int note = coin_run_beep_note;

    if (note < 0) note = 0;
    if (note > 3) note = 3;
    coin_run_speaker_program(coin_run_beep_counts[note]);
    coin_run_beep_deadline =
        timer_ticks + (uint32_t)coin_run_beep_note_ticks;
}

static void coin_run_speaker_start(int note_count, int note_ticks, bool is_coin) {
    coin_run_beep_note = 0;
    coin_run_beep_note_count = note_count;
    coin_run_beep_note_ticks = note_ticks;
    coin_run_beep_is_coin = is_coin;
    coin_run_beep_active = true;
    coin_run_speaker_start_note();
}

static void coin_run_speaker_silence(void) {
    coin_run_beep_active = false;
    coin_run_beep_is_coin = false;
    coin_run_beep_note = 0;
    coin_run_coin_queue = 0;
    coin_run_speaker_off();
}

/* Coin collect: C6 (1046 Hz) -> E6 (1319 Hz), retriggered while active. */
static void coin_run_play_coin_sound(void) {
    if (coin_run_beep_active) {
        if (coin_run_beep_is_coin) {
            coin_run_beep_note = 0;
            coin_run_speaker_start_note();
        } else if (coin_run_coin_queue < COIN_RUN_COIN_QUEUE_MAX) {
            ++coin_run_coin_queue;
        }
        return;
    }
    coin_run_beep_counts[0] = 1140;
    coin_run_beep_counts[1] = 905;
    coin_run_speaker_start(2, COIN_RUN_COIN_NOTE_TICKS, true);
}

/* Lose-life hurt: A3 (220 Hz) -> A2 (110 Hz), a longer two-note fall. */
static void coin_run_play_hurt_sound(void) {
    coin_run_coin_queue = 0;
    coin_run_beep_counts[0] = 5424;
    coin_run_beep_counts[1] = 10847;
    coin_run_speaker_start(2, COIN_RUN_HURT_NOTE_TICKS, false);
}

/* Powerup: C5 -> E5 -> G5 -> C6 arpeggio. */
static void coin_run_play_powerup_sound(void) {
    coin_run_coin_queue = 0;
    coin_run_beep_counts[0] = 2273;
    coin_run_beep_counts[1] = 1807;
    coin_run_beep_counts[2] = 1517;
    coin_run_beep_counts[3] = 1140;
    coin_run_speaker_start(4, COIN_RUN_POWERUP_NOTE_TICKS, false);
}

static void coin_run_update_sound(void) {
    if (!coin_run_beep_active) {
        return;
    }
    if ((int32_t)(timer_ticks - coin_run_beep_deadline) < 0) {
        return;
    }

    ++coin_run_beep_note;
    if (coin_run_beep_note < coin_run_beep_note_count) {
        coin_run_speaker_start_note();
        return;
    }

    coin_run_beep_active = false;
    coin_run_beep_is_coin = false;
    if (coin_run_coin_queue > 0) {
        --coin_run_coin_queue;
        coin_run_play_coin_sound();
        return;
    }
    coin_run_speaker_off();
}

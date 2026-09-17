// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: rtc.c, RTC Clock timing.

// This repository is licensed under the GNU General Public License.

static uint8_t cmos_read(uint8_t index) {
    outb(0x70, index);
    io_wait();
    return inb(0x71);
}

static uint8_t from_bcd(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value / 16) * 10));
}

/* Cached datetime string. CMOS reads are expensive port I/O on real
 * hardware and under cycle-accurate emulators (each outb/inb pair is
 * ~1us of ISA bus time); the old path re-read 14+ registers with
 * double-validation on every caller - three times per frame. Refresh
 * at most once per second instead, keyed on the RTC seconds field. */
static char datetime_cache[24];
static bool datetime_cache_valid = false;
static uint8_t datetime_cache_second = 0xFF;

/*
 * Taskbar clock widget: a 12-hour time line with seconds over a
 * DD/Mon/YYYY date line, both centered inside one 11-column box - the
 * widest the widget can ever read, since "12:59:59 AM" and "02/Jan/2026"
 * are both 11 characters - so the two lines keep a common center instead
 * of drifting apart.
 *
 * The fields below are stashed next to the string cache, so the widget
 * costs no extra CMOS traffic: the once-per-second refresh already read
 * them.
 */
#define CLOCK_BOX_COLS 11
#define CLOCK_BOX_W (CLOCK_BOX_COLS * 8)
#define CLOCK_LINE_H 8
#define CLOCK_LINE_ADVANCE 10
#define CLOCK_BOX_H (CLOCK_LINE_H + CLOCK_LINE_ADVANCE)

static uint8_t clock_hour12 = 12;   /* 1..12, how the hour is read */
static uint8_t clock_minute = 0;
static uint8_t clock_second = 0;
static bool clock_is_pm = false;
static uint8_t clock_day = 1;
static uint8_t clock_month = 1;
static uint16_t clock_year = 2026;  /* four-digit year, e.g. 2026 */
/* False until a reading actually succeeds, so a machine whose RTC never
 * answers shows "no clock" rather than an invented time. */
static bool clock_fields_valid = false;
static uint32_t datetime_retry_tick = 0;

/*
 * Read the six time/date registers twice and accept them only when both
 * laps agree - which is what a straddled update looks like.
 *
 * Deliberately does NOT wait on the update-in-progress flag (register 0x0A
 * bit 7). VMware's virtual RTC leaves that bit set, so the old code - which
 * treated "update in progress" as "do not read the clock at all", and which
 * spun on the same flag inside its validation lap - either froze the clock
 * or would have hung the machine outright. Agreement between two reads is
 * correct whether or not that flag means anything, and unlike the spins it
 * always terminates.
 */
#define RTC_READ_ATTEMPTS 8

static bool read_datetime_registers(uint8_t *second, uint8_t *minute, uint8_t *hour,
                                   uint8_t *day, uint8_t *month, uint8_t *year) {
    for (int attempt = 0; attempt < RTC_READ_ATTEMPTS; ++attempt) {
        uint8_t s1 = cmos_read(0x00);
        uint8_t m1 = cmos_read(0x02);
        uint8_t h1 = cmos_read(0x04);
        uint8_t d1 = cmos_read(0x07);
        uint8_t mo1 = cmos_read(0x08);
        uint8_t y1 = cmos_read(0x09);
        uint8_t s2 = cmos_read(0x00);
        uint8_t m2 = cmos_read(0x02);
        uint8_t h2 = cmos_read(0x04);
        uint8_t d2 = cmos_read(0x07);
        uint8_t mo2 = cmos_read(0x08);
        uint8_t y2 = cmos_read(0x09);

        if (s1 == s2 && m1 == m2 && h1 == h2 && d1 == d2 && mo1 == mo2 && y1 == y2) {
            *second = s1;
            *minute = m1;
            *hour = h1;
            *day = d1;
            *month = mo1;
            *year = y1;
            return true;
        }
    }
    return false;
}

static const char *month_name(uint8_t month) {
    static const char *names[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (month < 1 || month > 12) {
        return "---";
    }
    return names[month - 1];
}

static void append_cstr(char *dest, size_t *len, size_t max_len, const char *src) {
    while (*src != '\0') {
        append_char(dest, len, max_len, *src);
        ++src;
    }
}

/* Returns true only when a consistent reading was obtained. On success the
 * canonical string, the stashed widget fields and the cache key are all
 * updated; on failure nothing here changes, so a machine with no usable
 * RTC keeps showing the last good time (or the "no clock" placeholder on
 * a first-ever failure). */
static bool read_datetime_uncached(char *buffer, size_t max_len) {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t reg_b;
    bool pm_flag;
    size_t len = 0;

    if (!read_datetime_registers(&second, &minute, &hour, &day, &month, &year)) {
        return false;
    }

    /* Cache key: the RAW seconds byte - refresh_datetime_cache() compares a
     * raw read against it, before any BCD decoding. */
    datetime_cache_second = second;

    /* The 12-hour PM flag rides on bit 7 of the hour register; keep it
     * before masking, so the validated lap needs no extra CMOS read. */
    pm_flag = (bool)((hour & 0x80) != 0);
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

    /* A 12-hour RTC (register B bit 1 clear) stores 1..12 plus a PM flag.
     * Convert it properly: 12 AM is midnight and 12 PM is noon - the old
     * ((hour + 12) % 24) turned noon into midnight. */
    if ((reg_b & 0x02) == 0) {
        hour = (uint8_t)(hour % 12);
        if (pm_flag) {
            hour = (uint8_t)(hour + 12);
        }
    }

    /* 12-hour split for the taskbar clock: hour is 24-hour by now, so
     * midnight (00:xx) and noon (12:xx) both read as 12 and the meridiem
     * is simply "hour past noon". */
    clock_is_pm = (bool)(hour >= 12);
    clock_hour12 = (uint8_t)(hour % 12);
    if (clock_hour12 == 0) {
        clock_hour12 = 12;
    }
    clock_minute = minute;
    clock_second = second;
    clock_day = day;
    clock_month = month;
    clock_year = (uint16_t)(2000u + year);
    clock_fields_valid = true;

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

    datetime_cache_valid = true;
    return true;
}

/* Refresh the once-per-second RTC cache (and the stashed clock fields).
 *
 * Keyed on the seconds register - one inb per frame - so the six-register
 * validated read happens once a second instead of on every caller. A failed
 * read leaves the previous values in place and backs off for a quarter of a
 * second, so a machine whose RTC never answers cannot turn the refresh into
 * a bus-hogging retry loop. */
static void refresh_datetime_cache(void) {
    uint8_t now_second;

    if ((int32_t)(timer_ticks - datetime_retry_tick) < 0) {
        return;
    }

    now_second = cmos_read(0x00);
    if (datetime_cache_valid && now_second == datetime_cache_second) {
        return;
    }

    if (!read_datetime_uncached(datetime_cache, sizeof(datetime_cache))) {
        datetime_retry_tick = timer_ticks + (TIMER_HZ / 4u);
    }
}

static void read_datetime(char *buffer, size_t max_len) {
    refresh_datetime_cache();
    copy_string(buffer, datetime_cache, max_len);
}

/* The two taskbar clock lines, e.g. "3:20 PM" over "30/05/26". Every
 * character is 8 px wide in the OS font, hence the *8 geometry below. */
static void clock_widget_lines(char *time_out, size_t time_max,
                               char *date_out, size_t date_max) {
    size_t tlen = 0;
    size_t dlen = 0;

    time_out[0] = '\0';
    date_out[0] = '\0';
    refresh_datetime_cache();

    if (!clock_fields_valid) {
        copy_string(time_out, "--:--:-- --", time_max);
        copy_string(date_out, "--/---/----", date_max);
        return;
    }

    append_padded_uint(time_out, &tlen, time_max, clock_hour12, 2);
    append_char(time_out, &tlen, time_max, ':');
    append_padded_uint(time_out, &tlen, time_max, clock_minute, 2);
    append_char(time_out, &tlen, time_max, ':');
    append_padded_uint(time_out, &tlen, time_max, clock_second, 2);
    append_char(time_out, &tlen, time_max, ' ');
    append_char(time_out, &tlen, time_max, clock_is_pm ? 'P' : 'A');
    append_char(time_out, &tlen, time_max, 'M');

    append_padded_uint(date_out, &dlen, date_max, clock_day, 2);
    append_char(date_out, &dlen, date_max, '/');
    append_cstr(date_out, &dlen, date_max, month_name(clock_month));
    append_char(date_out, &dlen, date_max, '/');
    append_uint(date_out, &dlen, date_max, clock_year);
}

/* Geometry shared by the taskbar renderer and its hit-testing, so the
 * pixels and the clickable area can never disagree. */
static int clock_widget_x(void) {
    return OS_WIDTH - CLOCK_BOX_W - 8;
}

static int clock_widget_y(void) {
    return OS_HEIGHT - TASKBAR_H + (TASKBAR_H - CLOCK_BOX_H) / 2;
}

/* x of one clock line, centered inside the shared box. */
static int clock_line_x(const char *line) {
    int width = (int)strlen_local(line) * 8;
    if (width > CLOCK_BOX_W) {
        width = CLOCK_BOX_W;
    }
    return clock_widget_x() + (CLOCK_BOX_W - width) / 2;
}

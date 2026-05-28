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

static void read_datetime(char *buffer, size_t max_len) {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t second_check;
    uint8_t minute_check;
    uint8_t hour_check;
    uint8_t day_check;
    uint8_t month_check;
    uint8_t year_check;
    uint8_t reg_b;
    size_t len = 0;

    do {
        while (cmos_read(0x0A) & 0x80) {
        }
        second = cmos_read(0x00);
        minute = cmos_read(0x02);
        hour = cmos_read(0x04);
        day = cmos_read(0x07);
        month = cmos_read(0x08);
        year = cmos_read(0x09);
        while (cmos_read(0x0A) & 0x80) {
        }
        second_check = cmos_read(0x00);
        minute_check = cmos_read(0x02);
        hour_check = cmos_read(0x04);
        day_check = cmos_read(0x07);
        month_check = cmos_read(0x08);
        year_check = cmos_read(0x09);
    } while (second != second_check || minute != minute_check || hour != hour_check ||
             day != day_check || month != month_check || year != year_check);

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

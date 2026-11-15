#include "header/driver/cmos.h"
#include "header/cpu/portio.h"
#include <stddef.h>

uint8_t cmos_get_rtc_register(uint8_t reg) {
    out(CMOS_ADDRESS_PORT, reg);
    return in(CMOS_DATA_PORT);
}

int cmos_get_update_in_progress_flag(void) {
    out(CMOS_ADDRESS_PORT, CMOS_REG_STATUS_A);
    return (in(CMOS_DATA_PORT) & 0x80);
}

void cmos_read_rtc(struct RTCTime *time) {
    uint8_t second, minute, hour, day, month, year;
    uint8_t last_second, last_minute, last_hour, last_day, last_month, last_year;
    uint8_t registerB;

    while (cmos_get_update_in_progress_flag());

    second = cmos_get_rtc_register(CMOS_REG_SECONDS);
    minute = cmos_get_rtc_register(CMOS_REG_MINUTES);
    hour   = cmos_get_rtc_register(CMOS_REG_HOURS);
    day    = cmos_get_rtc_register(CMOS_REG_DAY);
    month  = cmos_get_rtc_register(CMOS_REG_MONTH);
    year   = cmos_get_rtc_register(CMOS_REG_YEAR);

    do {
        last_second = second;
        last_minute = minute;
        last_hour   = hour;
        last_day    = day;
        last_month  = month;
        last_year   = year;

        while (cmos_get_update_in_progress_flag());

        second = cmos_get_rtc_register(CMOS_REG_SECONDS);
        minute = cmos_get_rtc_register(CMOS_REG_MINUTES);
        hour   = cmos_get_rtc_register(CMOS_REG_HOURS);
        day    = cmos_get_rtc_register(CMOS_REG_DAY);
        month  = cmos_get_rtc_register(CMOS_REG_MONTH);
        year   = cmos_get_rtc_register(CMOS_REG_YEAR);
    } while (last_second != second || last_minute != minute || last_hour != hour ||
             last_day != day || last_month != month || last_year != year);

    registerB = cmos_get_rtc_register(CMOS_REG_STATUS_B);

    if (!(registerB & 0x04)) {
        second = (second & 0x0F) + ((second / 16) * 10);
        minute = (minute & 0x0F) + ((minute / 16) * 10);
        hour   = ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80);
        day    = (day & 0x0F) + ((day / 16) * 10);
        month  = (month & 0x0F) + ((month / 16) * 10);
        year   = (year & 0x0F) + ((year / 16) * 10);
    }

    if (!(registerB & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    if (time != NULL) {
        time->second = second;
        time->minute = minute;
        time->hour   = hour;
        time->day    = day;
        time->month  = month;
        time->year   = 2000 + year;
    }
}
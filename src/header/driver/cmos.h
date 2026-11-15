#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>

#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71

#define CMOS_REG_SECONDS  0x00
#define CMOS_REG_MINUTES  0x02
#define CMOS_REG_HOURS    0x04
#define CMOS_REG_DAY      0x07
#define CMOS_REG_MONTH    0x08
#define CMOS_REG_YEAR     0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B

struct RTCTime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

uint8_t cmos_get_rtc_register(uint8_t reg);
int cmos_get_update_in_progress_flag(void);
void cmos_read_rtc(struct RTCTime *time);

#endif
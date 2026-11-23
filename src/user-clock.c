#include <stdint.h>
#include <stdbool.h>

struct RTCTime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

int main(void) {
    uint8_t last_sec = 0xFF;
    struct RTCTime t;

    while (true) {
        syscall(14, (uint32_t) &t, 0, 0);

        if (t.second != last_sec) {
            last_sec = t.second;

            uint8_t hour_wib = (t.hour + 7) % 24;

            // Format:  HH:MM:SS 
            char buf[11];
            buf[0] = ' ';
            buf[1] = (hour_wib / 10) + '0';
            buf[2] = (hour_wib % 10) + '0';
            buf[3] = ':';
            buf[4] = (t.minute / 10) + '0';
            buf[5] = (t.minute % 10) + '0';
            buf[6] = ':';
            buf[7] = (t.second / 10) + '0';
            buf[8] = (t.second % 10) + '0';
            buf[9] = ' ';
            buf[10] = '\0';

            syscall(15, (uint32_t) buf, (29 << 16) | 70, 0x0E);
        }

        for (volatile int i = 0; i < 50000; i++);
    }

    return 0;
}
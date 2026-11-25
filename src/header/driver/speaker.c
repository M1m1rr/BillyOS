#include "header/driver/speaker.h"
#include "header/cpu/portio.h"

#define PIT_OSCILLATOR_HZ     1193182
#define PIT_COMMAND_PORT      0x43
#define PIT_CHANNEL2_PORT     0x42
#define PC_SPEAKER_PORT       0x61

#define PIT_CHANNEL2_SQUARE   0xB6

void speaker_play(uint32_t frequency_hz) {
    if (frequency_hz == 0) {
        speaker_stop();
        return;
    }

    uint32_t divisor = PIT_OSCILLATOR_HZ / frequency_hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    if (divisor == 0) divisor = 1;

    out(PIT_COMMAND_PORT, PIT_CHANNEL2_SQUARE);
    out(PIT_CHANNEL2_PORT, (uint8_t)(divisor & 0xFF));
    out(PIT_CHANNEL2_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t status = in(PC_SPEAKER_PORT);
    if ((status & 0x03) != 0x03) {
        out(PC_SPEAKER_PORT, (uint8_t)(status | 0x03));
    }
}

void speaker_stop(void) {
    uint8_t status = in(PC_SPEAKER_PORT);
    out(PC_SPEAKER_PORT, (uint8_t)(status & 0xFC));
}

void speaker_beep(uint32_t frequency_hz, uint32_t duration_ms) {
    speaker_play(frequency_hz);
    for (volatile uint32_t i = 0; i < duration_ms * 3000; i++) {
        __asm__ volatile("pause");
    }
    speaker_stop();
}
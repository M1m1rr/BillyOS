#ifndef _SPEAKER_H
#define _SPEAKER_H

#include <stdint.h>
#include <stdbool.h>

void speaker_play(uint32_t frequency_hz);

void speaker_stop(void);

void speaker_beep(uint32_t frequency_hz, uint32_t duration_ms);

#endif
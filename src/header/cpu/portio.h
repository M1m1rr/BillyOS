#ifndef _PORTIO_H
#define _PORTIO_H

#include <stdint.h>

void out(uint16_t port, uint8_t data);
uint8_t in(uint16_t port);

#endif
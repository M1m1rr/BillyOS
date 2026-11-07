#ifndef _FRAMEBUFFER_H
#define _FRAMEBUFFER_H

#include <stdint.h>

#define FRAMEBUFFER_MEMORY_OFFSET ((uint8_t*) 0xC00B8000)
#define FRAMEBUFFER_WIDTH 80
#define FRAMEBUFFER_HEIGHT 25
#define CURSOR_PORT_CMD    0x03D4
#define CURSOR_PORT_DATA   0x03D5

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);
void framebuffer_set_cursor(uint8_t r, uint8_t c);
void framebuffer_clear(void);
void console_set_position(uint32_t row, uint32_t col);
void console_putchar(char c, uint8_t color);
void console_puts(const char *s, uint32_t len, uint8_t color);

#endif
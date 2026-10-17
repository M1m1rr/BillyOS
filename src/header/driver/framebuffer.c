#include "header/driver/framebuffer.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint16_t offset = 2 * (row * FRAMEBUFFER_WIDTH + col);
    FRAMEBUFFER_MEMORY_OFFSET[offset]     = c;
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (uint8_t) ((bg << 4) | (fg & 0x0F));
}

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t pos = r * FRAMEBUFFER_WIDTH + c;
    out(0x3D4, 0x0F);
    out(0x3D5, (uint8_t) (pos & 0xFF));
    out(0x3D4, 0x0E);
    out(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

void framebuffer_clear(void) {
    memset(FRAMEBUFFER_MEMORY_OFFSET, 0, FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * 2);
}
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

static uint32_t console_row = 0;
static uint32_t console_col = 0;

void console_set_position(uint32_t row, uint32_t col) {
    console_row = row;
    console_col = col;
}

static void console_scroll(void) {
    uint32_t row_bytes = FRAMEBUFFER_WIDTH * 2;
    for (uint32_t i = 0; i < (FRAMEBUFFER_HEIGHT - 1) * row_bytes; i++)
        FRAMEBUFFER_MEMORY_OFFSET[i] = FRAMEBUFFER_MEMORY_OFFSET[i + row_bytes];
    for (uint32_t i = (FRAMEBUFFER_HEIGHT - 1) * row_bytes; i < FRAMEBUFFER_HEIGHT * row_bytes; i += 2) {
        FRAMEBUFFER_MEMORY_OFFSET[i]     = ' ';
        FRAMEBUFFER_MEMORY_OFFSET[i + 1] = 0x07;
    }
}

static void console_normalize(void) {
    while (console_row >= FRAMEBUFFER_HEIGHT) {
        console_scroll();
        console_row--;
    }
}

void console_putchar(char c, uint8_t color) {
    if(c == 0) return;

    console_normalize();
    if (c == '\n') {
        console_col = 0;
        console_row++;
    } else if (c == '\b') {
        if (console_col > 0) {
            console_col--;
            framebuffer_write(console_row, console_col, ' ', 0xF, 0);
        }
    } else {
        framebuffer_write(console_row, console_col, c, color, 0);
        console_col++;
        if (console_col >= FRAMEBUFFER_WIDTH) {
            console_col = 0;
            console_row++;
        }
    }
    console_normalize();
    framebuffer_set_cursor(console_row, console_col);
}

void console_puts(const char *s, uint32_t len, uint8_t color) {
    for (uint32_t i = 0; i < len; i++)
        console_putchar(s[i], color);
}
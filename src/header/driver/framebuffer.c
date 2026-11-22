#include "header/driver/framebuffer.h"
#include "header/driver/vga.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    if (vga_is_graphics_mode()) {
        vga_draw_char(col, row, c, fg, bg);
        return;
    }
    uint16_t offset = 2 * (row * FRAMEBUFFER_WIDTH + col);
    FRAMEBUFFER_MEMORY_OFFSET[offset]     = c;
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (uint8_t) ((bg << 4) | (fg & 0x0F));
}

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    if (vga_is_graphics_mode()) {
        vga_set_cursor(r, c);
        return;
    }
    uint16_t pos = r * FRAMEBUFFER_WIDTH + c;
    out(0x3D4, 0x0F);
    out(0x3D5, (uint8_t) (pos & 0xFF));
    out(0x3D4, 0x0E);
    out(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

void framebuffer_clear(void) {
    if (vga_is_graphics_mode()) {
        vga_clear_screen(0);
        return;
    }
    memset(FRAMEBUFFER_MEMORY_OFFSET, 0, FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * 2);
}

static uint32_t console_row = 0;
static uint32_t console_col = 0;

void console_set_position(uint32_t row, uint32_t col) {
    console_row = row;
    console_col = col;
}

static void console_scroll(void) {
    if (vga_is_graphics_mode()) {
        vga_scroll();
        return;
    }
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

static uint8_t utf8_buf[4];
static uint8_t utf8_len = 0;

void console_putchar(char c, uint8_t color) {
    if (c == 0) return;

    // Decoder UTF-8 Ringan untuk karakter blok grafis
    uint8_t u = (uint8_t) c;
    if (utf8_len == 0) {
        if ((u & 0x80) == 0) {
            // ASCII standar 1 byte, lanjutkan proses biasa di bawah
        } else if ((u & 0xE0) == 0xC0) {
            utf8_buf[0] = u; utf8_len = 1; return;
        } else if ((u & 0xF0) == 0xE0) {
            utf8_buf[0] = u; utf8_len = 1; return;
        } else {
            return;
        }
    } else if (utf8_len == 1) {
        utf8_buf[1] = u;
        if ((utf8_buf[0] & 0xF0) == 0xE0) {
            utf8_len = 2; return;
        }
        utf8_len = 0;
        return;
    } else if (utf8_len == 2) {
        utf8_buf[2] = u;
        utf8_len = 0;
        uint32_t cp = ((utf8_buf[0] & 0x0F) << 12) | ((utf8_buf[1] & 0x3F) << 6) | (utf8_buf[2] & 0x3F);

        if (cp == 0x2588)      c = (char) 0xDB; // █ Full block
        else if (cp == 0x2580) c = (char) 0xDF; // ▀ Upper half
        else if (cp == 0x2584) c = (char) 0xDC; // ▄ Lower half
        else if (cp == 0x258C) c = (char) 0xDD; // ▌ Left half
        else if (cp == 0x2590) c = (char) 0xDE; // ▐ Right half
        else if (cp == 0x2591) c = (char) 0xB0; // ░ Light shade
        else if (cp == 0x2592) c = (char) 0xB1; // ▒ Medium shade
        else if (cp == 0x2593) c = (char) 0xB2; // ▓ Dark shade
        else if (cp == 0x25C4) c = (char) 0x11; // ◄ Left pointer
        else if (cp == 0x25BA) c = (char) 0x10; // ► Right pointer
        else c = ' ';
    }

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
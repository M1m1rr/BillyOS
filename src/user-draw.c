#include <stdint.h>
#include <stdbool.h>

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static void draw_pixel(int x, int y, uint8_t color) {
    syscall(16, (uint32_t) x, (uint32_t) y, (uint32_t) color);
}

static void draw_rect(int x, int y, int w, int h, uint8_t color, bool fill) {
    uint32_t eb = (x << 16) | (y & 0xFFFF);
    uint32_t ec = (w << 16) | (h & 0xFFFF);
    uint32_t ed = color | (fill ? (1 << 8) : 0);
    syscall(17, eb, ec, ed);
}

static void draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    uint32_t eb = (x0 << 16) | (y0 & 0xFFFF);
    uint32_t ec = (x1 << 16) | (y1 & 0xFFFF);
    syscall(18, eb, ec, (uint32_t) color);
}

static void sys_clear(void) {
    syscall(8, 0, 0, 0);
}

static void sys_puts_at(const char *str, uint8_t row, uint8_t col, uint8_t color) {
    uint32_t pos = (row << 16) | col;
    syscall(15, (uint32_t) str, pos, (uint32_t) color);
}

static void wait_for_key(void) {
    char c = 0;
    while (c == 0) {
        syscall(4, (uint32_t) &c, 0, 0);
    }
}

int main(void) {
    sys_clear();

    sys_puts_at("16-Color Palette Grid:", 3, 4, 0x0F);
    for (int i = 0; i < 16; i++) {
        draw_rect(32 + i * 36, 70, 32, 28, i, true);
        draw_rect(32 + i * 36, 70, 32, 28, 15, false);
    }

    sys_puts_at("Per-Pixel Line Art:", 8, 4, 0x0B);
    for (int i = 0; i < 180; i += 15) {
        draw_line(40, 160 + i, 240, 340 - i, 11);
        draw_line(240, 160 + i, 40, 340 - i, 14);
    }

    sys_puts_at("Nested & Filled Rectangles:", 8, 40, 0x0A);
    draw_rect(320, 160, 270, 180, 9, false);
    draw_rect(340, 180, 230, 140, 10, false);
    draw_rect(360, 200, 190, 100, 12, true);

    sys_puts_at("Individual Pixels (put_pixel):", 24, 4, 0x0D);
    for (int x = 32; x < 608; x += 4) {
        draw_pixel(x, 410, 14);
    }

    sys_puts_at("Tekan sembarang tombol untuk kembali ke Shell...", 27, 16, 0x07);

    wait_for_key();

    sys_clear();

    return 0;
}
#ifndef _VGA_H
#define _VGA_H

#include <stdint.h>
#include <stdbool.h>

#define VGA_WIDTH       640
#define VGA_HEIGHT      480
#define VGA_TEXT_COLS   80
#define VGA_TEXT_ROWS   30

#define VGA_COLOR_BLACK         0
#define VGA_COLOR_BLUE          1
#define VGA_COLOR_GREEN         2
#define VGA_COLOR_CYAN          3
#define VGA_COLOR_RED           4
#define VGA_COLOR_MAGENTA       5
#define VGA_COLOR_BROWN         6
#define VGA_COLOR_LIGHT_GRAY    7
#define VGA_COLOR_DARK_GRAY     8
#define VGA_COLOR_LIGHT_BLUE    9
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_CYAN    11
#define VGA_COLOR_LIGHT_RED     12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW        14
#define VGA_COLOR_WHITE         15

void vga_init_graphics_mode(void);
void vga_init_text_mode(void);
bool vga_is_graphics_mode(void);

void vga_put_pixel(int x, int y, uint8_t color);
uint8_t vga_get_pixel(int x, int y);
void vga_draw_line(int x0, int y0, int x1, int y1, uint8_t color);
void vga_draw_rect(int x, int y, int w, int h, uint8_t color, bool fill);
void vga_draw_circle(int xc, int yc, int r, uint8_t color);

void vga_clear_screen(uint8_t color);
void vga_draw_char(int col, int row, char c, uint8_t fg, uint8_t bg);
void vga_set_cursor(int row, int col);
void vga_scroll(void);

#endif
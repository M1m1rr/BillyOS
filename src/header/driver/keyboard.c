#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"
#include "header/driver/framebuffer.h"

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on  = false,
    .keyboard_buffer    = 0,
};

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = 0;
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        pic_ack(IRQ_KEYBOARD);
        return;
    }

    if (keyboard_state.keyboard_input_on) {
        if (!(scancode & 0x80)) {
            if (!keyboard_state.read_extended_mode) {
                char c = keyboard_scancode_1_to_ascii_map[scancode];
                if (c) {
                    keyboard_state.keyboard_buffer = c;
                }
            }
        }
    }

    keyboard_state.read_extended_mode = false;
    pic_ack(IRQ_KEYBOARD);
}

static uint32_t console_row = 0;
static uint32_t console_col = 0;

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

void console_set_position(uint32_t row, uint32_t col) {
    console_row = row;
    console_col = col;
}

void console_putchar(char c, uint8_t color) {
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
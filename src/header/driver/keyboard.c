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

static bool shift_active = false;

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        pic_ack(IRQ_KEYBOARD);
        return;
    }

    // 1. Deteksi tombol Shift (Left Shift: 0x2A, Right Shift: 0x36)
    if (scancode == 0x2A || scancode == 0x36) {
        shift_active = true;
        pic_ack(IRQ_KEYBOARD);
        return;
    }
    // Deteksi saat tombol Shift dilepas (Break Code: bit 7 aktif)
    if (scancode == (0x2A | 0x80) || scancode == (0x36 | 0x80)) {
        shift_active = false;
        pic_ack(IRQ_KEYBOARD);
        return;
    }

    if (keyboard_state.keyboard_input_on) {
        if (!(scancode & 0x80)) {
            if (!keyboard_state.read_extended_mode) {
                char c = keyboard_scancode_1_to_ascii_map[scancode];
                if (c) {
                    // 2. Jika Shift aktif, ubah karakter ke simbol / huruf kapital
                    if (shift_active) {
                        if (c >= 'a' && c <= 'z') c -= 32;       // Huruf kapital A-Z
                        else if (c == '\\') c = '|';             // Backslash menjadi Pipe '|'!
                        else if (c == '1')  c = '!';
                        else if (c == '2')  c = '@';
                        else if (c == '3')  c = '#';
                        else if (c == '4')  c = '$';
                        else if (c == '5')  c = '%';
                        else if (c == '6')  c = '^';
                        else if (c == '7')  c = '&';
                        else if (c == '8')  c = '*';
                        else if (c == '9')  c = '(';
                        else if (c == '0')  c = ')';
                        else if (c == '-')  c = '_';             // Underscore '_'
                        else if (c == '=')  c = '+';
                        else if (c == ';')  c = ':';
                        else if (c == '\'') c = '"';
                        else if (c == ',')  c = '<';
                        else if (c == '.')  c = '>';
                        else if (c == '/')  c = '?';
                    }
                    keyboard_state.keyboard_buffer = c;
                }
            }
        }
    }

    keyboard_state.read_extended_mode = false;
    pic_ack(IRQ_KEYBOARD);
}
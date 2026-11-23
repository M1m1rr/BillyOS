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
    .buffer             = {0},
    .buf_head           = 0,
    .buf_tail           = 0,
};

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    if (keyboard_state.buf_tail != keyboard_state.buf_head) {
        *buf = keyboard_state.buffer[keyboard_state.buf_tail];
        keyboard_state.buf_tail = (keyboard_state.buf_tail + 1) % KEYBOARD_BUFFER_SIZE;
    } else {
        *buf = 0;
    }
}

// Tambahkan karakter ke ring buffer (dipanggil dari ISR)
static void keyboard_buffer_push(char c) {
    uint8_t next = (keyboard_state.buf_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != keyboard_state.buf_tail) {  // Buffer belum penuh
        keyboard_state.buffer[keyboard_state.buf_head] = c;
        keyboard_state.buf_head = next;
    }
}

static bool shift_active = false;

// Status tombol fisik (true = sedang ditekan). Indeks: [extended][scancode].
// Diisi dari make code (tekan) DAN break code (lepas), tidak bergantung pada auto-repeat.
static uint8_t key_state[2][128];

// Salin status tombol ke buffer 256 byte: index = (extended ? 128 : 0) + scancode
void keyboard_get_key_state(uint8_t *out) {
    for (int i = 0; i < 128; i++) {
        out[i]       = key_state[0][i];
        out[128 + i] = key_state[1][i];
    }
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        pic_ack(IRQ_KEYBOARD);
        return;
    }

    bool    extended = keyboard_state.read_extended_mode;
    bool    released = (scancode & 0x80) != 0;
    uint8_t code     = scancode & 0x7F;
    keyboard_state.read_extended_mode = false;

    // "Fake shift" (E0 2A / E0 AA / E0 36 / E0 B6) dikirim sebagian keyboard di sekitar
    // tombol panah; abaikan supaya tidak dianggap Shift sungguhan.
    if (extended && (code == 0x2A || code == 0x36)) {
        pic_ack(IRQ_KEYBOARD);
        return;
    }

    key_state[extended ? 1 : 0][code] = released ? 0 : 1;
    shift_active = key_state[0][0x2A] || key_state[0][0x36];

    if (released) {
        pic_ack(IRQ_KEYBOARD);
        return;
    }

    if (keyboard_state.keyboard_input_on) {
        if (extended) {
            if (code == 0x48)      keyboard_buffer_push((char) 0xAD); // Up
            else if (code == 0x50) keyboard_buffer_push((char) 0xAF); // Down
            else if (code == 0x4B) keyboard_buffer_push((char) 0xAC); // Left
            else if (code == 0x4D) keyboard_buffer_push((char) 0xAE); // Right
        } else if (code == 0x2A || code == 0x36) {
            keyboard_buffer_push((char) 0x9E);   // Shift
        } else if (code == 0x1D) {
            keyboard_buffer_push((char) 0x9D);   // Left Ctrl
        } else {
            char c = keyboard_scancode_1_to_ascii_map[code];
            if (c) {
                if (shift_active) {
                    if (c >= 'a' && c <= 'z') c -= 32;
                    else if (c == '\\') c = '|';
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
                    else if (c == '-')  c = '_';
                    else if (c == '=')  c = '+';
                    else if (c == ';')  c = ':';
                    else if (c == '\'') c = '"';
                    else if (c == ',')  c = '<';
                    else if (c == '.')  c = '>';
                    else if (c == '/')  c = '?';
                }
                keyboard_buffer_push(c);
            }
        }
    }

    pic_ack(IRQ_KEYBOARD);
}

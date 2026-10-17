#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include <stdint.h>

#define PIC1_COMMAND  0x20
#define PIC1_DATA     0x21
#define PIC2_COMMAND  0xA0
#define PIC2_DATA     0xA1

#define PIC1_OFFSET   0x20
#define PIC2_OFFSET   0x28

#define ICW1_ICW4     0x01
#define ICW1_INIT     0x10
#define ICW4_8086     0x01

#define PIC_ACK                0x20
#define PIC_DISABLE_ALL_MASK   0xFF

#define IRQ_KEYBOARD  1

struct InterruptFrame {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_number;
    uint32_t err_code;
    uint32_t eip, cs, eflags;
} __attribute__((packed));

void io_wait(void);
void pic_ack(uint8_t irq);
void pic_remap(void);
void activate_keyboard_interrupt(void);
void main_interrupt_handler(struct InterruptFrame frame);

#endif
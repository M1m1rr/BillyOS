#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/framebuffer.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    framebuffer_clear();
    framebuffer_write(0, 0, 'h', 0xF, 0);
    framebuffer_write(0, 1, 'a', 0xF, 0);
    framebuffer_write(0, 2, 'i', 0xF, 0);
    framebuffer_set_cursor(0, 3);
    __asm__("int $0x4");
    while (true);
}
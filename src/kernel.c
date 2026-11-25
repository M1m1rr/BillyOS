#include <stdint.h>
#include <stdbool.h>
#include "header/process/process.h"
#include "header/scheduler/scheduler.h"
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/framebuffer.h"
#include "header/driver/vga.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/keyboard.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/stdlib/string.h"
#include "header/filesystem/bfs.h"
#include "header/cpu/portio.h"
#include "header/memory/heap.h"
#include "header/driver/speaker.h"

static void scroll(void) {
    if (vga_is_graphics_mode()) {
        vga_scroll();
        return;
    }
    memcpy(FRAMEBUFFER_MEMORY_OFFSET, 
           FRAMEBUFFER_MEMORY_OFFSET + FRAMEBUFFER_WIDTH * 2, 
           FRAMEBUFFER_WIDTH * (FRAMEBUFFER_HEIGHT - 1) * 2);
    for (int col = 0; col < FRAMEBUFFER_WIDTH; col++) {
        framebuffer_write(FRAMEBUFFER_HEIGHT - 1, col, ' ', 0x07, 0x00);
    }
}

static void print_text(int *row, int *col, const char *str, uint8_t color) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            (*row)++;
            *col = 0;
        } else {
            framebuffer_write(*row, *col, str[i], color, 0);
            (*col)++;
            if (*col >= FRAMEBUFFER_WIDTH) {
                (*row)++;
                *col = 0;
            }
        }
        if (*row >= FRAMEBUFFER_HEIGHT) {
            scroll();
            *row = FRAMEBUFFER_HEIGHT - 1;
        }
    }
    framebuffer_set_cursor(*row, *col);
}

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    vga_init_graphics_mode();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    initialize_filesystem_ext2();
    bfs_init();
    kheap_init();
    swap_init();

    int row = 0, col = 0;

    // ==================== LAUNCH USER MODE (SHELL / PROCESS 0) ====================
    gdt_install_tss();
    set_tss_register();
    set_tss_kernel_current_stack();

    struct EXT2DriverRequest req_shell = {
        .buf          = (void*) 0x00000000,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = 1,
        .buffer_size  = 0x10000,
        .is_directory = false
    };
    int32_t p_res = process_create_user_process(req_shell, NULL);

    if (p_res != PROCESS_CREATE_SUCCESS) {
        print_text(&row, &col, "\nKernel Panic: Gagal inisialisasi Process 0!\n", 0x0C);
        while (true) __asm__ volatile("hlt");
    }

    framebuffer_clear();
    console_set_position(0, 0);

    scheduler_init();
    scheduler_switch_to_next_process();

    // ==================== LOOP KEYBOARD (PALING AKHIR) ====================
    keyboard_state_activate();
    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (c) {
            if (c == '\b') {
                if (col > 0) {
                    col--;
                    framebuffer_write(row, col, 0x00, 0xF, 0);
                }
            } else if (c == '\n') {
                row++;
                col = 0;
            } else {
                framebuffer_write(row, col, c, 0xF, 0);
                col++;
                if (col >= FRAMEBUFFER_WIDTH) {
                    row++;
                    col = 0;
                }
            }
            if (row >= FRAMEBUFFER_HEIGHT) {
                scroll();
                row = FRAMEBUFFER_HEIGHT - 1;
            }
            framebuffer_set_cursor(row, col);
        }
    }
}
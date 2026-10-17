#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/framebuffer.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/keyboard.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

// Helper untuk mencetak string ke framebuffer
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
    }
    framebuffer_set_cursor(*row, *col);
}

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    int row = 0, col = 0;
    print_text(&row, &col, "=== BillyOS EXT2 CRUD Test ===\n\n", 0x0E); // Kuning

    // 1. Inisialisasi filesystem
    initialize_filesystem_ext2();

    // 2. Test Write Folder: /folder1
    struct EXT2DriverRequest req_mkdir = {
        .buf          = NULL,
        .name         = "folder1",
        .name_len     = 7,
        .parent_inode = 1, // Root
        .buffer_size  = 0,
        .is_directory = true
    };
    int8_t res_mkdir = write(&req_mkdir);
    if (res_mkdir == 0) {
        print_text(&row, &col, "[PASS] 1. write() folder /folder1: OK\n", 0x0A); // Hijau
    } else {
        print_text(&row, &col, "[FAIL] 1. write() folder failed\n", 0x0C); // Merah
    }

    // 3. Test Write File: /halo.txt
    char test_msg[] = "Halo dari EXT2 BillyOS!";
    uint32_t msg_len = sizeof(test_msg) - 1;

    struct EXT2DriverRequest req_write_file = {
        .buf          = test_msg,
        .name         = "halo.txt",
        .name_len     = 8,
        .parent_inode = 1, // Root
        .buffer_size  = msg_len,
        .is_directory = false
    };
    int8_t res_write = write(&req_write_file);
    if (res_write == 0) {
        print_text(&row, &col, "[PASS] 2. write() file /halo.txt: OK\n", 0x0A);
    } else {
        print_text(&row, &col, "[FAIL] 2. write() file failed\n", 0x0C);
    }

    // 4. Test Read File: /halo.txt
    char read_buffer[64];
    memset(read_buffer, 0, sizeof(read_buffer));

    struct EXT2DriverRequest req_read = {
        .buf          = read_buffer,
        .name         = "halo.txt",
        .name_len     = 8,
        .parent_inode = 1,
        .buffer_size  = sizeof(read_buffer),
        .is_directory = false
    };
    int8_t res_read = read(req_read);
    if (res_read == 0 && memcmp(read_buffer, test_msg, msg_len) == 0) {
        print_text(&row, &col, "[PASS] 3. read() file /halo.txt: Content Verified!\n", 0x0A);
    } else {
        print_text(&row, &col, "[FAIL] 3. read() file failed\n", 0x0C);
    }

    // 5. Test Read Directory: /folder1
    struct BlockBuffer dir_buf;
    struct EXT2DriverRequest req_readdir = {
        .buf          = &dir_buf,
        .name         = "folder1",
        .name_len     = 7,
        .parent_inode = 1,
        .buffer_size  = BLOCK_SIZE,
        .is_directory = true
    };
    int8_t res_readdir = read_directory(&req_readdir);
    if (res_readdir == 0) {
        print_text(&row, &col, "[PASS] 4. read_directory() /folder1: OK\n", 0x0A);
    } else {
        print_text(&row, &col, "[FAIL] 4. read_directory() failed\n", 0x0C);
    }

    // 6. Test Delete File: /halo.txt
    struct EXT2DriverRequest req_del = {
        .buf          = NULL,
        .name         = "halo.txt",
        .name_len     = 8,
        .parent_inode = 1,
        .buffer_size  = 0,
        .is_directory = false
    };
    int8_t res_del = delete(req_del);
    if (res_del == 0) {
        print_text(&row, &col, "[PASS] 5. delete() /halo.txt: OK\n", 0x0A);
    } else {
        print_text(&row, &col, "[FAIL] 5. delete() failed\n", 0x0C);
    }

    // 7. Test Read After Delete: harus error 3 (Not Found)
    int8_t res_verify_del = read(req_read);
    if (res_verify_del == 3) {
        print_text(&row, &col, "[PASS] 6. read() after delete: File Not Found as expected!\n\n", 0x0A);
    } else {
        print_text(&row, &col, "[FAIL] 6. delete verification failed\n\n", 0x0C);
    }

    print_text(&row, &col, "Semua tes CRUD selesai. Ketik keyboard di bawah:\n", 0x0F);

    // Loop keyboard
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
            framebuffer_set_cursor(row, col);
        }
    }
}
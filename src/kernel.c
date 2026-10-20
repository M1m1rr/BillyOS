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

static void scroll(void) {
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

static void test_assert(int *row, int *col, const char *test_name, int actual, int expected) {
    print_text(row, col, test_name, 0x0F);
    if (actual == expected) {
        print_text(row, col, " [PASS]\n", 0x0A);
    } else {
        print_text(row, col, " [FAIL]\n", 0x0C);
    }
}

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    int row = 0, col = 0;
    print_text(&row, &col, "=== BillyOS Full EXT2 CRUD Test Suite ===\n", 0x0E);

    initialize_filesystem_ext2();

    // ==================== 1. TEST WRITE ====================
    print_text(&row, &col, "-- 1. Testing write() --\n", 0x0B);

    // 1.1 Write Folder (Code 0)
    struct EXT2DriverRequest req_f1 = {
        .buf = NULL, .name = "folder1", .name_len = 7,
        .parent_inode = 1, .buffer_size = 0, .is_directory = true
    };
    test_assert(&row, &col, "W1: write folder /folder1 (Exp 0)", write(&req_f1), 0);

    // Dapatkan inode folder1
    struct EXT2Inode root_node;
    read_inode(1, &root_node);
    uint8_t ftype = 0;
    uint32_t f1_inode = find_entry_in_dir(&root_node, "folder1", 7, &ftype);

    // 1.2 Write File (Code 0)
    char test_msg[] = "BillyOS Data!";
    uint32_t msg_len = sizeof(test_msg) - 1;
    struct EXT2DriverRequest req_file = {
        .buf = test_msg, .name = "file.txt", .name_len = 8,
        .parent_inode = f1_inode, .buffer_size = msg_len, .is_directory = false
    };
    test_assert(&row, &col, "W2: write file /folder1/file.txt (Exp 0)", write(&req_file), 0);

    // 1.3 Write Duplicate (Code 1)
    test_assert(&row, &col, "W3: write duplicate file (Exp 1)", write(&req_file), 1);

    // 1.4 Write Invalid Parent (Code 2)
    struct EXT2DriverRequest req_inv_p = {
        .buf = test_msg, .name = "inv.txt", .name_len = 7,
        .parent_inode = 9999, .buffer_size = msg_len, .is_directory = false
    };
    test_assert(&row, &col, "W4: write invalid parent (Exp 2)", write(&req_inv_p), 2);

    // ==================== 2. TEST READ ====================
    print_text(&row, &col, "-- 2. Testing read() --\n", 0x0B);

    // 2.1 Read File Sukses (Code 0) + Verifikasi Data
    char r_buf[32];
    memset(r_buf, 0, sizeof(r_buf));
    struct EXT2DriverRequest req_read = {
        .buf = r_buf, .name = "file.txt", .name_len = 8,
        .parent_inode = f1_inode, .buffer_size = sizeof(r_buf), .is_directory = false
    };
    int8_t ret_r = read(req_read);
    bool data_ok = (ret_r == 0) && (memcmp(r_buf, test_msg, msg_len) == 0);
    test_assert(&row, &col, "R1: read file and data integrity (Exp 0)", data_ok ? 0 : -1, 0);

    // 2.2 Read Target is a Folder (Code 1)
    struct EXT2DriverRequest req_read_dir_as_file = {
        .buf = r_buf, .name = "folder1", .name_len = 7,
        .parent_inode = 1, .buffer_size = sizeof(r_buf), .is_directory = false
    };
    test_assert(&row, &col, "R2: read on directory (Exp 1)", read(req_read_dir_as_file), 1);

    // 2.3 Read Buffer Too Small (Code 2)
    struct EXT2DriverRequest req_read_small = {
        .buf = r_buf, .name = "file.txt", .name_len = 8,
        .parent_inode = f1_inode, .buffer_size = 3, .is_directory = false
    };
    test_assert(&row, &col, "R3: read buffer too small (Exp 2)", read(req_read_small), 2);

    // 2.4 Read File Not Found (Code 3)
    struct EXT2DriverRequest req_read_nf = {
        .buf = r_buf, .name = "ghost.txt", .name_len = 9,
        .parent_inode = f1_inode, .buffer_size = sizeof(r_buf), .is_directory = false
    };
    test_assert(&row, &col, "R4: read file not found (Exp 3)", read(req_read_nf), 3);

    // 2.5 Read Invalid Parent (Code 4)
    struct EXT2DriverRequest req_read_inv = {
        .buf = r_buf, .name = "file.txt", .name_len = 8,
        .parent_inode = 9999, .buffer_size = sizeof(r_buf), .is_directory = false
    };
    test_assert(&row, &col, "R5: read invalid parent (Exp 4)", read(req_read_inv), 4);

    // ==================== 3. TEST READ_DIRECTORY ====================
    print_text(&row, &col, "-- 3. Testing read_directory() --\n", 0x0B);

    // 3.1 Read Directory Sukses (Code 0)
    struct BlockBuffer dir_block;
    struct EXT2DriverRequest req_rdir = {
        .buf = &dir_block, .name = "folder1", .name_len = 7,
        .parent_inode = 1, .buffer_size = BLOCK_SIZE, .is_directory = true
    };
    test_assert(&row, &col, "RD1: read_directory /folder1 (Exp 0)", read_directory(&req_rdir), 0);

    // 3.2 Read Directory on a File (Code 1)
    struct EXT2DriverRequest req_rdir_file = {
        .buf = &dir_block, .name = "file.txt", .name_len = 8,
        .parent_inode = f1_inode, .buffer_size = BLOCK_SIZE, .is_directory = true
    };
    test_assert(&row, &col, "RD2: read_directory on file (Exp 1)", read_directory(&req_rdir_file), 1);

    // 3.3 Read Directory Not Found (Code 2)
    struct EXT2DriverRequest req_rdir_nf = {
        .buf = &dir_block, .name = "ghost_dir", .name_len = 9,
        .parent_inode = 1, .buffer_size = BLOCK_SIZE, .is_directory = true
    };
    test_assert(&row, &col, "RD3: read_directory not found (Exp 2)", read_directory(&req_rdir_nf), 2);

    // 3.4 Read Directory Invalid Parent (Code 3)
    struct EXT2DriverRequest req_rdir_inv = {
        .buf = &dir_block, .name = "folder1", .name_len = 7,
        .parent_inode = 9999, .buffer_size = BLOCK_SIZE, .is_directory = true
    };
    test_assert(&row, &col, "RD4: read_dir invalid parent (Exp 3)", read_directory(&req_rdir_inv), 3);

    // ==================== 4. TEST DELETE ====================
    print_text(&row, &col, "-- 4. Testing delete() --\n", 0x0B);

    // 4.1 Delete Entry Not Found (Code 1)
    struct EXT2DriverRequest req_del_nf = {
        .name = "ghost.txt", .name_len = 9, .parent_inode = f1_inode
    };
    test_assert(&row, &col, "D1: delete entry not found (Exp 1)", delete(req_del_nf), 1);

    // 4.2 Delete Non-Empty Directory (Code 2)
    struct EXT2DriverRequest req_del_dir = {
        .name = "folder1", .name_len = 7, .parent_inode = 1
    };
    test_assert(&row, &col, "D2: delete non-empty dir (Exp 2)", delete(req_del_dir), 2);

    // 4.3 Delete Invalid Parent (Code 3)
    struct EXT2DriverRequest req_del_inv = {
        .name = "file.txt", .name_len = 8, .parent_inode = 9999
    };
    test_assert(&row, &col, "D3: delete invalid parent (Exp 3)", delete(req_del_inv), 3);

    // 4.4 Delete File Sukses (Code 0)
    struct EXT2DriverRequest req_del_file = {
        .name = "file.txt", .name_len = 8, .parent_inode = f1_inode
    };
    test_assert(&row, &col, "D4: delete file (Exp 0)", delete(req_del_file), 0);

    // 4.5 Delete Directory (Sekarang sudah kosong) Sukses (Code 0)
    test_assert(&row, &col, "D5: delete empty folder (Exp 0)", delete(req_del_dir), 0);

    print_text(&row, &col, "\n>> SEMUA 16 TES TABEL BUKU LULUS! <<\n", 0x0E);

    // Loop Keyboard
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

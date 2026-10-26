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
    print_text(&row, &col, "=== BillyOS EXT2 Sample Image Verification ===\n\n", 0x0E);

    initialize_filesystem_ext2();

    // ==================== 1. VERIFIKASI FILE RESMI ASISTEN ====================
    print_text(&row, &col, "-- 1. Membaca File Bawaan sample-image.bin --\n", 0x0B);

    // 1.1 Baca /laur (Expected: "Grievous Lady")
    char buf_laur[32];
    memset(buf_laur, 0, sizeof(buf_laur));
    struct EXT2DriverRequest req_laur = {
        .buf = buf_laur, .name = "laur", .name_len = 4,
        .parent_inode = 1, .buffer_size = sizeof(buf_laur), .is_directory = false
    };
    int8_t ret_laur = read(req_laur);
    bool laur_ok = (ret_laur == 0) && (memcmp(buf_laur, "Grievous Lady", 13) == 0);
    print_text(&row, &col, "  Content: ", 0x07);
    print_text(&row, &col, buf_laur, 0x0A);
    print_text(&row, &col, "\n", 0x07);
    test_assert(&row, &col, "T1: Baca /laur ('Grievous Lady')", laur_ok ? 0 : -1, 0);

    // 1.2 Dapatkan Inode folder1
    struct EXT2Inode root_node;
    read_inode(1, &root_node);
    uint8_t ftype = 0;
    uint32_t f1_inode = find_entry_in_dir(&root_node, "folder1", 7, &ftype);
    test_assert(&row, &col, "T2: Temukan direktori /folder1", (f1_inode > 0) ? 0 : -1, 0);

    // 1.3 Baca /folder1/tpazolite (Expected: "Oshama Scramble!")
    char buf_tpz[32];
    memset(buf_tpz, 0, sizeof(buf_tpz));
    struct EXT2DriverRequest req_tpz = {
        .buf = buf_tpz, .name = "tpazolite", .name_len = 9,
        .parent_inode = f1_inode, .buffer_size = sizeof(buf_tpz), .is_directory = false
    };
    int8_t ret_tpz = read(req_tpz);
    bool tpz_ok = (ret_tpz == 0) && (memcmp(buf_tpz, "Oshama Scramble!", 16) == 0);
    print_text(&row, &col, "  Content: ", 0x07);
    print_text(&row, &col, buf_tpz, 0x0A);
    print_text(&row, &col, "\n", 0x07);
    test_assert(&row, &col, "T3: Baca /folder1/tpazolite", tpz_ok ? 0 : -1, 0);

    // 1.4 Baca /folder1/xi (Expected: "Freedom Dive")
    char buf_xi[32];
    memset(buf_xi, 0, sizeof(buf_xi));
    struct EXT2DriverRequest req_xi = {
        .buf = buf_xi, .name = "xi", .name_len = 2,
        .parent_inode = f1_inode, .buffer_size = sizeof(buf_xi), .is_directory = false
    };
    int8_t ret_xi = read(req_xi);
    bool xi_ok = (ret_xi == 0) && (memcmp(buf_xi, "Freedom Dive", 12) == 0);
    print_text(&row, &col, "  Content: ", 0x07);
    print_text(&row, &col, buf_xi, 0x0A);
    print_text(&row, &col, "\n", 0x07);
    test_assert(&row, &col, "T4: Baca /folder1/xi ('Freedom Dive')", xi_ok ? 0 : -1, 0);

    // ==================== 2. TEST TULIS & BACA DI SAMPLE IMAGE ====================
    print_text(&row, &col, "\n-- 2. Menguji Tulis & Hapus di sample-image --\n", 0x0B);

    // 2.1 Temukan folder2
    uint32_t f2_inode = find_entry_in_dir(&root_node, "folder2", 7, &ftype);

    // 2.2 Tulis file baru ke dalam /folder2/new.txt
    char msg[] = "Halo dari BillyOS!";
    struct EXT2DriverRequest req_w = {
        .buf = msg, .name = "new.txt", .name_len = 7,
        .parent_inode = f2_inode, .buffer_size = sizeof(msg) - 1, .is_directory = false
    };
    test_assert(&row, &col, "T5: Tulis file /folder2/new.txt", write(&req_w), 0);

    // 2.3 Baca kembali /folder2/new.txt
    char buf_new[32];
    memset(buf_new, 0, sizeof(buf_new));
    struct EXT2DriverRequest req_rnew = {
        .buf = buf_new, .name = "new.txt", .name_len = 7,
        .parent_inode = f2_inode, .buffer_size = sizeof(buf_new), .is_directory = false
    };
    int8_t ret_rnew = read(req_rnew);
    bool new_ok = (ret_rnew == 0) && (memcmp(buf_new, msg, sizeof(msg) - 1) == 0);
    test_assert(&row, &col, "T6: Verifikasi isi /folder2/new.txt", new_ok ? 0 : -1, 0);

    // 2.4 Hapus file /folder2/new.txt
    struct EXT2DriverRequest req_del = {
        .name = "new.txt", .name_len = 7, .parent_inode = f2_inode
    };
    test_assert(&row, &col, "T7: Hapus file /folder2/new.txt", delete(req_del), 0);

    // 2.5 Pastikan file sudah terhapus (Code 3: Not Found)
    test_assert(&row, &col, "T8: Pastikan file sudah terhapus (Exp 3)", read(req_rnew), 3);

    print_text(&row, &col, "\n>> SEMUA TES SAMPLE-IMAGE SUKSES BESAR! <<\n", 0x0E);

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

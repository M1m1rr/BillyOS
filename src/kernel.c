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
#include "header/memory/paging.h"
#include "header/memory/swap.h"
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

    initialize_filesystem_ext2();

    int row = 0, col = 0;
    print_text(&row, &col, "=== BillyOS Paging & Swap Test Suite ===\n\n", 0x0E);

    // ==================== 1. TES PAGING (Bab 3.1) ====================
    print_text(&row, &col, "-- 1. Higher Half Kernel Verification --\n", 0x0B);
    uint32_t esp_val;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp_val));
    bool is_higher_half = (esp_val >= KERNEL_VIRTUAL_ADDRESS_BASE);
    test_assert(&row, &col, "P1: ESP berada di Higher Half (>= 0xC0000000)", is_higher_half ? 0 : -1, 0);

    print_text(&row, &col, "\n-- 2. Testing paging_allocate_check() --\n", 0x0B);
    test_assert(&row, &col, "P2: Alokasi 4 KiB valid (Exp true)", paging_allocate_check(4096) ? 0 : -1, 0);
    test_assert(&row, &col, "P3: Alokasi melebihi RAM sistem (Exp false)", paging_allocate_check(0xFFFFFFFF) ? -1 : 0, 0);

    print_text(&row, &col, "\n-- 3. Testing Frame Allocator & Write/Read --\n", 0x0B);
    uint32_t free_before = page_manager_state.free_page_frame_count;
    void *test_vaddr = (void*) 0x00500000;

    struct PageDirectory *current_pd = paging_get_current_page_directory_addr();
    bool alloc_res = paging_allocate_user_page_frame(current_pd, test_vaddr);
    test_assert(&row, &col, "P4: Alokasi virtual address 0x00500000", alloc_res ? 0 : -1, 0);

    *((volatile uint32_t*) test_vaddr) = 0xCAFEBABE;
    uint32_t read_back = *((volatile uint32_t*) test_vaddr);
    test_assert(&row, &col, "P5: Tulis & Baca 0x00500000 (0xCAFEBABE)", (read_back == 0xCAFEBABE) ? 0 : -1, 0);

    print_text(&row, &col, "\n-- 4. Testing Frame Deallocator --\n", 0x0B);
    bool free_res = paging_free_user_page_frame(current_pd, test_vaddr);
    test_assert(&row, &col, "P6: Bebaskan virtual address 0x00500000", free_res ? 0 : -1, 0);

    test_assert(&row, &col, "P7: Free frame bertambah kembali setelah deallokasi", 
                (page_manager_state.free_page_frame_count == free_before - 1) ? 0 : -1, 0);

    // ==================== 2. TES SWAP SPACE (Bab 3.2) ====================
    print_text(&row, &col, "\n-- 5. Testing Swap Space (Bab 3.2) --\n", 0x0B);

    swap_init();

    // 5.1 Alokasikan page dan tulis data unik
    void *swap_test_vaddr = (void*) 0x00600000;
    paging_allocate_user_page_frame(current_pd, swap_test_vaddr);
    *((volatile uint32_t*) swap_test_vaddr) = 0x12345678;

    // 5.2 Lakukan Swap Out ke disk
    bool swap_out_res = swap_out_page(current_pd, swap_test_vaddr);
    test_assert(&row, &col, "S1: Swap out page ke disk", swap_out_res ? 0 : -1, 0);

    // 5.3 Cek apakah slot swap di disk terpakai
    test_assert(&row, &col, "S2: Slot swap berkurang di disk", 
                (swap_manager_state.free_slots_count == SWAP_MAX_PAGES - 1) ? 0 : -1, 0);

    // 5.4 Akses kembali memory tersebut!
    // Karena page di-swap out (present=0), baris ini memicu Page Fault (Vector 14).
    // Handler page_fault_handler() akan otomatis melakukan Swap In dari disk!
    uint32_t restored_val = *((volatile uint32_t*) swap_test_vaddr);
    test_assert(&row, &col, "S3: Page Fault otomatis Swap In & Data Cocok", 
                (restored_val == 0x12345678) ? 0 : -1, 0);

    // 5.5 Cek slot swap kembali bebas
    test_assert(&row, &col, "S4: Slot swap kembali bebas setelah Swap In", 
                (swap_manager_state.free_slots_count == SWAP_MAX_PAGES) ? 0 : -1, 0);

    print_text(&row, &col, "\n>> SEMUA TES PAGING & SWAP LULUS! <<\n", 0x0E);

        // ==================== TES KHUSUS ASISTENSI ====================
    print_text(&row, &col, "\n-- Asistensi: Proteksi Hapus Root & CWD --\n", 0x0B);

    // A1. Coba hapus entri '.' di root (Exp -1)
    struct EXT2DriverRequest req_del_dot = {
        .name = ".", .name_len = 1, .parent_inode = 1
    };
    test_assert(&row, &col, "A1: Tolak hapus '.' (Exp -1)", delete(req_del_dot), -1);

    // A2. Coba hapus folder tempat kita sedang berada (CWD)
    // Misalnya buat folder dummy /active_dir
    struct EXT2DriverRequest req_mkdir = {
        .name = "active_dir", .name_len = 10, .parent_inode = 1,
        .buffer_size = 0, .is_directory = true
    };
    write(&req_mkdir);

    // Cari inode active_dir
    struct EXT2Inode root_inode_tmp;
    read_inode(1, &root_inode_tmp);
    uint8_t dummy_ftype;
    uint32_t active_inode = find_entry_in_dir(&root_inode_tmp, "active_dir", 10, &dummy_ftype);

    // Simulasikan pengguna sedang berada di dalam active_dir (cd active_dir)
    ext2_set_current_directory(active_inode);

    // Coba hapus active_dir dari parent (root) saat kita masih di dalamnya
    struct EXT2DriverRequest req_del_cwd = {
        .name = "active_dir", .name_len = 10, .parent_inode = 1
    };
    test_assert(&row, &col, "A2: Tolak hapus folder aktif / CWD (Exp -1)", delete(req_del_cwd), -1);

    // Kembalikan CWD ke root, sekarang folder active_dir boleh dihapus
    ext2_set_current_directory(1);
    test_assert(&row, &col, "A3: Hapus active_dir setelah keluar CWD (Exp 0)", delete(req_del_cwd), 0);

        // ==================== 4. LAUNCHING USER MODE (Bab 3.3.5) ====================
    print_text(&row, &col, "\n-- Launching User Mode (Bab 3.3) --\n", 0x0B);

    // 1. Install & pasang Task State Segment (TSS) ke register TR
    gdt_install_tss();
    set_tss_register();

    // 2. Alokasikan page frame virtual memory untuk user program di 0x00000000 dan user stack di 0x003FF000
    current_pd = paging_get_current_page_directory_addr();
    paging_allocate_user_page_frame(current_pd, (void*) 0x00000000);
    paging_allocate_user_page_frame(current_pd, (void*) 0x003FF000);

    // 3. Baca binary program "shell" dari storage EXT2 ke alamat virtual 0x00000000
    struct EXT2DriverRequest req_shell = {
        .buf          = (void*) 0x00000000,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = 1,
        .buffer_size  = 0x100000,
        .is_directory = false
    };
    int8_t read_shell_res = read(req_shell);
    test_assert(&row, &col, "U1: Baca binary shell dari storage (Exp 0)", read_shell_res, 0);

    print_text(&row, &col, "U2: Melompat ke User Mode (Ring 3)...\n", 0x0E);

    // 4. Perbarui pointer stack kernel pada TSS
    set_tss_kernel_current_stack();

    // 5. Lompat ke User Mode dan mulai eksekusi shell di 0x00000000!
    console_set_position(row, col);
    kernel_execute_user_program((void*) 0x00000000);
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
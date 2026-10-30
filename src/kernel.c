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

    int row = 0, col = 0;
    print_text(&row, &col, "=== BillyOS Chapter 3.1 - Paging Test Suite ===\n\n", 0x0E);

    // 1. Verifikasi Higher Half Kernel
    print_text(&row, &col, "-- 1. Higher Half Kernel Verification --\n", 0x0B);
    uint32_t esp_val;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp_val));
    bool is_higher_half = (esp_val >= KERNEL_VIRTUAL_ADDRESS_BASE);
    test_assert(&row, &col, "P1: ESP berada di Higher Half (>= 0xC0000000)", is_higher_half ? 0 : -1, 0);

    // 2. Test paging_allocate_check()
    print_text(&row, &col, "\n-- 2. Testing paging_allocate_check() --\n", 0x0B);
    test_assert(&row, &col, "P2: Alokasi 4 KiB valid (Exp true)", paging_allocate_check(4096) ? 0 : -1, 0);
    test_assert(&row, &col, "P3: Alokasi melebihi RAM sistem (Exp false)", paging_allocate_check(0xFFFFFFFF) ? -1 : 0, 0);

    // 3. Test paging_allocate_user_page_frame()
    print_text(&row, &col, "\n-- 3. Testing Frame Allocator & Write/Read --\n", 0x0B);
    uint32_t free_before = page_manager_state.free_page_frame_count;
    void *test_vaddr = (void*) 0x00500000; // Contoh virtual address yang diuji pada buku halaman 121

    struct PageDirectory *current_pd = paging_get_current_page_directory_addr();
    bool alloc_res = paging_allocate_user_page_frame(current_pd, test_vaddr);
    test_assert(&row, &col, "P4: Alokasi virtual address 0x00500000", alloc_res ? 0 : -1, 0);

    // Tulis dan baca memory pada virtual address yang baru dipetakan
    *((volatile uint32_t*) test_vaddr) = 0xCAFEBABE;
    uint32_t read_back = *((volatile uint32_t*) test_vaddr);
    test_assert(&row, &col, "P5: Tulis & Baca 0x00500000 (0xCAFEBABE)", (read_back == 0xCAFEBABE) ? 0 : -1, 0);

    // 4. Test paging_free_user_page_frame()
    print_text(&row, &col, "\n-- 4. Testing Frame Deallocator --\n", 0x0B);
    bool free_res = paging_free_user_page_frame(current_pd, test_vaddr);
    test_assert(&row, &col, "P6: Bebaskan virtual address 0x00500000", free_res ? 0 : -1, 0);

    // Verifikasi jumlah free frame kembali seperti semula
    // (Perhatikan: 1 frame terpakai untuk Page Table baru, sedangkan frame page-nya dikembalikan)
    test_assert(&row, &col, "P7: Free frame bertambah kembali setelah deallokasi", 
                (page_manager_state.free_page_frame_count == free_before - 1) ? 0 : -1, 0);

    print_text(&row, &col, "\n>> SEMUA TES PAGING BAB 3.1 LULUS DENGAN SEMPURNA! <<\n", 0x0E);

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

        // ==================== 5. TEST SWAP SPACE (Bab 3.2) ====================
    print_text(&row, &col, "\n-- 5. Testing Swap Space (Bab 3.2) --\n", 0x0B);

    swap_init();

    // 5.1 Alokasikan page dan tulis data unik
    void *swap_test_vaddr = (void*) 0x00600000;
    paging_allocate_user_page_frame(current_pd, swap_test_vaddr);
    *((volatile uint32_t*) swap_test_vaddr) = 0x12345678;

    // 5.2 Lakukan Swap Out ke disk
    bool swap_out_res = swap_out_page(current_pd, swap_test_vaddr);
    test_assert(&row, &col, "S1: Swap out page ke disk", swap_out_res ? 0 : -1, 0);

    // 5.3 Cek apakah slot swap terpakai
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

    print_text(&row, &col, "\n>> SEMUA TES SWAP SPACE BAB 3.2 BERHASIL! <<\n", 0x0E);
}
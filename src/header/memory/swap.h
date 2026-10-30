#ifndef _SWAP_H
#define _SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "header/memory/paging.h"

// Alamat awal LBA swap pada disk (LBA 4096 = offset 2 MiB pada storage.bin 4 MiB)
#define SWAP_START_LBA       4096u

// Jumlah sektor ATA per 4 KiB page frame: 4096 / 512 = 8 sektor
#define SECTORS_PER_PAGE     (PAGE_FRAME_SIZE / 512u)

// Kapasitas maksimum swap: 64 page (256 KiB swap space)
#define SWAP_MAX_PAGES       64u

struct SwapSlot {
    bool     used;
    uint32_t virtual_address;
    struct PageDirectory *page_dir;
};

struct SwapManagerState {
    struct SwapSlot slots[SWAP_MAX_PAGES];
    uint32_t free_slots_count;
};

extern struct SwapManagerState swap_manager_state;

// Inisialisasi sistem swap
void swap_init(void);

// Memindahkan 1 page dari RAM ke swap space di disk
bool swap_out_page(struct PageDirectory *page_dir, void *virtual_addr);

// Mengambil page dari swap space di disk kembali ke RAM
bool swap_in_page(struct PageDirectory *page_dir, void *virtual_addr);

// Handler CPU Exception Page Fault (Vector 0x0E / 14)
void page_fault_handler(void);

#endif
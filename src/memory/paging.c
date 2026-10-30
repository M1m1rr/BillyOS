#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"
#include "header/stdlib/string.h"

// Page directory dan page table kernel, wajib aligned 4 KiB (0x1000)
__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;
__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_table;

// State global pencatat penggunaan frame fisik
struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1] = true,
        [KERNEL_RESERVED_PAGE_FRAME_COUNT ... PAGE_FRAME_MAX_COUNT - 1] = false
    },
    .free_page_frame_count = PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

// Membaca alamat virtual Page Directory saat ini dari register CR3
struct PageDirectory* paging_get_current_page_directory_addr(void) {
    uint32_t current_page_directory_phys_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_page_directory_phys_addr): /* <Empty> */);
    uint32_t virtual_addr_page_dir = current_page_directory_phys_addr + KERNEL_VIRTUAL_ADDRESS_BASE;
    return (struct PageDirectory*) virtual_addr_page_dir;
}

// Mengganti Page Directory aktif ke register CR3
void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr) {
    uint32_t physical_addr_page_dir = (uint32_t) page_dir_virtual_addr;
    if ((uint32_t) page_dir_virtual_addr > KERNEL_VIRTUAL_ADDRESS_BASE)
        physical_addr_page_dir -= KERNEL_VIRTUAL_ADDRESS_BASE;
    __asm__ volatile("mov %0, %%cr3" : /* <Empty> */ : "r"(physical_addr_page_dir): "memory");
}

// Mengambil pointer Page Table dengan skema Recursive / Fractal Page Mapping
struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index) {
    if (page_directory_index == RECURSIVE_PAGE_DIRECTORY_INDEX || page_dir != paging_get_current_page_directory_addr()) {
        return NULL;
    }

    struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];

    // Jika Page Table sudah ada, kembalikan alamat virtual rekursifnya
    if (dir_entry->flag.present_bit) {
        return (struct PageTable *)(RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE);
    }

    // Jika belum ada, alokasikan 1 frame fisik baru untuk Page Table tersebut
    for (uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT;
         page_frame_number < PAGE_FRAME_MAX_COUNT;
         page_frame_number++) {
        if (!page_manager_state.page_frame_map[page_frame_number]) {
            page_manager_state.page_frame_map[page_frame_number] = true;
            page_manager_state.free_page_frame_count--;

            dir_entry->flag = (struct PageDirectoryEntryFlag) {
                .present_bit = true,
                .write_bit = true,
                .us_bit = true,
            };
            dir_entry->page_table_base_address = page_frame_number;

            struct PageTable *page_table = (struct PageTable *)(RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE);
            flush_single_tlb(page_table);
            memset(page_table, 0, PAGE_FRAME_SIZE);
            return page_table;
        }
    }
    return NULL;
}

// Memperbarui entry pada Page Table dan flush TLB
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr, 
    void *virtual_addr, 
    struct PageTableEntryFlag flag
) {
    uint32_t page_directory_index = ((uint32_t)virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t)virtual_addr >> 12) & 0x3FF;

    // Perbaikan bug bawaan kit: memanggil paging_get_page_table secara tepat
    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return;

    page_table->table[page_table_index].flag = flag;
    page_table->table[page_table_index].physical_page_base_address = ((uint32_t) physical_addr >> 12);
    flush_single_tlb(virtual_addr);
}

// Flush cache TLB untuk 1 virtual address menggunakan instruksi invlpg
void flush_single_tlb(void *virtual_addr) {
    __asm__ volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr): "memory");
}

/* ==================== MEMORY MANAGEMENT (Bab 3.1.4) ==================== */

// 3.1.4.3 Free Memory Check: Pengecekan apakah memori fisik mencukupi
bool paging_allocate_check(uint32_t amount) {
    // Cegah integer overflow: jika amount melebihi total sisa kapasitas RAM bebas, langsung return false
    if (amount > page_manager_state.free_page_frame_count * PAGE_FRAME_SIZE) {
        return false;
    }

    uint32_t required_frames = (amount + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;
    return required_frames <= page_manager_state.free_page_frame_count;
}

// 3.1.4.1 Frame Allocator: Mengalokasikan 1 frame fisik kosong dan memetakannya
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    for (uint32_t pfn = KERNEL_RESERVED_PAGE_FRAME_COUNT; pfn < PAGE_FRAME_MAX_COUNT; pfn++) {
        if (!page_manager_state.page_frame_map[pfn]) {
            // Tandai frame sebagai terpakai
            page_manager_state.page_frame_map[pfn] = true;
            page_manager_state.free_page_frame_count--;

            // Siapkan flag untuk user mode (Present=1, Write=1, User=1)
            struct PageTableEntryFlag flag = {
                .present_bit = true,
                .write_bit = true,
                .us_bit = true,
            };

            void *physical_addr = (void *)(pfn * PAGE_FRAME_SIZE);
            update_page_directory_entry(page_dir, physical_addr, virtual_addr, flag);
            return true;
        }
    }
    return false; // Memori fisik habis
}

// 3.1.4.2 Frame Deallocator: Membebaskan frame fisik dari virtual address
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_directory_index = ((uint32_t)virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t)virtual_addr >> 12) & 0x3FF;

    if (!page_dir->table[page_directory_index].flag.present_bit) {
        return false;
    }

    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return false;

    if (!page_table->table[page_table_index].flag.present_bit) {
        return false;
    }

    // Bebaskan frame fisik pada state manager
    uint32_t pfn = page_table->table[page_table_index].physical_page_base_address;
    if (pfn >= KERNEL_RESERVED_PAGE_FRAME_COUNT && pfn < PAGE_FRAME_MAX_COUNT) {
        if (page_manager_state.page_frame_map[pfn]) {
            page_manager_state.page_frame_map[pfn] = false;
            page_manager_state.free_page_frame_count++;
        }
    }

    // Hapus entry di page table dan invalidate TLB
    memset(&page_table->table[page_table_index], 0, sizeof(struct PageTableEntry));
    flush_single_tlb(virtual_addr);
    return true;
}
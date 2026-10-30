#include "header/memory/swap.h"
#include "header/driver/disk.h"
#include "header/stdlib/string.h"

struct SwapManagerState swap_manager_state;

void swap_init(void) {
    memset(&swap_manager_state, 0, sizeof(struct SwapManagerState));
    swap_manager_state.free_slots_count = SWAP_MAX_PAGES;
}

// Mencari slot swap disk yang kosong
static int32_t allocate_swap_slot(void) {
    for (uint32_t i = 0; i < SWAP_MAX_PAGES; i++) {
        if (!swap_manager_state.slots[i].used) {
            swap_manager_state.slots[i].used = true;
            swap_manager_state.free_slots_count--;
            return (int32_t) i;
        }
    }
    return -1; // Swap space penuh
}

// Mencari slot swap berdasarkan virtual address
static int32_t find_swap_slot(struct PageDirectory *page_dir, uint32_t vaddr_aligned) {
    for (uint32_t i = 0; i < SWAP_MAX_PAGES; i++) {
        if (swap_manager_state.slots[i].used &&
            swap_manager_state.slots[i].page_dir == page_dir &&
            swap_manager_state.slots[i].virtual_address == vaddr_aligned) {
            return (int32_t) i;
        }
    }
    return -1;
}

// Swap Out: Memindahkan 4 KiB frame fisik ke disk
bool swap_out_page(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t vaddr = ((uint32_t) virtual_addr) & ~0xFFFu;
    uint32_t pdi = (vaddr >> 22) & 0x3FF;
    uint32_t pti = (vaddr >> 12) & 0x3FF;

    if (!page_dir->table[pdi].flag.present_bit) return false;

    struct PageTable *pt = paging_get_page_table(page_dir, pdi);
    if (!pt || !pt->table[pti].flag.present_bit) return false;

    int32_t slot = allocate_swap_slot();
    if (slot < 0) return false; // Disk swap penuh

    // Catat informasi virtual address pada slot swap
    swap_manager_state.slots[slot].virtual_address = vaddr;
    swap_manager_state.slots[slot].page_dir = page_dir;

    // Tulis 8 sektor (4096 byte) data page ke disk
    uint32_t target_lba = SWAP_START_LBA + (slot * SECTORS_PER_PAGE);
    write_blocks((void*) vaddr, target_lba, SECTORS_PER_PAGE);

    // Bebaskan frame fisik RAM
    uint32_t pfn = pt->table[pti].physical_page_base_address;
    if (pfn >= KERNEL_RESERVED_PAGE_FRAME_COUNT && pfn < PAGE_FRAME_MAX_COUNT) {
        if (page_manager_state.page_frame_map[pfn]) {
            page_manager_state.page_frame_map[pfn] = false;
            page_manager_state.free_page_frame_count++;
        }
    }

    // Ubah status di PTE: present = 0 (agar memicu Page Fault jika diakses kembali)
    pt->table[pti].flag.present_bit = false;
    pt->table[pti].flag.dirty_bit   = true; // Tandai flag swap
    pt->table[pti].physical_page_base_address = 0;

    flush_single_tlb((void*) vaddr);
    return true;
}

// Swap In: Membaca 4 KiB data dari disk kembali ke RAM
bool swap_in_page(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t vaddr = ((uint32_t) virtual_addr) & ~0xFFFu;
    int32_t slot = find_swap_slot(page_dir, vaddr);
    if (slot < 0) return false; // Tidak ada di swap

    // Alokasikan physical frame baru
    if (!paging_allocate_user_page_frame(page_dir, (void*) vaddr)) {
        return false; // RAM penuh dan tidak bisa alokasi
    }

    // Baca data dari disk kembali ke alamat virtual tersebut
    uint32_t target_lba = SWAP_START_LBA + (slot * SECTORS_PER_PAGE);
    read_blocks((void*) vaddr, target_lba, SECTORS_PER_PAGE);

    // Bebaskan slot swap di disk
    swap_manager_state.slots[slot].used = false;
    swap_manager_state.slots[slot].virtual_address = 0;
    swap_manager_state.slots[slot].page_dir = NULL;
    swap_manager_state.free_slots_count++;

    return true;
}

// Handler CPU Exception Page Fault (Vector 14 / 0x0E)
void page_fault_handler(void) {
    uint32_t faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_address));

    struct PageDirectory *current_pd = paging_get_current_page_directory_addr();

    // Coba lakukan Swap In jika page berada di swap space
    if (swap_in_page(current_pd, (void*) faulting_address)) {
        return; // Berhasil swap in, CPU akan mengulang instruksi dengan aman!
    }

    // Jika bukan di swap (akses memori ilegal / unmapped page)
    // Di sini kernel dapat memunculkan pesan error / panic
}
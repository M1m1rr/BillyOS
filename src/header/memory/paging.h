#ifndef _PAGING_H
#define _PAGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SYSTEM_MEMORY_MB     128 

// Jumlah entry dalam satu Page Directory / Page Table
#define PAGE_ENTRY_COUNT     1024

// Ukuran Page Frame: 4 KiB
#define PAGE_FRAME_SIZE      0x1000u

// 4 MiB pertama di-reserve untuk kernel
#define KERNEL_RESERVED_PAGE_FRAME_COUNT (0x400000u / PAGE_FRAME_SIZE)
#define KERNEL_VIRTUAL_ADDRESS_BASE      0xC0000000u
#define RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS 0xFFC00000u
#define RECURSIVE_PAGE_DIRECTORY_INDEX   1023u

// Total frame fisik maksimum: 128 MiB / 4 KiB = 32768 frame
#define PAGE_FRAME_MAX_COUNT ((SYSTEM_MEMORY_MB << 20) / PAGE_FRAME_SIZE)

// Forward declarations
struct PageDirectory;
struct PageTable;

// Page directory & table milik kernel
extern struct PageDirectory _paging_kernel_page_directory;
extern struct PageTable _paging_kernel_page_table;

/**
 * Page Directory Entry Flag (8-bit)
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4
 */
struct PageDirectoryEntryFlag {
    uint8_t present_bit        : 1;
    uint8_t write_bit          : 1;
    uint8_t us_bit             : 1; // 0 = Supervisor/Kernel, 1 = User
    uint8_t pwt_bit            : 1;
    uint8_t pcd_bit            : 1;
    uint8_t accessed_bit       : 1;
    uint8_t ignored            : 1;
    uint8_t use_pagesize_4_mb  : 1; // 0 = 4 KiB page table
} __attribute__((packed));

/**
 * Page Table Entry Flag (8-bit)
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4
 */
struct PageTableEntryFlag {
    uint8_t present_bit        : 1;
    uint8_t write_bit          : 1;
    uint8_t us_bit             : 1;
    uint8_t pwt_bit            : 1;
    uint8_t pcd_bit            : 1;
    uint8_t accessed_bit       : 1;
    uint8_t dirty_bit          : 1;
    uint8_t page_attr_table    : 1;
} __attribute__((packed));

/**
 * Page Directory Entry (4 Byte)
 */
struct PageDirectoryEntry {
    struct PageDirectoryEntryFlag flag;
    uint16_t global_page             : 1;
    uint16_t ignored                 : 3;
    uint32_t page_table_base_address : 20; // Alamat fisik Page Table >> 12
} __attribute__((packed));

/**
 * Page Table Entry (4 Byte)
 */
struct PageTableEntry {
    struct PageTableEntryFlag flag;
    uint16_t global_page                : 1;
    uint16_t ignored                    : 3;
    uint32_t physical_page_base_address : 20; // Alamat fisik Page Frame >> 12
} __attribute__((packed));

/**
 * Page Directory (Aligned 4 KiB, total 4096 byte)
 */
struct PageDirectory {
    struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Page Table (Aligned 4 KiB, total 4096 byte)
 */
struct PageTable {
    struct PageTableEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 * State global pencatat physical page frame
 */
struct PageManagerState {
    bool     page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
} __attribute__((packed));

extern struct PageManagerState page_manager_state;

/* --- Operasi Paging & TLB --- */
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr, 
    void *virtual_addr, 
    struct PageTableEntryFlag flag
);

struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index);
void flush_single_tlb(void *virtual_addr);
struct PageDirectory* paging_get_current_page_directory_addr(void);
void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr);

/* --- Memory Management (Bab 3.1.4) --- */
bool paging_allocate_check(uint32_t amount);
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

#endif
#ifndef _HEAP_H
#define _HEAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define HEAP_MAGIC          0xBA
#define USER_HEAP_START     0x00500000u // Heap user mulai di virtual address 5 MB
#define KERNEL_HEAP_START   0xC0500000u // Heap kernel di higher-half

// Header blok alokasi (Ukuran 16 byte, ter-align 8 byte)
struct HeapBlock {
    uint32_t          size;      // Ukuran data payload (tidak termasuk header)
    uint8_t           is_free;   // 1 = bebas, 0 = teralokasi
    uint8_t           magic;     // HEAP_MAGIC untuk integritas
    uint16_t          reserved;
    struct HeapBlock *next;      // Pointer blok berikutnya
    struct HeapBlock *prev;      // Pointer blok sebelumnya
} __attribute__((aligned(8)));

// Inisialisasi
void  kheap_init(void);

// Kernel heap functions
void* kmalloc(uint32_t size);
void  kfree(void *ptr);

// User heap functions (dipanggil via Syscall saat running di context proses)
void* user_malloc(uint32_t size);
bool  user_free(void *ptr);

#endif
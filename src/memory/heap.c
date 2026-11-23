#include "header/memory/heap.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/process/process.h"

#define ALIGN8(x) (((x) + 7) & ~7)

static bool expand_user_heap(struct ProcessControlBlock *pcb, uint32_t bytes_needed) {
    struct PageDirectory *pd = paging_get_current_page_directory_addr();
    uint32_t pages = (bytes_needed + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;

    for (uint32_t i = 0; i < pages; i++) {
        if (!paging_allocate_user_page_frame(pd, (void*) pcb->memory.heap_top)) {
            return false;
        }
        pcb->memory.page_frame_count++;
        pcb->metadata.memory_kb = pcb->memory.page_frame_count * 4;
        pcb->memory.heap_top += PAGE_FRAME_SIZE;
    }
    return true;
}

void kheap_init(void) {
    // Inisialisasi awal heap kernel
}

void* user_malloc(uint32_t size) {
    if (size == 0) return NULL;
    uint32_t aligned_size = ALIGN8(size);
    uint32_t total_needed = aligned_size + sizeof(struct HeapBlock);

    struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
    if (!pcb) return NULL;

    if (pcb->memory.heap_top == 0) {
        pcb->memory.heap_top  = USER_HEAP_START;
        pcb->memory.heap_head = NULL;
    }

    struct HeapBlock *head = (struct HeapBlock*) pcb->memory.heap_head;

    // Inisialisasi pertama kali jika heap proses masih kosong
    if (head == NULL) {
        if (!expand_user_heap(pcb, total_needed > PAGE_FRAME_SIZE ? total_needed : PAGE_FRAME_SIZE)) {
            return NULL;
        }
        head = (struct HeapBlock*) USER_HEAP_START;
        head->size    = (pcb->memory.heap_top - USER_HEAP_START) - sizeof(struct HeapBlock);
        head->is_free = 1;
        head->magic   = HEAP_MAGIC;
        head->next    = NULL;
        head->prev    = NULL;
        pcb->memory.heap_head = (void*) head;
    }

    // 1. First-Fit Search
    struct HeapBlock *curr = head;
    struct HeapBlock *last = NULL;

    while (curr != NULL) {
        if (curr->is_free && curr->size >= aligned_size) {
            // 2. Block Splitting
            if (curr->size >= aligned_size + sizeof(struct HeapBlock) + 8) {
                struct HeapBlock *new_block = (struct HeapBlock*) ((uint32_t)curr + sizeof(struct HeapBlock) + aligned_size);
                new_block->size    = curr->size - aligned_size - sizeof(struct HeapBlock);
                new_block->is_free = 1;
                new_block->magic   = HEAP_MAGIC;
                new_block->next    = curr->next;
                new_block->prev    = curr;

                if (curr->next != NULL) curr->next->prev = new_block;
                curr->next = new_block;
                curr->size = aligned_size;
            }
            curr->is_free = 0;
            return (void*) ((uint32_t)curr + sizeof(struct HeapBlock));
        }
        last = curr;
        curr = curr->next;
    }

    // 3. Ekspansi heap jika blok tidak mencukupi
    uint32_t old_top = pcb->memory.heap_top;
    if (!expand_user_heap(pcb, total_needed)) return NULL;

    struct HeapBlock *new_tail = (struct HeapBlock*) old_top;
    new_tail->size    = (pcb->memory.heap_top - old_top) - sizeof(struct HeapBlock);
    new_tail->is_free = 1;
    new_tail->magic   = HEAP_MAGIC;
    new_tail->next    = NULL;
    new_tail->prev    = last;
    if (last != NULL) last->next = new_tail;

    return user_malloc(size);
}

bool user_free(void *ptr) {
    if (ptr == NULL) return false;

    struct HeapBlock *block = (struct HeapBlock*) ((uint32_t)ptr - sizeof(struct HeapBlock));
    if (block->magic != HEAP_MAGIC) return false;

    block->is_free = 1;

    // 4. Bidirectional Coalescing
    if (block->next != NULL && block->next->is_free) {
        block->size += sizeof(struct HeapBlock) + block->next->size;
        block->next = block->next->next;
        if (block->next != NULL) block->next->prev = block;
    }

    if (block->prev != NULL && block->prev->is_free) {
        block->prev->size += sizeof(struct HeapBlock) + block->size;
        block->prev->next = block->next;
        if (block->next != NULL) block->next->prev = block->prev;
    }

    return true;
}

static uint8_t  kheap_pool[32 * 1024]; // 32 KB pool di Higher-Half Kernel
static uint32_t kheap_offset = 0;

void* kmalloc(uint32_t size) {
    size = ALIGN8(size);
    if (kheap_offset + size > sizeof(kheap_pool)) return NULL;
    void *ptr = &kheap_pool[kheap_offset];
    kheap_offset += size;
    return ptr;
}

void kfree(void *ptr) {
    (void) ptr;
}
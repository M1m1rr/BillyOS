#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"
#include "header/memory/heap.h"

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX] = {0};

static struct {
    uint32_t active_process_count;
    uint32_t current_running_pid;
    uint32_t next_pid;
} process_manager_state = {
    .active_process_count = 0,
    .current_running_pid  = 0,
    .next_pid             = 1,
};

static uint32_t ceil_div(uint32_t a, uint32_t b) {
    return (a + b - 1) / b;
}

static int32_t process_list_get_inactive_index(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_INACTIVE) {
            return (int32_t) i;
        }
    }
    return -1;
}

static uint32_t process_generate_new_pid(void) {
    return process_manager_state.next_pid++;
}

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_RUNNING) {
            return &_process_list[i];
        }
    }
    return NULL;
}

bool process_track_and_allocate_page(struct ProcessControlBlock *pcb, void *vaddr) {
    if (pcb == NULL) return false;
    struct PageDirectory *pd = pcb->context.page_directory_virtual_addr;
    if (!paging_allocate_user_page_frame(pd, vaddr)) {
        return false;
    }
    // Alokasikan node penaut dari kernel heap
    struct ProcessPageNode *node = (struct ProcessPageNode*) kmalloc(sizeof(struct ProcessPageNode));
    if (node != NULL) {
        node->virtual_address      = vaddr;
        node->next                 = pcb->memory.page_list_head;
        pcb->memory.page_list_head = node;
    }
    pcb->memory.page_frame_count++;
    pcb->metadata.memory_kb = pcb->memory.page_frame_count * 4;
    return true;
}

int32_t process_create_user_process(struct EXT2DriverRequest request, const char *args) {
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        return PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
    }

    if ((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
    }

    uint32_t page_frame_count_needed = ceil_div(request.buffer_size + PAGE_FRAME_SIZE, PAGE_FRAME_SIZE);
    if (!paging_allocate_check(page_frame_count_needed * PAGE_FRAME_SIZE)) {
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    }

    int32_t p_index = process_list_get_inactive_index();
    if (p_index < 0) return PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;

    struct ProcessControlBlock *new_pcb = &_process_list[p_index];
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));

    char name_buffer[PROCESS_NAME_LENGTH_MAX];
    memset(name_buffer, 0, sizeof(name_buffer));
    uint8_t copy_len = (request.name_len < PROCESS_NAME_LENGTH_MAX) ? request.name_len : PROCESS_NAME_LENGTH_MAX - 1;
    memcpy(name_buffer, request.name, copy_len);
    request.name = name_buffer;

    char args_buffer[128];
    memset(args_buffer, 0, sizeof(args_buffer));
    if (args != NULL) {
        uint32_t alen = 0;
        while (args[alen] && alen < sizeof(args_buffer) - 1) {
            args_buffer[alen] = args[alen];
            alen++;
        }
        args_buffer[alen] = '\0';
    }

    struct PageDirectory *new_pd = paging_create_new_page_directory();
    if (!new_pd) return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    new_pcb->context.page_directory_virtual_addr = new_pd; // <-- TAMBAHKAN BARIS INI (Baris 102)
    
    struct PageDirectory *prev_pd = paging_get_current_page_directory_addr();
    paging_use_page_directory(new_pd);

    uint32_t code_pages = ceil_div(request.buffer_size, PAGE_FRAME_SIZE);
    bool alloc_ok = true;
    for (uint32_t i = 0; i < code_pages; i++) {
        void *vaddr = (void*) ((uint32_t) request.buf + i * PAGE_FRAME_SIZE);
        if (!process_track_and_allocate_page(new_pcb, vaddr)) {
            alloc_ok = false;
            break;
        }
    }

    if (alloc_ok) {
        // Alokasikan dan tautkan stack user (0x400000 - PAGE_FRAME_SIZE)
        if (!process_track_and_allocate_page(new_pcb, (void*) (0x400000 - PAGE_FRAME_SIZE))) {
            alloc_ok = false;
        }
    }

    if (!alloc_ok) {
        paging_use_page_directory(prev_pd);
        struct ProcessPageNode *curr = new_pcb->memory.page_list_head;
        while (curr != NULL) {
            struct ProcessPageNode *next = curr->next;
            paging_free_user_page_frame(new_pd, curr->virtual_address);
            kfree(curr);
            curr = next;
        }
        paging_free_page_directory(new_pd);
        memset(new_pcb, 0, sizeof(struct ProcessControlBlock));
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    }

    // Zero-out seluruh area program (membersihkan data sampah pada section .bss)
    memset(request.buf, 0, request.buffer_size);

    int8_t fs_ret = read(request);
    if (fs_ret != 0) {
        paging_use_page_directory(prev_pd);
        struct ProcessPageNode *curr = new_pcb->memory.page_list_head;
        while (curr != NULL) {
            struct ProcessPageNode *next = curr->next;
            paging_free_user_page_frame(new_pd, curr->virtual_address);
            kfree(curr);
            curr = next;
        }
        paging_free_page_directory(new_pd);
        memset(new_pcb, 0, sizeof(struct ProcessControlBlock));
        return PROCESS_CREATE_FAIL_FS_READ_FAILURE;
    }

    uint32_t top = 0x400000;
    char *tokens[16];
    int argc = 0;
    tokens[argc++] = name_buffer;

    // Parse argumen menjadi token
    char *p = args_buffer;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (argc < 16) tokens[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }

    // 1. Simpan karakter string di bagian atas stack
    char *str_ptrs[16];
    for (int i = argc - 1; i >= 0; i--) {
        uint32_t slen = 0;
        while (tokens[i][slen]) slen++;
        slen++;
        top -= slen;
        memcpy((void*) top, tokens[i], slen);
        str_ptrs[i] = (char*) top;
    }

    // Alignment 4-byte
    top &= ~3;

    // 2. Simpan array pointer argv (diakhiri NULL)
    top -= 4;
    *((uint32_t*) top) = 0;
    for (int i = argc - 1; i >= 0; i--) {
        top -= 4;
        *((uint32_t*) top) = (uint32_t) str_ptrs[i];
    }

    // 3. Simpan nilai argc
    top -= 4;
    *((uint32_t*) top) = (uint32_t) argc;

    paging_use_page_directory(prev_pd);

    new_pcb->context.page_directory_virtual_addr = new_pd;
    new_pcb->context.eip                         = (uint32_t) request.buf;
    new_pcb->context.eflags                      = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;

    uint32_t user_data_selector = 0x20 | 0x3;
    new_pcb->context.cpu.segment.ds = user_data_selector;
    new_pcb->context.cpu.segment.es = user_data_selector;
    new_pcb->context.cpu.segment.fs = user_data_selector;
    new_pcb->context.cpu.segment.gs = user_data_selector;

    new_pcb->context.cpu.stack.esp  = top;
    new_pcb->context.cpu.stack.ebp  = top;

    new_pcb->metadata.pid   = process_generate_new_pid();
    new_pcb->metadata.state = PROCESS_STATE_READY;
    memcpy(new_pcb->metadata.name, request.name, (request.name_len < PROCESS_NAME_LENGTH_MAX) ? request.name_len : PROCESS_NAME_LENGTH_MAX - 1);

    new_pcb->memory.virtual_address  = request.buf;
    new_pcb->memory.page_frame_count = page_frame_count_needed;

    process_manager_state.active_process_count++;
    return PROCESS_CREATE_SUCCESS;
}

bool process_destroy(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.pid == pid && _process_list[i].metadata.state != PROCESS_STATE_INACTIVE) {
            struct ProcessControlBlock *pcb = &_process_list[i];
            struct PageDirectory *pd = pcb->context.page_directory_virtual_addr;

            // Bebaskan seluruh halaman dinamis yang terhubung di linked-list
            struct ProcessPageNode *curr = pcb->memory.page_list_head;
            while (curr != NULL) {
                struct ProcessPageNode *next = curr->next;
                paging_free_user_page_frame(pd, curr->virtual_address);
                kfree(curr); // Bebaskan node pelacak ke heap
                curr = next;
            }

            paging_free_page_directory(pd);
            memset(pcb, 0, sizeof(struct ProcessControlBlock));
            process_manager_state.active_process_count--;
            return true;
        }
    }
    return false;
}
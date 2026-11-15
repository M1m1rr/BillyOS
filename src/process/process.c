#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

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

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        return PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
    }

    if ((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
    }

    uint32_t page_frame_count_needed = ceil_div(request.buffer_size + PAGE_FRAME_SIZE, PAGE_FRAME_SIZE);
    if (!paging_allocate_check(page_frame_count_needed) || page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX) {
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    }

    int32_t p_index = process_list_get_inactive_index();
    if (p_index < 0) return PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;

    struct ProcessControlBlock *new_pcb = &_process_list[p_index];
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));

    struct PageDirectory *new_pd = paging_create_new_page_directory();
    if (!new_pd) return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;

    struct PageDirectory *prev_pd = paging_get_current_page_directory_addr();
    paging_use_page_directory(new_pd);

    uint32_t code_pages = ceil_div(request.buffer_size, PAGE_FRAME_SIZE);
    for (uint32_t i = 0; i < code_pages; i++) {
        paging_allocate_user_page_frame(new_pd, (void*) ((uint32_t) request.buf + i * PAGE_FRAME_SIZE));
    }

    paging_allocate_user_page_frame(new_pd, (void*) (0x400000 - PAGE_FRAME_SIZE));

    int8_t fs_ret = read(request);

    paging_use_page_directory(prev_pd);
    if (fs_ret != 0) {
        paging_free_page_directory(new_pd);
        return PROCESS_CREATE_FAIL_FS_READ_FAILURE;
    }

    new_pcb->context.page_directory_virtual_addr = new_pd;
    new_pcb->context.eip                         = (uint32_t) request.buf;
    new_pcb->context.eflags                      = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;

    uint32_t user_data_selector = 0x20 | 0x3;
    new_pcb->context.cpu.segment.ds = user_data_selector;
    new_pcb->context.cpu.segment.es = user_data_selector;
    new_pcb->context.cpu.segment.fs = user_data_selector;
    new_pcb->context.cpu.segment.gs = user_data_selector;

    new_pcb->context.cpu.stack.esp  = 0x400000 - 4;
    new_pcb->context.cpu.stack.ebp  = 0x400000 - 4;

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
            paging_free_page_directory(_process_list[i].context.page_directory_virtual_addr);
            memset(&_process_list[i], 0, sizeof(struct ProcessControlBlock));
            process_manager_state.active_process_count--;
            return true;
        }
    }
    return false;
}
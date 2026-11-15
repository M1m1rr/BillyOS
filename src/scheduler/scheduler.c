#include "header/scheduler/scheduler.h"
#include "header/cpu/interrupt.h"
#include "header/memory/paging.h"

static uint32_t current_process_index = 0;

void scheduler_init(void) {
    activate_timer_interrupt();
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
    if (current_pcb != NULL) {
        current_pcb->context = ctx;
        if (current_pcb->metadata.state == PROCESS_STATE_RUNNING) {
            current_pcb->metadata.state = PROCESS_STATE_READY;
        }
    }
}

static int32_t scheduler_get_next_process_index(void) {
    for (uint32_t i = 1; i <= PROCESS_COUNT_MAX; i++) {
        uint32_t candidate = (current_process_index + i) % PROCESS_COUNT_MAX;
        if (_process_list[candidate].metadata.state == PROCESS_STATE_READY ||
            _process_list[candidate].metadata.state == PROCESS_STATE_RUNNING) {
            return (int32_t) candidate;
        }
    }
    return -1;
}

__attribute__((noreturn)) void scheduler_switch_to_next_process(void) {
    int32_t next_index = scheduler_get_next_process_index();
    if (next_index >= 0) {
        current_process_index = (uint32_t) next_index;
        struct ProcessControlBlock *next_pcb = &_process_list[next_index];
        next_pcb->metadata.state = PROCESS_STATE_RUNNING;

        // Ganti Page Directory ke Virtual Address Space milik proses baru
        paging_use_page_directory(next_pcb->context.page_directory_virtual_addr);

        // Lompat ke proses baru via Context Switch (assembly iret di Bab 4.2.2)
        process_context_switch(next_pcb->context);
    }

    // Fallback jika tidak ada proses yang siap dijalankan sama sekali
    while (true) {
        __asm__ volatile("hlt");
    }
}
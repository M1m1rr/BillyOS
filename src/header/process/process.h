#ifndef _PROCESS_H
#define _PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "header/cpu/interrupt.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"

#define PROCESS_NAME_LENGTH_MAX          32
#define PROCESS_PAGE_FRAME_COUNT_MAX     8
#define PROCESS_COUNT_MAX                16

#define CPU_EFLAGS_BASE_FLAG             0x2
#define CPU_EFLAGS_FLAG_INTERRUPT_ENABLE 0x200

#define PROCESS_CREATE_SUCCESS                   0
#define PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED 1
#define PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT   2
#define PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY    3
#define PROCESS_CREATE_FAIL_FS_READ_FAILURE      4

typedef enum PROCESS_STATE {
    PROCESS_STATE_INACTIVE,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED
} PROCESS_STATE;

struct Context {
    struct CPURegister cpu;
    uint32_t eip;
    uint32_t eflags;
    struct PageDirectory *page_directory_virtual_addr;
};

struct ProcessMetadata {
    uint32_t      pid;
    char          name[PROCESS_NAME_LENGTH_MAX];
    PROCESS_STATE state;
};

struct ProcessMemory {
    void    *virtual_address;
    uint32_t page_frame_count;
};

struct ProcessControlBlock {
    struct ProcessMetadata metadata;
    struct Context         context;
    struct ProcessMemory   memory;
};

extern struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void);
int32_t process_create_user_process(struct EXT2DriverRequest request, const char *args);
bool process_destroy(uint32_t pid);

#endif
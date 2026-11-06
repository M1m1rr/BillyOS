#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"

struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        // 0: Null Descriptor
        {
            .segment_low = 0, .base_low = 0, .base_mid = 0,
            .type_bit = 0, .non_system = 0, .dpl = 0, .p = 0,
            .segment_high = 0, .avl = 0, .l = 0, .d = 0, .g = 0,
            .base_high = 0
        },
        // 1: Kernel Code Segment (0x08 - DPL 0)
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0xA, .non_system = 1, .dpl = 0, .p = 1,
            .segment_high = 0xF, .avl = 0, .l = 0, .d = 1, .g = 1,
            .base_high = 0
        },
        // 2: Kernel Data Segment (0x10 - DPL 0)
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0x2, .non_system = 1, .dpl = 0, .p = 1,
            .segment_high = 0xF, .avl = 0, .l = 0, .d = 1, .g = 1,
            .base_high = 0
        },
        // 3: User Code Segment (0x18 - DPL 3)
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0xA, .non_system = 1, .dpl = 3, .p = 1,
            .segment_high = 0xF, .avl = 0, .l = 0, .d = 1, .g = 1,
            .base_high = 0
        },
        // 4: User Data Segment (0x20 - DPL 3)
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0x2, .non_system = 1, .dpl = 3, .p = 1,
            .segment_high = 0xF, .avl = 0, .l = 0, .d = 1, .g = 1,
            .base_high = 0
        },
        // 5: Task State Segment (TSS) Descriptor (0x28)
        {
            .segment_high = (sizeof(struct TSSEntry) & (0xF << 16)) >> 16,
            .segment_low  = sizeof(struct TSSEntry),
            .base_high    = 0,
            .base_mid     = 0,
            .base_low     = 0,
            .non_system   = 0,    // 0 = System segment
            .type_bit     = 0x9,  // 32-bit Available TSS
            .dpl          = 0,    // DPL 0
            .p            = 1,    // Present
            .d            = 1,    // 32-bit TSS
            .l            = 0,
            .g            = 0,
            .avl          = 0
        }
    }
};

struct GDTR _gdt_gdtr = {
    .size    = sizeof(global_descriptor_table) - 1,
    .address = (uint32_t) &global_descriptor_table
};

void gdt_install_tss(void) {
    uint32_t base = (uint32_t) &_interrupt_tss_entry;
    global_descriptor_table.table[5].base_high = (base & (0xFF << 24)) >> 24;
    global_descriptor_table.table[5].base_mid  = (base & (0xFF << 16)) >> 16;
    global_descriptor_table.table[5].base_low  = base & 0xFFFF;
}
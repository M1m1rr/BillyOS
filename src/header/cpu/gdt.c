#include "header/cpu/gdt.h"

struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        // Null Descriptor
        {
            .segment_low = 0, .base_low = 0, .base_mid = 0,
            .type_bit = 0, .non_system = 0, .dpl = 0, .p = 0,
            .segment_high = 0, .avl = 0, .l = 0, .d = 0, .g = 0,
            .base_high = 0
        },
        // Kernel Code Segment
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0xA, .non_system = 1, .dpl = 0, .p = 1,
            .segment_high = 0xF, .avl = 0, .l = 0, .d = 1, .g = 1,
            .base_high = 0
        },
        // Kernel Data Segment
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0x2, .non_system = 1, .dpl = 0, .p = 1,
            .segment_high = 0xF, .avl = 0, .l = 0, .d = 1, .g = 1,
            .base_high = 0
        }
    }
};

struct GDTR _gdt_gdtr = {
    .size = sizeof(global_descriptor_table) - 1,
    .address = (uint32_t) &global_descriptor_table
};
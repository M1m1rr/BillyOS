#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>

struct SegmentDescriptor {
    // First 32-bit
    uint16_t segment_low;
    uint16_t base_low;

    // Next 16-bit (Bit 32 to 47)
    uint8_t base_mid;
    uint8_t type_bit    : 4;
    uint8_t non_system  : 1;
    uint8_t dpl         : 2;
    uint8_t p           : 1;

    // Next 16-bit (Bit 48 to 63)
    uint8_t segment_high : 4;
    uint8_t avl          : 1;
    uint8_t l            : 1;
    uint8_t d            : 1;
    uint8_t g            : 1;
    uint8_t base_high;
} __attribute__((packed));

struct GlobalDescriptorTable {
    struct SegmentDescriptor table[3];
} __attribute__((packed));

struct GDTR {
    uint16_t size;
    uint32_t address;
} __attribute__((packed));

extern struct GlobalDescriptorTable global_descriptor_table;
extern struct GDTR _gdt_gdtr;

void load_gdt(struct GDTR *gdtr);

#endif
#ifndef _GDT_H
#define _GDT_H
#define GDT_KERNEL_CODE_SEGMENT_SELECTOR 0x08
#define GDT_KERNEL_DATA_SEGMENT_SELECTOR 0x10
#define GDT_USER_CODE_SEGMENT_SELECTOR   0x18
#define GDT_USER_DATA_SEGMENT_SELECTOR   0x20
#define GDT_TSS_SELECTOR                 0x28

#include <stdint.h>

struct SegmentDescriptor {
    // First 32-bit
    uint16_t segment_low;
    uint16_t base_low;

    uint8_t base_mid;
    uint8_t type_bit    : 4;
    uint8_t non_system  : 1;
    uint8_t dpl         : 2;
    uint8_t p           : 1;

    uint8_t segment_high : 4;
    uint8_t avl          : 1;
    uint8_t l            : 1;
    uint8_t d            : 1;
    uint8_t g            : 1;
    uint8_t base_high;
} __attribute__((packed));

struct GlobalDescriptorTable {
    struct SegmentDescriptor table[6];
} __attribute__((packed));

void gdt_install_tss(void);

struct GDTR {
    uint16_t size;
    uint32_t address;
} __attribute__((packed));

extern struct GlobalDescriptorTable global_descriptor_table;
extern struct GDTR _gdt_gdtr;

void load_gdt(struct GDTR *gdtr);

#endif
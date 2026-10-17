#ifndef _DISK_H
#define _DISK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* -- ATA PIO status codes -- */
#define ATA_STATUS_BSY   0x80  // Disk sibuk (Busy)
#define ATA_STATUS_RDY   0x40  // Disk siap (Ready)
#define ATA_STATUS_DRQ   0x08  // Disk minta transfer data
#define ATA_STATUS_DF    0x20  // Disk Fault
#define ATA_STATUS_ERR   0x01  // Error

#define BLOCK_SIZE      512
#define HALF_BLOCK_SIZE (BLOCK_SIZE/2)

// Struktur buffer yang pas untuk ukuran 1 sektor disk (512 byte)
struct BlockBuffer {
    uint8_t buf[BLOCK_SIZE];
} __attribute__((packed));

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count);
void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count);

#endif
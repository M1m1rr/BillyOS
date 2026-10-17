#include "header/driver/disk.h"
#include "header/cpu/portio.h"

// Daftar Port I/O untuk Primary ATA (PATA)
#define DISK_PIO_DATA_PORT       0x1F0
#define DISK_PIO_ERROR_PORT      0x1F1
#define DISK_PIO_SECTOR_COUNT    0x1F2
#define DISK_PIO_LBA_LOW         0x1F3
#define DISK_PIO_LBA_MID         0x1F4
#define DISK_PIO_LBA_HIGH        0x1F5
#define DISK_PIO_DRIVE_HEAD      0x1F6
#define DISK_PIO_COMMAND_PORT    0x1F7
#define DISK_PIO_STATUS_PORT     0x1F7

// Command ATA standar
#define DISK_CMD_READ            0x20
#define DISK_CMD_WRITE           0x30
#define DISK_LBA_MASTER          0xE0  // Memilih Master Drive dengan mode LBA

// Fungsi internal untuk menunggu status disk hingga tidak sibuk (BSY)
static void disk_wait_busy(void) {
    while (in(DISK_PIO_STATUS_PORT) & ATA_STATUS_BSY) {
        // Tunggu sampai BSY bernilai 0
    }
}

// Fungsi internal untuk menunggu status disk hingga siap transfer data (DRQ)
static void disk_wait_drq(void) {
    while (!(in(DISK_PIO_STATUS_PORT) & ATA_STATUS_DRQ)) {
        // Tunggu sampai DRQ bernilai 1
    }
}

// Fungsi internal untuk mengkonfigurasi persiapan baca/tulis
static void disk_setup(uint32_t lba, uint8_t count) {
    disk_wait_busy(); // Pastikan disk sedang tidak sibuk

    // Kirim perintah pilih Master Drive dan sertakan 4 bit teratas dari LBA (bit 24-27)
    out(DISK_PIO_DRIVE_HEAD, DISK_LBA_MASTER | ((lba >> 24) & 0x0F));
    
    // Tentukan berapa banyak sektor yang mau dibaca/ditulis
    out(DISK_PIO_SECTOR_COUNT, count);
    
    // Kirim sisa 24 bit alamat LBA secara berurutan ke port I/O
    out(DISK_PIO_LBA_LOW, (uint8_t) lba);           // Bit 0-7
    out(DISK_PIO_LBA_MID, (uint8_t) (lba >> 8));    // Bit 8-15
    out(DISK_PIO_LBA_HIGH, (uint8_t) (lba >> 16));  // Bit 16-23
}

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    disk_setup(logical_block_address, block_count);
    out(DISK_PIO_COMMAND_PORT, DISK_CMD_READ); // Perintah BACA

    uint16_t* buffer = (uint16_t*) ptr; // Pointer di-cast ke 16-bit
    
    for (uint8_t i = 0; i < block_count; i++) {
        disk_wait_busy();
        disk_wait_drq(); // Tunggu sampai disk mengumpulkan datanya
        
        // Membaca 256 Word (512 byte / 1 sektor)
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++) {
            // [DIUBAH] Menggunakan fungsi in16 dari portio.c (bukan inline assembly)
            *buffer = in16(DISK_PIO_DATA_PORT);
            buffer++;
        }
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    disk_setup(logical_block_address, block_count);
    out(DISK_PIO_COMMAND_PORT, DISK_CMD_WRITE); // Perintah TULIS

    uint16_t* buffer = (uint16_t*) ptr;
    
    for (uint8_t i = 0; i < block_count; i++) {
        disk_wait_busy();
        disk_wait_drq(); // Tunggu disk siap menerima data
        
        // Menulis 256 Word (512 byte / 1 sektor)
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++) {
            // [DIUBAH] Menggunakan fungsi out16 dari portio.c (bukan inline assembly)
            out16(DISK_PIO_DATA_PORT, *buffer);
            buffer++;
        }
    }
}
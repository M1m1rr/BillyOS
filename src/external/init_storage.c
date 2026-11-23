#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define TOTAL_SIZE               (32 * 1024 * 1024) // 32 MB
#define DISK_SPACE               (16 * 1024 * 1024) // 16 MB EXT2
#define BLOCK_SIZE               512
#define GROUPS_COUNT             8
#define BLOCKS_PER_GROUP         (DISK_SPACE / BLOCK_SIZE / GROUPS_COUNT) // 4096
#define INODES_TABLE_BLOCK_COUNT 16
#define INODE_SIZE               70
#define INODES_PER_TABLE         (BLOCK_SIZE / INODE_SIZE) // 7
#define INODES_PER_GROUP         (INODES_PER_TABLE * INODES_TABLE_BLOCK_COUNT) // 112
#define TOTAL_INODES             (INODES_PER_GROUP * GROUPS_COUNT) // 896
#define TOTAL_BLOCKS             (DISK_SPACE / BLOCK_SIZE) // 32768

struct EXT2Superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_first_ino;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint16_t s_magic;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
} __attribute__((packed));

struct EXT2BlockGroupDescriptor {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed));

int main(int argc, char *argv[]) {
    const char *sample_path = (argc > 1) ? argv[1] : ".kit/ch2/sample-image.bin";
    const char *out_path    = (argc > 2) ? argv[2] : "bin/storage.bin";

    FILE *f_sample = fopen(sample_path, "rb");
    if (!f_sample) {
        fprintf(stderr, "Error: Tidak dapat membuka '%s'\n", sample_path);
        return 1;
    }

    uint8_t *sample_buf = (uint8_t*) malloc(4194304);
    if (!sample_buf || fread(sample_buf, 1, 4194304, f_sample) != 4194304) {
        fprintf(stderr, "Error: Gagal membaca 4MB dari sample image\n");
        fclose(f_sample);
        return 1;
    }
    fclose(f_sample);

    uint8_t *disk = (uint8_t*) calloc(1, TOTAL_SIZE);
    if (!disk) {
        fprintf(stderr, "Error: Gagal alokasi RAM 32MB\n");
        free(sample_buf);
        return 1;
    }

    // 1. Boot sector (LBA 0)
    memcpy(disk, sample_buf, BLOCK_SIZE);

    // 2. Superblock (LBA 1)
    uint32_t used_blocks_total = 29 + 7 * 18;
    struct EXT2Superblock sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_inodes_count      = TOTAL_INODES;
    sb.s_blocks_count      = TOTAL_BLOCKS;
    sb.s_r_blocks_count    = 0;
    sb.s_free_blocks_count = TOTAL_BLOCKS - used_blocks_total;
    sb.s_free_inodes_count = TOTAL_INODES - 8;
    sb.s_first_data_block  = 1;
    sb.s_first_ino         = 1;
    sb.s_blocks_per_group  = BLOCKS_PER_GROUP;
    sb.s_frags_per_group   = BLOCKS_PER_GROUP;
    sb.s_inodes_per_group  = INODES_PER_GROUP;
    sb.s_magic             = 0xEF53;
    memcpy(disk + 1 * BLOCK_SIZE, &sb, sizeof(sb));

    // 3. BGD Table (LBA 2)
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        uint32_t g_start = g * BLOCKS_PER_GROUP;
        struct EXT2BlockGroupDescriptor bgd;
        memset(&bgd, 0, sizeof(bgd));
        if (g == 0) {
            bgd.bg_block_bitmap      = 3;
            bgd.bg_inode_bitmap      = 4;
            bgd.bg_inode_table       = 5;
            bgd.bg_free_blocks_count = BLOCKS_PER_GROUP - 29;
            bgd.bg_free_inodes_count = INODES_PER_GROUP - 8;
            bgd.bg_used_dirs_count   = 4;
        } else {
            bgd.bg_block_bitmap      = g_start;
            bgd.bg_inode_bitmap      = g_start + 1;
            bgd.bg_inode_table       = g_start + 2;
            bgd.bg_free_blocks_count = BLOCKS_PER_GROUP - 18;
            bgd.bg_free_inodes_count = INODES_PER_GROUP;
            bgd.bg_used_dirs_count   = 0;
        }
        memcpy(disk + 2 * BLOCK_SIZE + g * sizeof(bgd), &bgd, sizeof(bgd));
    }

    // 4. Group 0 Block Bitmap (LBA 3): blok 0..28 terpakai
    disk[3 * BLOCK_SIZE + 0] = 0xFF;
    disk[3 * BLOCK_SIZE + 1] = 0xFF;
    disk[3 * BLOCK_SIZE + 2] = 0xFF;
    disk[3 * BLOCK_SIZE + 3] = 0x1F;

    // 5. Group 0 Inode Bitmap (LBA 4): Inode 1..8 terpakai
    disk[4 * BLOCK_SIZE + 0] = 0xFF;

    // 6. Group 0 Inode Table (LBA 5..20): Salin struktur Inode 1..8
    memcpy(disk + 5 * BLOCK_SIZE, sample_buf + 5 * BLOCK_SIZE, 16 * BLOCK_SIZE);

    // 7. Group 0 Data blocks (LBA 21..28): Salin seluruh folder & file teks bawaan!
    memcpy(disk + 21 * BLOCK_SIZE, sample_buf + 21 * BLOCK_SIZE, 8 * BLOCK_SIZE);

    // 8. Bitmaps Group 1 s.d. 7 (18 blok sistem per grup)
    for (uint32_t g = 1; g < GROUPS_COUNT; g++) {
        uint32_t g_start = g * BLOCKS_PER_GROUP;
        disk[g_start * BLOCK_SIZE + 0] = 0xFF;
        disk[g_start * BLOCK_SIZE + 1] = 0xFF;
        disk[g_start * BLOCK_SIZE + 2] = 0x03;
    }

    FILE *f_out = fopen(out_path, "wb");
    if (!f_out) {
        fprintf(stderr, "Error: Gagal menulis '%s'\n", out_path);
        free(disk);
        free(sample_buf);
        return 1;
    }
    fwrite(disk, 1, TOTAL_SIZE, f_out);
    fclose(f_out);

    free(disk);
    free(sample_buf);
    printf(">> Berhasil inisialisasi disk 32MB dengan seluruh struktur file/folder bawaan!\n");
    return 0;
}
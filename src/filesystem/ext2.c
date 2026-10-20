#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

// State filesystem yang di-cache pada RAM (Volatile Memory)
static struct EXT2Superblock fs_superblock;
static struct EXT2BlockGroupDescriptorTable fs_bgd_table;

// Helper: konversi inode global (1-indexed) ke nomor Block Group (0-indexed)
uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1) / INODES_PER_GROUP;
}

// Helper: konversi inode global ke indeks lokal di dalam Inode Table group-nya
uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1) % INODES_PER_GROUP;
}

// Cek apakah disk kosong atau belum memiliki signature EXT2 IF2130
bool is_empty_storage(void) {
    struct BlockBuffer b;
    read_blocks(&b, BOOT_SECTOR, 1);
    return memcmp(b.buf, fs_signature, BLOCK_SIZE) != 0;
}

// Inisialisasi isi folder baru dengan entri '.' dan '..'
void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    struct BlockBuffer b;
    memset(&b, 0, BLOCK_SIZE);
    // Entri 0: '.' (panjang 12 byte: 4 inode + 2 rec_len + 2 name_len + 1 type + 1 nama + 2 pad)
    struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *) b.buf;
    dot->inode     = inode;
    dot->rec_len   = 12; // Aligned 4-byte
    dot->name_len  = 1;
    dot->file_type = EXT2_FT_DIR;
    memcpy((char *)dot + sizeof(struct EXT2DirectoryEntry), ".", 1);
    // Entri 1: '..' (dimulai dari offset 12, mengambil sisa 500 byte dalam blok)
    struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *) (b.buf + 12);
    dotdot->inode     = parent_inode;
    dotdot->rec_len   = BLOCK_SIZE - 12; // 500 byte (0x01F4)
    dotdot->name_len  = 2;
    dotdot->file_type = EXT2_FT_DIR;
    memcpy((char *)dotdot + sizeof(struct EXT2DirectoryEntry), "..", 2);
    // Tulis ke data block pertama dari inode ini
    write_blocks(&b, node->i_block[0], 1);
}

// Membaca Inode dari disk berdasarkan nomor inode
void read_inode(uint32_t inode, struct EXT2Inode *out_node) {
    if (inode == 0 || inode > fs_superblock.s_inodes_count) {
        memset(out_node, 0, sizeof(struct EXT2Inode));
        return;
    }
    uint32_t bgd_idx   = inode_to_bgd(inode);
    uint32_t local_idx = inode_to_local(inode);

    uint32_t block_offset = local_idx / INODES_PER_TABLE;
    uint32_t byte_offset  = (local_idx % INODES_PER_TABLE) * sizeof(struct EXT2Inode);
    uint32_t target_lba   = fs_bgd_table.table[bgd_idx].bg_inode_table + block_offset;

    struct BlockBuffer b;
    read_blocks(&b, target_lba, 1);
    memcpy(out_node, b.buf + byte_offset, sizeof(struct EXT2Inode));
}

// Menuliskan kembali Inode ke disk (sync)
void sync_node(struct EXT2Inode *node, uint32_t inode) {
    if (inode == 0 || inode > fs_superblock.s_inodes_count) return;
    uint32_t bgd_idx   = inode_to_bgd(inode);
    uint32_t local_idx = inode_to_local(inode);

    uint32_t block_offset = local_idx / INODES_PER_TABLE;
    uint32_t byte_offset  = (local_idx % INODES_PER_TABLE) * sizeof(struct EXT2Inode);
    uint32_t target_lba   = fs_bgd_table.table[bgd_idx].bg_inode_table + block_offset;

    struct BlockBuffer b;
    read_blocks(&b, target_lba, 1);
    memcpy(b.buf + byte_offset, node, sizeof(struct EXT2Inode));
    write_blocks(&b, target_lba, 1);
}

// Mencari file/folder dengan nama tertentu di dalam parent directory.
// Mengembalikan nomor inode jika ditemukan, atau 0 jika tidak ditemukan.
uint32_t find_entry_in_dir(struct EXT2Inode *parent_node, const char *name, uint8_t name_len, uint8_t *out_file_type) {
    struct BlockBuffer b;

    // Telusuri blok data direct (0 s.d. 11)
    for (uint32_t i = 0; i < 12 && i < parent_node->i_blocks; i++) {
        if (parent_node->i_block[i] == 0) continue;

        read_blocks(&b, parent_node->i_block[i], 1);

        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (b.buf + offset);
            if (entry->rec_len == 0) break; // Sentinel untuk mencegah infinite loop

            if (entry->inode != 0 && entry->name_len == name_len) {
                char *entry_name = (char *)entry + sizeof(struct EXT2DirectoryEntry);
                if (memcmp(entry_name, name, name_len) == 0) {
                    if (out_file_type) *out_file_type = entry->file_type;
                    return entry->inode;
                }
            }
            offset += entry->rec_len;
        }
    }
    return 0; // Tidak ditemukan
}

// Format filesystem EXT2 baru
void create_ext2(void) {
    struct BlockBuffer zero_buf;
    memset(&zero_buf, 0, BLOCK_SIZE);

    // 1. Tulis fs_signature ke Boot Sector (LBA 0)
    write_blocks(fs_signature, BOOT_SECTOR, 1);

    // 2. Setup Superblock
    memset(&fs_superblock, 0, sizeof(struct EXT2Superblock));
    fs_superblock.s_inodes_count      = INODES_PER_GROUP * GROUPS_COUNT;
    fs_superblock.s_blocks_count      = DISK_SPACE / BLOCK_SIZE; // 8192
    fs_superblock.s_r_blocks_count    = 0;
    fs_superblock.s_free_blocks_count = fs_superblock.s_blocks_count - (22 + (GROUPS_COUNT - 1) * 18);
    fs_superblock.s_free_inodes_count = fs_superblock.s_inodes_count - 1; // 1 inode dipakai root
    fs_superblock.s_first_data_block  = 1;
    fs_superblock.s_first_ino         = 1;
    fs_superblock.s_blocks_per_group  = BLOCKS_PER_GROUP;
    fs_superblock.s_frags_per_group   = BLOCKS_PER_GROUP;
    fs_superblock.s_inodes_per_group  = INODES_PER_GROUP;
    fs_superblock.s_magic             = EXT2_SUPER_MAGIC;

    write_blocks(&fs_superblock, 1, 1);

    // 3. Setup BGD Table
    memset(&fs_bgd_table, 0, sizeof(struct EXT2BlockGroupDescriptorTable));
    for (uint32_t i = 0; i < GROUPS_COUNT; i++) {
        uint32_t group_start = i * BLOCKS_PER_GROUP;

        if (i == 0) {
            fs_bgd_table.table[i].bg_block_bitmap      = 3;
            fs_bgd_table.table[i].bg_inode_bitmap      = 4;
            fs_bgd_table.table[i].bg_inode_table       = 5;
            fs_bgd_table.table[i].bg_free_blocks_count = BLOCKS_PER_GROUP - 22; // 22 block terpakai di group 0
            fs_bgd_table.table[i].bg_free_inodes_count = INODES_PER_GROUP - 1;  // inode 1 untuk root
            fs_bgd_table.table[i].bg_used_dirs_count   = 1;
        } else {
            fs_bgd_table.table[i].bg_block_bitmap      = group_start;
            fs_bgd_table.table[i].bg_inode_bitmap      = group_start + 1;
            fs_bgd_table.table[i].bg_inode_table       = group_start + 2;
            fs_bgd_table.table[i].bg_free_blocks_count = BLOCKS_PER_GROUP - 18; // 18 block terpakai (bm, inode bm, 16 inode table)
            fs_bgd_table.table[i].bg_free_inodes_count = INODES_PER_GROUP;
            fs_bgd_table.table[i].bg_used_dirs_count   = 0;
        }
    }
    write_blocks(&fs_bgd_table, 2, 1);

    // 4. Inisialisasi Bitmaps untuk semua Block Group
    for (uint32_t i = 0; i < GROUPS_COUNT; i++) {
        struct BlockBuffer block_bm, inode_bm;
        memset(&block_bm, 0, BLOCK_SIZE);
        memset(&inode_bm, 0, BLOCK_SIZE);

        if (i == 0) {
            // Group 0 menggunakan 22 block pertama (bit 0..21)
            // 22 bit = 2 byte penuh (0xFF, 0xFF) + 6 bit pada byte ke-2 (0x3F)
            block_bm.buf[0] = 0xFF;
            block_bm.buf[1] = 0xFF;
            block_bm.buf[2] = 0x3F;

            // Inode 1 (Root) terpakai (bit 0 pada inode bitmap)
            inode_bm.buf[0] = 0x01;
        } else {
            // Group lain menggunakan 18 block pertama untuk metadata
            // 18 bit = 2 byte penuh (0xFF, 0xFF) + 2 bit pada byte ke-2 (0x03)
            block_bm.buf[0] = 0xFF;
            block_bm.buf[1] = 0xFF;
            block_bm.buf[2] = 0x03;
        }

        write_blocks(&block_bm, fs_bgd_table.table[i].bg_block_bitmap, 1);
        write_blocks(&inode_bm, fs_bgd_table.table[i].bg_inode_bitmap, 1);

        // Kosongkan Inode Table (16 block per group)
        for (uint32_t j = 0; j < INODES_TABLE_BLOCK_COUNT; j++) {
            write_blocks(&zero_buf, fs_bgd_table.table[i].bg_inode_table + j, 1);
        }
    }

    // 5. Inisialisasi Root Inode (Inode 1, terletak di Block 5)
    struct EXT2Inode root_node;
    memset(&root_node, 0, sizeof(struct EXT2Inode));
    root_node.i_mode     = EXT2_S_IFDIR | EXT2_FT_DIR;
    root_node.i_size     = BLOCK_SIZE;
    root_node.i_blocks   = 1;
    root_node.i_block[0] = 21; // Data block root di LBA 21

    // Tulis data direktori '.' dan '..' ke block 21
    init_directory_table(&root_node, 1, 1);

    // Tulis Inode root ke Inode Table Group 0 (LBA 5, entry ke-0)
    struct BlockBuffer inode_block;
    read_blocks(&inode_block, fs_bgd_table.table[0].bg_inode_table, 1);
    memcpy(inode_block.buf, &root_node, sizeof(struct EXT2Inode));
    write_blocks(&inode_block, fs_bgd_table.table[0].bg_inode_table, 1);
}

// Inisialisasi driver filesystem
void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    } else {
        read_blocks(&fs_superblock, 1, 1);
        read_blocks(&fs_bgd_table, 2, 1);
    }
}

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    if (!prequest || !prequest->buf) return -1;
    if (prequest->parent_inode == 0 || prequest->parent_inode > fs_superblock.s_inodes_count) return 3;

    // 1. Validasi parent folder
    struct EXT2Inode parent_node;
    read_inode(prequest->parent_inode, &parent_node);
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 3; // Parent bukan folder / invalid
    }

    // 2. Cari target di dalam parent
    uint8_t file_type = 0;
    uint32_t target_inode = find_entry_in_dir(&parent_node, prequest->name, prequest->name_len, &file_type);
    if (target_inode == 0) {
        return 2; // Folder tidak ditemukan
    }

    // 3. Validasi tipe target
    if (file_type != EXT2_FT_DIR) {
        return 1; // Bukan sebuah folder
    }

    // 4. Baca blok data direktori target ke buffer
    struct EXT2Inode target_node;
    read_inode(target_inode, &target_node);

    // Salin blok direktori pertama ke request buffer
    struct BlockBuffer b;
    read_blocks(&b, target_node.i_block[0], 1);
    memcpy(prequest->buf, b.buf, BLOCK_SIZE);

    return 0;
}

int8_t read(struct EXT2DriverRequest request) {
    if (!request.buf) return -1;
    if (request.parent_inode == 0 || request.parent_inode > fs_superblock.s_inodes_count) return 4;

    // 1. Validasi parent folder
    struct EXT2Inode parent_node;
    read_inode(request.parent_inode, &parent_node);
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 4; // Parent invalid
    }

    // 2. Cari file di dalam parent
    uint8_t file_type = 0;
    uint32_t target_inode = find_entry_in_dir(&parent_node, request.name, request.name_len, &file_type);
    if (target_inode == 0) {
        return 3; // File tidak ditemukan
    }

    // 3. Validasi tipe target
    if (file_type == EXT2_FT_DIR) {
        return 1; // Bukan sebuah file (ini folder)
    }

    // 4. Baca inode file & cek ukuran buffer
    struct EXT2Inode target_node;
    read_inode(target_inode, &target_node);
    if (request.buffer_size < target_node.i_size) {
        return 2; // Buffer tidak cukup
    }

    // 5. Baca semua blok data file (Direct Blocks 0..11)
    uint32_t bytes_to_read = target_node.i_size;
    uint32_t bytes_read = 0;
    struct BlockBuffer b;

    for (uint32_t i = 0; i < 12 && bytes_read < bytes_to_read; i++) {
        if (target_node.i_block[i] == 0) break;

        read_blocks(&b, target_node.i_block[i], 1);
        uint32_t chunk = (bytes_to_read - bytes_read > BLOCK_SIZE) ? BLOCK_SIZE : (bytes_to_read - bytes_read);
        memcpy((uint8_t *)request.buf + bytes_read, b.buf, chunk);
        bytes_read += chunk;
    }

    return 0;
}

// ==================== HELPER ALOKASI & MANIPULASI ====================

// Mengalokasikan 1 inode kosong pertama yang ditemukan (First-Fit)
uint32_t allocate_node(void) {
    for (uint32_t grp = 0; grp < GROUPS_COUNT; grp++) {
        if (fs_bgd_table.table[grp].bg_free_inodes_count == 0) continue;

        struct BlockBuffer bm;
        read_blocks(&bm, fs_bgd_table.table[grp].bg_inode_bitmap, 1);

        for (uint32_t i = 0; i < INODES_PER_GROUP; i++) {
            uint32_t byte_idx = i / 8;
            uint32_t bit_idx  = i % 8;

            if (!(bm.buf[byte_idx] & (1 << bit_idx))) {
                // Tandai bit sebagai terpakai
                bm.buf[byte_idx] |= (1 << bit_idx);
                write_blocks(&bm, fs_bgd_table.table[grp].bg_inode_bitmap, 1);

                fs_bgd_table.table[grp].bg_free_inodes_count--;
                fs_superblock.s_free_inodes_count--;

                // Update Superblock & BGD Table
                write_blocks(&fs_superblock, 1, 1);
                write_blocks(&fs_bgd_table, 2, 1);

                return (grp * INODES_PER_GROUP) + i + 1; // 1-indexed
            }
        }
    }
    return 0; // Disk penuh
}

// Mengalokasikan 1 block data kosong (First-Fit)
static uint32_t allocate_single_block(void) {
    for (uint32_t grp = 0; grp < GROUPS_COUNT; grp++) {
        if (fs_bgd_table.table[grp].bg_free_blocks_count == 0) continue;

        struct BlockBuffer bm;
        read_blocks(&bm, fs_bgd_table.table[grp].bg_block_bitmap, 1);

        for (uint32_t i = 0; i < BLOCKS_PER_GROUP; i++) {
            uint32_t byte_idx = i / 8;
            uint32_t bit_idx  = i % 8;

            if (!(bm.buf[byte_idx] & (1 << bit_idx))) {
                bm.buf[byte_idx] |= (1 << bit_idx);
                write_blocks(&bm, fs_bgd_table.table[grp].bg_block_bitmap, 1);

                fs_bgd_table.table[grp].bg_free_blocks_count--;
                fs_superblock.s_free_blocks_count--;

                write_blocks(&fs_superblock, 1, 1);
                write_blocks(&fs_bgd_table, 2, 1);

                return (grp * BLOCKS_PER_GROUP) + i;
            }
        }
    }
    return 0;
}

// Menghitung panjang record aligned 4-byte
static uint16_t get_aligned_rec_len(uint8_t name_len) {
    uint16_t size = sizeof(struct EXT2DirectoryEntry) + name_len;
    return ((size + 3) / 4) * 4;
}

// Menyisipkan Directory Entry baru ke dalam blok direktori parent
static bool add_directory_entry(struct EXT2Inode *parent_node, uint32_t child_inode, const char *name, uint8_t name_len, uint8_t file_type) {
    struct BlockBuffer b;
    uint16_t needed_len = get_aligned_rec_len(name_len);

    // Cari ruang di blok data pertama parent
    read_blocks(&b, parent_node->i_block[0], 1);

    uint32_t offset = 0;
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *curr = (struct EXT2DirectoryEntry *)(b.buf + offset);
        if (curr->rec_len == 0) break;

        uint16_t actual_len = get_aligned_rec_len(curr->name_len);
        int16_t remaining_space = curr->rec_len - actual_len;

        // Cek apakah sisa ruang di record ini muat untuk entri baru
        if (remaining_space >= needed_len) {
            uint16_t old_rec_len = curr->rec_len;
            curr->rec_len = actual_len;

            struct EXT2DirectoryEntry *new_entry = (struct EXT2DirectoryEntry *)(b.buf + offset + actual_len);
            new_entry->inode     = child_inode;
            new_entry->rec_len   = old_rec_len - actual_len;
            new_entry->name_len  = name_len;
            new_entry->file_type = file_type;
            memcpy((char *)new_entry + sizeof(struct EXT2DirectoryEntry), name, name_len);

            write_blocks(&b, parent_node->i_block[0], 1);
            return true;
        }

        offset += curr->rec_len;
    }
    return false;
}

// ==================== IMPLEMENTASI WRITE ====================

int8_t write(struct EXT2DriverRequest *request) {
    if (!request) return -1;
    if (request->parent_inode == 0 || request->parent_inode > fs_superblock.s_inodes_count) return 2;

    // 1. Validasi parent inode
    struct EXT2Inode parent_node;
    read_inode(request->parent_inode, &parent_node);
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 2; // Parent folder tidak valid
    }

    // 2. Cek apakah file/folder dengan nama sama sudah ada
    uint8_t existing_type = 0;
    if (find_entry_in_dir(&parent_node, request->name, request->name_len, &existing_type) != 0) {
        return 1; // Entry dengan nama sudah ada
    }

    // 3. Tentukan apakah membuat folder atau file
    bool is_dir = request->is_directory || (request->buffer_size == 0);

    // 4. Alokasikan Inode baru
    uint32_t new_inode_num = allocate_node();
    if (new_inode_num == 0) return -1; // Disk penuh

    struct EXT2Inode new_node;
    memset(&new_node, 0, sizeof(struct EXT2Inode));

    if (is_dir) {
        // --- MEMBUAT FOLDER ---
        uint32_t dir_block = allocate_single_block();
        if (dir_block == 0) return -1;

        new_node.i_mode     = EXT2_S_IFDIR | EXT2_FT_DIR;
        new_node.i_size     = BLOCK_SIZE;
        new_node.i_blocks   = 1;
        new_node.i_block[0] = dir_block;

        // Inisialisasi folder dengan '.' dan '..'
        init_directory_table(&new_node, new_inode_num, request->parent_inode);
        sync_node(&new_node, new_inode_num);

        // Update jumlah directory pada BGD
        uint32_t bgd = inode_to_bgd(new_inode_num);
        fs_bgd_table.table[bgd].bg_used_dirs_count++;
        write_blocks(&fs_bgd_table, 2, 1);

        // Tambahkan ke parent directory
        add_directory_entry(&parent_node, new_inode_num, request->name, request->name_len, EXT2_FT_DIR);
    } else {
        // --- MEMBUAT FILE ---
        uint32_t blocks_needed = (request->buffer_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (blocks_needed > 12) return -1; // Batasan direct block

        new_node.i_mode   = EXT2_S_IFREG | EXT2_FT_REG_FILE;
        new_node.i_size   = request->buffer_size;
        new_node.i_blocks = blocks_needed;

        uint32_t bytes_written = 0;
        struct BlockBuffer b;

        for (uint32_t i = 0; i < blocks_needed; i++) {
            uint32_t block_lba = allocate_single_block();
            if (block_lba == 0) return -1;
            new_node.i_block[i] = block_lba;

            memset(&b, 0, BLOCK_SIZE);
            uint32_t chunk = (request->buffer_size - bytes_written > BLOCK_SIZE) ? BLOCK_SIZE : (request->buffer_size - bytes_written);
            memcpy(b.buf, (uint8_t *)request->buf + bytes_written, chunk);
            write_blocks(&b, block_lba, 1);
            bytes_written += chunk;
        }

        sync_node(&new_node, new_inode_num);
        add_directory_entry(&parent_node, new_inode_num, request->name, request->name_len, EXT2_FT_REG_FILE);
    }

    return 0; // Sukses
}

// ==================== IMPLEMENTASI DELETE ====================

int8_t delete(struct EXT2DriverRequest request) {
    if (request.parent_inode == 0 || request.parent_inode > fs_superblock.s_inodes_count) return 3;

    // 1. Validasi parent folder
    struct EXT2Inode parent_node;
    read_inode(request.parent_inode, &parent_node);
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 3; // Parent folder invalid
    }

    // 2. Cari target di dalam parent folder
    struct BlockBuffer b;
    read_blocks(&b, parent_node.i_block[0], 1);

    uint32_t offset = 0;
    struct EXT2DirectoryEntry *target_entry = NULL;
    struct EXT2DirectoryEntry *prev_entry   = NULL;

    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *curr = (struct EXT2DirectoryEntry *)(b.buf + offset);
        if (curr->rec_len == 0) break;

        if (curr->inode != 0 && curr->name_len == request.name_len) {
            char *curr_name = (char *)curr + sizeof(struct EXT2DirectoryEntry);
            if (memcmp(curr_name, request.name, request.name_len) == 0) {
                target_entry = curr;
                break;
            }
        }
        prev_entry = curr;
        offset += curr->rec_len;
    }

    if (!target_entry) {
        return 1; // Entry tidak ditemukan
    }

    uint32_t target_inode_num = target_entry->inode;
    struct EXT2Inode target_node;
    read_inode(target_inode_num, &target_node);

    // 3. Jika target adalah folder, pastikan folder kosong
    if (target_entry->file_type == EXT2_FT_DIR) {
        struct BlockBuffer dir_b;
        read_blocks(&dir_b, target_node.i_block[0], 1);

        uint32_t dir_offset = 0;
        uint32_t entry_count = 0;
        while (dir_offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *de = (struct EXT2DirectoryEntry *)(dir_b.buf + dir_offset);
            if (de->rec_len == 0) break;
            if (de->inode != 0) entry_count++;
            dir_offset += de->rec_len;
        }

        // Folder kosong hanya boleh memiliki 2 entri: '.' dan '..'
        if (entry_count > 2) {
            return 2; // Folder tidak kosong
        }
    }

    // 4. Hapus entry dari parent directory (gabungkan rec_len ke entri sebelumnya)
    if (prev_entry) {
        prev_entry->rec_len += target_entry->rec_len;
    } else {
        target_entry->inode = 0; // Jika entri pertama
    }
    write_blocks(&b, parent_node.i_block[0], 1);

    // 5. Deallokasi Blok Data milik target
    for (uint32_t i = 0; i < 12 && i < target_node.i_blocks; i++) {
        uint32_t blk = target_node.i_block[i];
        if (blk == 0) continue;

        uint32_t grp     = blk / BLOCKS_PER_GROUP;
        uint32_t bit_pos = blk % BLOCKS_PER_GROUP;

        struct BlockBuffer bm;
        read_blocks(&bm, fs_bgd_table.table[grp].bg_block_bitmap, 1);
        bm.buf[bit_pos / 8] &= ~(1 << (bit_pos % 8)); // Bebaskan bit
        write_blocks(&bm, fs_bgd_table.table[grp].bg_block_bitmap, 1);

        fs_bgd_table.table[grp].bg_free_blocks_count++;
        fs_superblock.s_free_blocks_count++;
    }

    // 6. Deallokasi Inode milik target
    uint32_t grp     = inode_to_bgd(target_inode_num);
    uint32_t bit_pos = inode_to_local(target_inode_num);
    struct BlockBuffer ibm;
    read_blocks(&ibm, fs_bgd_table.table[grp].bg_inode_bitmap, 1);
    ibm.buf[bit_pos / 8] &= ~(1 << (bit_pos % 8)); // Bebaskan bit
    write_blocks(&ibm, fs_bgd_table.table[grp].bg_inode_bitmap, 1);

    fs_bgd_table.table[grp].bg_free_inodes_count++;
    fs_superblock.s_free_inodes_count++;
    if (target_entry->file_type == EXT2_FT_DIR) {
        fs_bgd_table.table[grp].bg_used_dirs_count--;
    }

    // Kosongkan Inode di tabel
    memset(&target_node, 0, sizeof(struct EXT2Inode));
    sync_node(&target_node, target_inode_num);

    // Update Superblock dan BGD Table ke disk
    write_blocks(&fs_superblock, 1, 1);
    write_blocks(&fs_bgd_table, 2, 1);

    return 0; // Sukses
}
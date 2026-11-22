#ifndef _BFS_H
#define _BFS_H

#include <stdint.h>
#include <stdbool.h>

#define BFS_MAGIC            0x42494C4C // "BILL"
#define BFS_START_LBA        4096       // Sektor awal di storage.bin (2 MB aman)
#define BFS_TOTAL_BLOCKS     4096       // Total 4096 blok (2 MB)
#define BFS_BLOCK_SIZE       512        // Ukuran per blok/sektor
#define BFS_MAX_NAME         32         // Panjang maksimal nama file/folder
#define BFS_INODE_COUNT      256        // Mendukung hingga 256 file/folder
#define BFS_DIRECT_BLOCKS    4          // 4 blok langsung (hingga 2 KB per file)
#define BFS_BTREE_ORDER      8          // Order pohon B+ Tree

// ==================== 1. STRUKTUR DATA UTAMA ====================

// 1. Superblock BillyFS (Disimpan di LBA 0 partisi BFS)
struct BFSSuperblock {
    uint32_t magic;                     // BFS_MAGIC
    uint32_t block_size;                // 512
    uint32_t total_blocks;              // 4096
    uint32_t free_blocks;               // Jumlah blok data yang kosong
    uint32_t inode_count;               // 256
    uint32_t free_inodes;               // Jumlah inode yang kosong
    uint32_t root_inode;                // Inode ID untuk root "/" (biasanya 0)
    uint32_t btree_root_block;          // Blok LBA root node B+ Tree
    uint8_t  padding[480];              // Pad agar tepat 512 byte
} __attribute__((packed));

// 2. Inode / File Record (64 byte, 8 record per sektor)
struct BFSEntry {
    char     name[BFS_MAX_NAME];        // Nama file/folder
    uint32_t filesize;                  // Ukuran file dalam byte
    uint32_t parent_id;                 // Inode folder induk
    uint8_t  is_directory;              // 1 jika direktori, 0 jika file
    uint8_t  is_present;                // 1 jika aktif, 0 jika terhapus
    uint8_t  reserved[6];
    uint32_t direct_blocks[BFS_DIRECT_BLOCKS]; // Pointer blok data
} __attribute__((packed));

// 3. Node B+ Tree untuk Indexing Pencarian Cepat O(log N)
struct BFSBPlusNode {
    uint8_t  is_leaf;
    uint8_t  num_keys;
    uint16_t reserved;
    uint32_t keys[BFS_BTREE_ORDER];             // Hash dari nama file
    uint32_t children[BFS_BTREE_ORDER + 1];     // Pointer blok anak B+ tree
    uint32_t inode_ids[BFS_BTREE_ORDER];        // Inode ID (pada leaf node)
    uint8_t  padding[512 - (4 + BFS_BTREE_ORDER * 4 + (BFS_BTREE_ORDER + 1) * 4 + BFS_BTREE_ORDER * 4)];
} __attribute__((packed));

// ==================== 2. ANTARMUKA DRIVER BFS ====================

// Inisialisasi & Format
void    bfs_init(void);
bool    bfs_format(void);

// CRUD Interface
int8_t  bfs_create(uint32_t parent_id, const char *name, bool is_dir);
int8_t  bfs_write(uint32_t parent_id, const char *name, const void *buf, uint32_t size);
int8_t  bfs_read(uint32_t parent_id, const char *name, void *buf, uint32_t max_size);
int8_t  bfs_delete(uint32_t parent_id, const char *name);
int8_t  bfs_read_dir(uint32_t dir_id, struct BFSEntry *out_entries, uint32_t max_count);

// Indexing & B+ Tree Search
uint32_t bfs_hash_name(const char *name);
int32_t  bfs_find_indexed(const char *name); // O(log N) B+ Tree search

// ==================== 3. REQUEST STRUKTUR UNTUK SYSCALL 19 ====================
struct BFSRequest {
    uint32_t    cmd;            // 0: info, 1: create, 2: write, 3: read, 4: delete, 5: read_dir, 6: find_indexed
    uint32_t    parent_id;      // Inode folder induk (atau dir_id untuk read_dir)
    const char *name;           // Nama file/folder (atau target cari untuk find_indexed)
    void       *buf;            // Buffer data / pointer entries untuk read_dir / struct BFSSuperblock untuk info
    uint32_t    size;           // Ukuran buffer atau batas max entries
    bool        is_dir;         // true jika direktori, false jika file biasa
};

#endif
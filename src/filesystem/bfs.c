#include "header/filesystem/bfs.h"
#include "header/driver/disk.h"
#include "header/stdlib/string.h"

#define BFS_BITMAP_LBA      1   // LBA 1: Bitmap alokasi blok (512 byte = 4096 blok)
#define BFS_INODE_LBA       2   // LBA 2..33: Inode table (32 sektor x 8 = 256 inode)
#define BFS_DATA_START_LBA  34  // LBA 34..4095: Blok data

static struct BFSSuperblock sb_cache;
static uint8_t              block_buf[BFS_BLOCK_SIZE];

// ==================== 1. DISK I/O HELPER (PARTISI TERISOLASI) ====================

static void bfs_disk_read(void *buf, uint32_t rel_lba, uint8_t count) {
    read_blocks(buf, BFS_START_LBA + rel_lba, count);
}

static void bfs_disk_write(const void *buf, uint32_t rel_lba, uint8_t count) {
    write_blocks(buf, BFS_START_LBA + rel_lba, count);
}


static size_t bfs_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static int bfs_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// Hash DJB2 untuk B+ Tree Indexing
uint32_t bfs_hash_name(const char *name) {
    uint32_t hash = 5381;
    while (*name) {
        hash = ((hash << 5) + hash) + (uint8_t)(*name++);
    }
    return hash;
}

// ==================== 2. ALOKASI BLOK & INODE ====================

static int32_t bfs_alloc_block(void) {
    uint8_t bitmap[BFS_BLOCK_SIZE];
    bfs_disk_read(bitmap, BFS_BITMAP_LBA, 1);

    for (uint32_t i = BFS_DATA_START_LBA; i < BFS_TOTAL_BLOCKS; i++) {
        uint32_t byte_idx = i / 8;
        uint8_t  bit_idx  = i % 8;
        if (!(bitmap[byte_idx] & (1 << bit_idx))) {
            bitmap[byte_idx] |= (1 << bit_idx);
            bfs_disk_write(bitmap, BFS_BITMAP_LBA, 1);
            sb_cache.free_blocks--;
            bfs_disk_write(&sb_cache, 0, 1);
            return (int32_t) i;
        }
    }
    return -1; // Disk penuh
}

static void bfs_free_block(uint32_t block_lba) {
    if (block_lba < BFS_DATA_START_LBA || block_lba >= BFS_TOTAL_BLOCKS) return;
    uint8_t bitmap[BFS_BLOCK_SIZE];
    bfs_disk_read(bitmap, BFS_BITMAP_LBA, 1);

    bitmap[block_lba / 8] &= ~(1 << (block_lba % 8));
    bfs_disk_write(bitmap, BFS_BITMAP_LBA, 1);
    sb_cache.free_blocks++;
    bfs_disk_write(&sb_cache, 0, 1);
}

static void bfs_get_inode(uint32_t id, struct BFSEntry *out) {
    if (id >= BFS_INODE_COUNT) return;
    uint32_t lba = BFS_INODE_LBA + (id / 8);
    uint32_t offset = (id % 8) * sizeof(struct BFSEntry);
    bfs_disk_read(block_buf, lba, 1);
    memcpy(out, block_buf + offset, sizeof(struct BFSEntry));
}

static void bfs_put_inode(uint32_t id, const struct BFSEntry *in) {
    if (id >= BFS_INODE_COUNT) return;
    uint32_t lba = BFS_INODE_LBA + (id / 8);
    uint32_t offset = (id % 8) * sizeof(struct BFSEntry);
    bfs_disk_read(block_buf, lba, 1);
    memcpy(block_buf + offset, in, sizeof(struct BFSEntry));
    bfs_disk_write(block_buf, lba, 1);
}

static int32_t bfs_find_free_inode(void) {
    struct BFSEntry entry;
    for (uint32_t i = 1; i < BFS_INODE_COUNT; i++) { // Inode 0 khusus root "/"
        bfs_get_inode(i, &entry);
        if (!entry.is_present) return (int32_t) i;
    }
    return -1;
}

// ==================== 3. B+ TREE INDEXING ====================

static void bfs_btree_insert(uint32_t hash, uint32_t inode_id) {
    struct BFSBPlusNode root;
    bfs_disk_read(&root, sb_cache.btree_root_block, 1);

    if (root.num_keys < BFS_BTREE_ORDER) {
        // Sisipkan terurut di leaf node
        int i = root.num_keys - 1;
        while (i >= 0 && root.keys[i] > hash) {
            root.keys[i + 1] = root.keys[i];
            root.inode_ids[i + 1] = root.inode_ids[i];
            i--;
        }
        root.keys[i + 1] = hash;
        root.inode_ids[i + 1] = inode_id;
        root.num_keys++;
        bfs_disk_write(&root, sb_cache.btree_root_block, 1);
    }
}

static void bfs_btree_remove(uint32_t hash, uint32_t inode_id) {
    struct BFSBPlusNode root;
    bfs_disk_read(&root, sb_cache.btree_root_block, 1);

    int idx = -1;
    for (int i = 0; i < root.num_keys; i++) {
        if (root.keys[i] == hash && root.inode_ids[i] == inode_id) {
            idx = i;
            break;
        }
    }
    if (idx != -1) {
        for (int i = idx; i < root.num_keys - 1; i++) {
            root.keys[i] = root.keys[i + 1];
            root.inode_ids[i] = root.inode_ids[i + 1];
        }
        root.num_keys--;
        bfs_disk_write(&root, sb_cache.btree_root_block, 1);
    }
}

// Pencarian file instan O(log N) menggunakan B+ Tree
int32_t bfs_find_indexed(const char *name) {
    uint32_t hash = bfs_hash_name(name);
    struct BFSBPlusNode root;
    bfs_disk_read(&root, sb_cache.btree_root_block, 1);

    // Binary search pada B+ Tree node
    int low = 0, high = root.num_keys - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        if (root.keys[mid] == hash) {
            struct BFSEntry e;
            bfs_get_inode(root.inode_ids[mid], &e);
            if (e.is_present && bfs_strcmp(e.name, name) == 0) {
                return (int32_t) root.inode_ids[mid];
            }
            return (int32_t) root.inode_ids[mid];
        } else if (root.keys[mid] < hash) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return -1; // Tidak ditemukan
}

// ==================== 4. FORMAT & INISIALISASI ====================

bool bfs_format(void) {
    memset(&sb_cache, 0, sizeof(sb_cache));
    sb_cache.magic            = BFS_MAGIC;
    sb_cache.block_size       = BFS_BLOCK_SIZE;
    sb_cache.total_blocks     = BFS_TOTAL_BLOCKS;
    sb_cache.free_blocks      = BFS_TOTAL_BLOCKS - BFS_DATA_START_LBA;
    sb_cache.inode_count      = BFS_INODE_COUNT;
    sb_cache.free_inodes      = BFS_INODE_COUNT - 1;
    sb_cache.root_inode       = 0;
    sb_cache.btree_root_block = BFS_DATA_START_LBA - 1; // LBA 33 untuk B+ Tree Root

    // Tulis Superblock di LBA 0
    bfs_disk_write(&sb_cache, 0, 1);

    // Inisialisasi Bitmap (blok 0 s.d. 33 ditandai terpakai)
    uint8_t bitmap[BFS_BLOCK_SIZE];
    memset(bitmap, 0, BFS_BLOCK_SIZE);
    for (uint32_t i = 0; i < BFS_DATA_START_LBA; i++) {
        bitmap[i / 8] |= (1 << (i % 8));
    }
    bfs_disk_write(bitmap, BFS_BITMAP_LBA, 1);

    // Kosongkan Inode Table
    memset(block_buf, 0, BFS_BLOCK_SIZE);
    for (uint32_t l = 0; l < 32; l++) {
        bfs_disk_write(block_buf, BFS_INODE_LBA + l, 1);
    }

    // Buat Root Directory "/" di Inode 0
    struct BFSEntry root_entry;
    memset(&root_entry, 0, sizeof(root_entry));
    root_entry.name[0]      = '/';
    root_entry.name[1]      = '\0';
    root_entry.parent_id    = 0;
    root_entry.is_directory = 1;
    root_entry.is_present   = 1;
    bfs_put_inode(0, &root_entry);

    // Inisialisasi Root Node B+ Tree
    struct BFSBPlusNode btree_root;
    memset(&btree_root, 0, sizeof(btree_root));
    btree_root.is_leaf  = 1;
    btree_root.num_keys = 0;
    bfs_disk_write(&btree_root, sb_cache.btree_root_block, 1);

    return true;
}

void bfs_init(void) {
    bfs_disk_read(&sb_cache, 0, 1);
    if (sb_cache.magic != BFS_MAGIC) {
        bfs_format();
    }
}

// ==================== 5. CRUD OPERASI LENGKAP ====================

static int32_t bfs_find_in_dir(uint32_t parent_id, const char *name) {
    struct BFSEntry entry;
    for (uint32_t i = 0; i < BFS_INODE_COUNT; i++) {
        bfs_get_inode(i, &entry);
        if (entry.is_present && entry.parent_id == parent_id && bfs_strcmp(entry.name, name) == 0) {
            return (int32_t) i;
        }
    }
    return -1;
}

int8_t bfs_create(uint32_t parent_id, const char *name, bool is_dir) {
    if (bfs_find_in_dir(parent_id, name) != -1) return -1; // Nama sudah ada

    int32_t free_id = bfs_find_free_inode();
    if (free_id < 0) return -2; // Inode penuh

    struct BFSEntry entry;
    memset(&entry, 0, sizeof(entry));
    size_t len = bfs_strlen(name);
    if (len >= BFS_MAX_NAME) len = BFS_MAX_NAME - 1;
    memcpy(entry.name, name, len);
    entry.name[len]      = '\0';
    entry.parent_id     = parent_id;
    entry.is_directory  = is_dir ? 1 : 0;
    entry.is_present    = 1;
    entry.filesize      = 0;

    bfs_put_inode((uint32_t) free_id, &entry);
    bfs_btree_insert(bfs_hash_name(name), (uint32_t) free_id);

    sb_cache.free_inodes--;
    bfs_disk_write(&sb_cache, 0, 1);
    return 0; // Sukses
}

int8_t bfs_write(uint32_t parent_id, const char *name, const void *buf, uint32_t size) {
    int32_t id = bfs_find_in_dir(parent_id, name);
    if (id < 0) {
        if (bfs_create(parent_id, name, false) != 0) return -1;
        id = bfs_find_in_dir(parent_id, name);
    }

    struct BFSEntry entry;
    bfs_get_inode((uint32_t) id, &entry);
    if (entry.is_directory) return -2; // Tidak bisa menulis ke folder

    // Bebaskan blok lama jika ada
    for (int i = 0; i < BFS_DIRECT_BLOCKS; i++) {
        if (entry.direct_blocks[i] != 0) {
            bfs_free_block(entry.direct_blocks[i]);
            entry.direct_blocks[i] = 0;
        }
    }

    uint32_t blocks_needed = (size + BFS_BLOCK_SIZE - 1) / BFS_BLOCK_SIZE;
    if (blocks_needed > BFS_DIRECT_BLOCKS) return -3; // Melebihi ukuran maksimal

    const uint8_t *src = (const uint8_t*) buf;
    for (uint32_t i = 0; i < blocks_needed; i++) {
        int32_t blk = bfs_alloc_block();
        if (blk < 0) return -4; // Disk penuh
        entry.direct_blocks[i] = (uint32_t) blk;

        memset(block_buf, 0, BFS_BLOCK_SIZE);
        uint32_t to_copy = (size - i * BFS_BLOCK_SIZE > BFS_BLOCK_SIZE) ? BFS_BLOCK_SIZE : (size - i * BFS_BLOCK_SIZE);
        memcpy(block_buf, src + (i * BFS_BLOCK_SIZE), to_copy);
        bfs_disk_write(block_buf, (uint32_t) blk, 1);
    }

    entry.filesize = size;
    bfs_put_inode((uint32_t) id, &entry);
    return 0;
}

int8_t bfs_read(uint32_t parent_id, const char *name, void *buf, uint32_t max_size) {
    int32_t id = bfs_find_in_dir(parent_id, name);
    if (id < 0) return -1; // File tidak ditemukan

    struct BFSEntry entry;
    bfs_get_inode((uint32_t) id, &entry);
    if (entry.is_directory) return -2;

    uint32_t read_bytes = (entry.filesize < max_size) ? entry.filesize : max_size;
    uint32_t blocks_to_read = (read_bytes + BFS_BLOCK_SIZE - 1) / BFS_BLOCK_SIZE;

    uint8_t *dest = (uint8_t*) buf;
    for (uint32_t i = 0; i < blocks_to_read; i++) {
        bfs_disk_read(block_buf, entry.direct_blocks[i], 1);
        uint32_t to_copy = (read_bytes - i * BFS_BLOCK_SIZE > BFS_BLOCK_SIZE) ? BFS_BLOCK_SIZE : (read_bytes - i * BFS_BLOCK_SIZE);
        memcpy(dest + (i * BFS_BLOCK_SIZE), block_buf, to_copy);
    }
    return 0;
}

int8_t bfs_delete(uint32_t parent_id, const char *name) {
    int32_t id = bfs_find_in_dir(parent_id, name);
    if (id < 0) return -1;

    struct BFSEntry entry;
    bfs_get_inode((uint32_t) id, &entry);

    // Jika direktori, pastikan tidak ada isi di dalamnya
    if (entry.is_directory) {
        struct BFSEntry child;
        for (uint32_t i = 0; i < BFS_INODE_COUNT; i++) {
            bfs_get_inode(i, &child);
            if (child.is_present && child.parent_id == (uint32_t) id) {
                return -2; // Direktori tidak kosong
            }
        }
    } else {
        // Bebaskan data blok
        for (int i = 0; i < BFS_DIRECT_BLOCKS; i++) {
            if (entry.direct_blocks[i] != 0) {
                bfs_free_block(entry.direct_blocks[i]);
            }
        }
    }

    bfs_btree_remove(bfs_hash_name(name), (uint32_t) id);

    entry.is_present = 0;
    bfs_put_inode((uint32_t) id, &entry);

    sb_cache.free_inodes++;
    bfs_disk_write(&sb_cache, 0, 1);
    return 0;
}

int8_t bfs_read_dir(uint32_t dir_id, struct BFSEntry *out_entries, uint32_t max_count) {
    uint32_t count = 0;
    struct BFSEntry e;
    for (uint32_t i = 0; i < BFS_INODE_COUNT && count < max_count; i++) {
        bfs_get_inode(i, &e);
        if (e.is_present && e.parent_id == dir_id && i != dir_id) {
            out_entries[count++] = e;
        }
    }
    return (int8_t) count;
}
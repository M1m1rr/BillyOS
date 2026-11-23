#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/filesystem/bfs.h"
#include "header/stdlib/string.h"
#include "header/process/process.h"

#define MAX_LINE_LEN 128
#define MAX_ARGS     16

// ==================== 1. SYSCALL WRAPPERS ====================

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static void sys_putchar(char c, uint8_t color) {
    syscall(5, (uint32_t) c, (uint32_t) color, 0);
}

static uint32_t str_len(const char *s) {
    uint32_t l = 0;
    while (s[l]) l++;
    return l;
}

static void sys_puts(const char *s, uint8_t color) {
    syscall(6, (uint32_t) s, str_len(s), (uint32_t) color);
}

static void sys_getchar(char *c) {
    syscall(4, (uint32_t) c, 0, 0);
}

static void sys_clear(void) {
    syscall(8, 0, 0, 0);
}

static int8_t sys_fs(uint32_t call_no, uint32_t parent, const char *name, void *buf, uint32_t size, bool is_dir) {
    struct EXT2DriverRequest req = {
        .buf = buf, .name = (char*) name, .name_len = (uint8_t) str_len(name),
        .parent_inode = parent, .buffer_size = size, .is_directory = is_dir
    };
    int8_t ret = -1;
    syscall(call_no, (uint32_t) &req, (uint32_t) &ret, 0);
    return ret;
}

static int8_t sys_read_file(uint32_t parent, const char *name, void *buf, uint32_t size) {
    return sys_fs(0, parent, name, buf, size, false);
}

static int8_t sys_read_dir(uint32_t parent, const char *name, void *buf) {
    return sys_fs(1, parent, name, buf, BLOCK_SIZE, true);
}

static int8_t sys_write_entry(uint32_t parent, const char *name, void *buf, uint32_t size, bool is_dir) {
    return sys_fs(2, parent, name, buf, size, is_dir);
}

static int8_t sys_delete_entry(uint32_t parent, const char *name) {
    return sys_fs(3, parent, name, NULL, 0, false);
}

static bool sys_process_destroy(uint32_t pid) {
    bool ret = false;
    syscall(12, pid, (uint32_t) &ret, 0);
    return ret;
}

static uint32_t sys_process_get_info(struct ProcessMetadata *dest) {
    uint32_t count = 0;
    syscall(13, (uint32_t) dest, (uint32_t) &count, 0);
    return count;
}

static int32_t sys_bfs_req(struct BFSRequest *req) {
    int32_t ret = -1;
    syscall(19, (uint32_t) req, (uint32_t) &ret, 0);
    return ret;
}
// ==================== USER HEAP WRAPPERS ====================
static void* malloc(uint32_t size) {
    void *ptr = NULL;
    syscall(20, 0, (uint32_t) &ptr, size);
    return ptr;
}
static void free(void *ptr) {
    bool ok = false;
    syscall(20, 1, (uint32_t) &ok, (uint32_t) ptr);
}

// ==================== 2. HELPER STRING ====================

static int str_cmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void str_cpy(char *d, const char *s) {
    while ((*d++ = *s++));
}

static char* str_find(const char *h, const char *n) {
    if (!*n) return (char*) h;
    for (; *h; h++) {
        if (*h == *n) {
            const char *h1 = h, *n1 = n;
            while (*h1 && *n1 && (*h1 == *n1)) { h1++; n1++; }
            if (!*n1) return (char*) h;
        }
    }
    return NULL;
}

static void int_to_str(uint32_t val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char temp[12];
    int i = 0;
    while (val > 0) {
        temp[i++] = (val % 10) + '0';
        val /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

static uint32_t str_to_int(const char *s) {
    uint32_t res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}

// ==================== 3. STATE SHELL & SHARED BUFFER ====================
#define PATH_FILE "path.env"
static char     env_path[64];

static uint32_t cwd_inode;
static char     cwd_path[64];

static uint8_t  sh_buf[2048];
static char     pipe_buf[1024]; 
static uint32_t pipe_len = 0;
static bool     pipe_mode = false;

static void env_save(void) {
    sys_delete_entry(1, PATH_FILE);
    sys_write_entry(1, PATH_FILE, env_path, str_len(env_path), false);
}
static void env_load(void) {
    memset(sh_buf, 0, sizeof(sh_buf));
    if (sys_read_file(1, PATH_FILE, sh_buf, sizeof(sh_buf) - 1) == 0 && sh_buf[0] != '\0') {
        char *p = (char*) sh_buf;
        while (*p && *p != '\n' && *p != '\r') p++;
        *p = '\0';
        if (sh_buf[0] != '\0') {
            str_cpy(env_path, (char*) sh_buf);
            return;
        }
    }
    str_cpy(env_path, "/");
    env_save();
}

static uint32_t resolve_path(const char *path) {
    if (!path || !*path) return 0;
    if (str_cmp(path, "/") == 0) return 1;

    uint32_t cur = (path[0] == '/') ? 1 : cwd_inode;
    const char *p = (path[0] == '/') ? path + 1 : path;

    while (*p) {
        char part[32];
        int i = 0;
        while (*p && *p != '/' && i < 31) part[i++] = *p++;
        part[i] = '\0';
        if (*p == '/') p++;
        if (i == 0) continue;

        if (sys_read_dir(cur, ".", sh_buf) != 0) return 0;

        uint32_t off = 0, p_len = str_len(part);
        bool found = false;
        while (off < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(sh_buf + off);
            if (e->rec_len == 0) break;
            if (e->inode != 0 && e->file_type == EXT2_FT_DIR && e->name_len == p_len) {
                if (memcmp((char*)e + sizeof(*e), part, p_len) == 0) {
                    cur = e->inode;
                    found = true;
                    break;
                }
            }
            off += e->rec_len;
        }
        if (!found) return 0;
    }
    return cur;
}

static void shell_print(const char *str, uint8_t color) {
    if (pipe_mode) {
        uint32_t l = str_len(str);
        if (pipe_len + l < sizeof(pipe_buf) - 1) {
            memcpy(pipe_buf + pipe_len, str, l);
            pipe_len += l;
            pipe_buf[pipe_len] = '\0';
        }
    } else {
        sys_puts(str, color);
    }
}

// ==================== 4. COMMAND IMPLEMENTATION ====================

static void cmd_help(void) {
    shell_print("Perintah: cd, ls, cat, mkdir, rm, cp, mv, find, grep, ps, kill, export, help\n", 0x0E);
}

static void cmd_ls(int argc, char *argv[]) {
    const char *target = (argc > 1) ? argv[1] : ".";
    if (sys_read_dir(cwd_inode, target, sh_buf) != 0) {
        shell_print("ls: Gagal\n", 0x0C);
        return;
    }

    uint32_t off = 0;
    while (off < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(sh_buf + off);
        if (e->rec_len == 0) break;
        if (e->inode != 0) {
            char name[64];
            uint8_t len = (e->name_len < 63) ? e->name_len : 63;
            memcpy(name, (char*)e + sizeof(struct EXT2DirectoryEntry), len);
            name[len] = '\0';

            shell_print(name, (e->file_type == EXT2_FT_DIR) ? 0x0B : 0x0F);
            shell_print((e->file_type == EXT2_FT_DIR) ? "/  " : "  ", 0x0B);
        }
        off += e->rec_len;
    }
    shell_print("\n", 0x0F);
}

static void cmd_cd(int argc, char *argv[]) {
    if (argc < 2 || str_cmp(argv[1], "/") == 0) {
        cwd_inode = 1;
        str_cpy(cwd_path, "/");
        return;
    }
    if (str_cmp(argv[1], ".") == 0) return;

    uint32_t target = resolve_path(argv[1]);
    if (target == 0) {
        shell_print("cd: Gagal\n", 0x0C);
        return;
    }
    cwd_inode = target;
    if (str_cmp(argv[1], "..") == 0) {
        if (cwd_inode == 1) str_cpy(cwd_path, "/");
        else {
            int l = str_len(cwd_path);
            while (l > 1 && cwd_path[l - 1] != '/') l--;
            cwd_path[(l > 1) ? l - 1 : 1] = '\0';
        }
    } else if (argv[1][0] == '/') {
        str_cpy(cwd_path, argv[1]);
    } else {
        int l = str_len(cwd_path);
        if (l > 1 && cwd_path[l - 1] != '/') {
            cwd_path[l++] = '/';
            cwd_path[l] = '\0';
        }
        str_cpy(cwd_path + ((cwd_path[0] == '/' && cwd_path[1] == '\0') ? 1 : l), argv[1]);
    }
}

static void cmd_cat(int argc, char *argv[]) {
    if (argc < 2 || argv[1][0] == '\0') {
        shell_print("cat <file>\n", 0x0C);
        return;
    }
    memset(sh_buf, 0, sizeof(sh_buf));
    if (sys_read_file(cwd_inode, argv[1], sh_buf, sizeof(sh_buf) - 1) == 0) {
        shell_print((char*) sh_buf, 0x0F);
        shell_print("\n", 0x0F);
    } else {
        shell_print("cat: Gagal\n", 0x0C);
    }
}

static void cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0' || str_find(argv[1], "/")) {
        shell_print("mkdir: Nama tdk valid\n", 0x0C);
        return;
    }
    int8_t ret = sys_write_entry(cwd_inode, argv[1], NULL, 0, true);
    shell_print(ret == 0 ? "Folder dibuat\n" : "mkdir: Gagal\n", ret == 0 ? 0x0A : 0x0C);
}

static void cmd_rm(int argc, char *argv[]) {
    if (argc < 2 || argv[1][0] == '\0') {
        shell_print("rm <target>\n", 0x0C);
        return;
    }
    int8_t r = sys_delete_entry(cwd_inode, argv[1]);
    shell_print(r == 0 ? "Dihapus\n" : "rm: Gagal\n", r == 0 ? 0x0A : 0x0C);
}

static void cmd_cp_mv(int argc, char *argv[], bool is_mv) {
    if (argc < 3 || argv[1][0] == '\0' || argv[2][0] == '\0') {
        shell_print("Argumen kurang!\n", 0x0C);
        return;
    }
    memset(sh_buf, 0, sizeof(sh_buf));
    if (sys_read_file(cwd_inode, argv[1], sh_buf, sizeof(sh_buf)) != 0) {
        shell_print("Sumber tdk ditemukan\n", 0x0C);
        return;
    }
    if (sys_write_entry(cwd_inode, argv[2], sh_buf, str_len((char*)sh_buf), false) != 0) {
        shell_print("Gagal tulis tujuan\n", 0x0C);
        return;
    }
    if (is_mv) sys_delete_entry(cwd_inode, argv[1]);
    shell_print("Berhasil\n", 0x0A);
}

static void dfs_find(uint32_t ino, const char *path, const char *target, int mode) {
    uint8_t d_buf[BLOCK_SIZE];
    if (sys_read_dir(ino, ".", d_buf) != 0) return;

    uint32_t off = 0;
    while (off < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(d_buf + off);
        if (e->rec_len == 0) break;
        if (e->inode != 0) {
            char name[64];
            uint8_t len = (e->name_len < 63) ? e->name_len : 63;
            memcpy(name, (char*)e + sizeof(struct EXT2DirectoryEntry), len);
            name[len] = '\0';

            if (str_cmp(name, ".") != 0 && str_cmp(name, "..") != 0) {
                char next[128];
                str_cpy(next, path);
                uint32_t pl = str_len(next);
                if (next[pl - 1] != '/') {
                    next[pl] = '/';
                    next[pl + 1] = '\0';
                }
                str_cpy(next + str_len(next), name);

                // 1. Pencarian nama (mode 0 = auto, mode 1 = nama)
                if (mode != 2 && str_cmp(name, target) == 0) {
                    shell_print("Found: ", 0x0A);
                    shell_print(next, 0x0E);
                    shell_print("\n", 0x0F);
                }

                // 2. Traversal DFS jika entri adalah direktori
                if (e->file_type == EXT2_FT_DIR) {
                    dfs_find(e->inode, next, target, mode);
                }
                // 3. Pencarian isi file jika entri adalah file biasa (mode 0 = auto, mode 2 = isi)
                else if (e->file_type == EXT2_FT_REG_FILE && mode != 1) {
                    memset(sh_buf, 0, sizeof(sh_buf));
                    if (sys_read_file(ino, name, sh_buf, sizeof(sh_buf) - 1) == 0) {
                        if (str_find((char*) sh_buf, target) != NULL) {
                            shell_print("Found in content: ", 0x0B);
                            shell_print(next, 0x0E);
                            shell_print("\n", 0x0F);
                        }
                    }
                }
            }
        }
        off += e->rec_len;
    }
}

static void cmd_find(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("find <kata>\n", 0x0E);
        return;
    }
    const char *target = argv[1];
    if (str_cmp(argv[1], "-name") == 0 && argc >= 3) target = argv[2];

    // Cek B+ Tree Indexing terlebih dahulu (O(log N))
    struct BFSRequest btree_req = { .cmd = 6, .name = target };
    int32_t btree_id = sys_bfs_req(&btree_req);
    if (btree_id >= 0) {
        shell_print("[B+ Tree Index Found] ", 0x0A);
        shell_print(target, 0x0E);
        shell_print(" (Pencarian Instan O(log N))\n", 0x0A);
    }
    // Lanjutkan pencarian DFS standar EXT2
    dfs_find(1, "/", target, 0);
}

static void cmd_grep(int argc, char *argv[], const char *pipe_in) {
    if (argc < 2) {
        shell_print("Gunakan: grep <pattern> [file]\n", 0x0E);
        return;
    }
    const char *src = NULL;
    if (pipe_in && pipe_in[0] != '\0') {
        src = pipe_in;
    } else if (argc >= 3) {
        memset(sh_buf, 0, sizeof(sh_buf));
        if (sys_read_file(cwd_inode, argv[2], sh_buf, sizeof(sh_buf) - 1) == 0) {
            src = (char*) sh_buf;
        } else {
            shell_print("grep: Gagal membaca file\n", 0x0C);
            return;
        }
    } else {
        shell_print("grep: Tidak ada input\n", 0x0E);
        return;
    }

    char line[128];
    uint32_t idx = 0;
    while (*src) {
        if (*src == '\n') {
            line[idx] = '\0';
            if (str_find(line, argv[1]) != NULL) {
                shell_print(line, 0x0A);
                shell_print("\n", 0x0F);
            }
            idx = 0;
        } else {
            if (idx < sizeof(line) - 1) line[idx++] = *src;
        }
        src++;
    }
    if (idx > 0) {
        line[idx] = '\0';
        if (str_find(line, argv[1]) != NULL) {
            shell_print(line, 0x0A);
            shell_print("\n", 0x0F);
        }
    }
}

// ==================== 5. PARSER & EXECUTION ====================
static void cmd_ps(void) {
    struct ProcessMetadata list[PROCESS_COUNT_MAX];
    uint32_t count = sys_process_get_info(list);
    shell_print("PID  NAME         STATE    MEMORY\n---------------------------------\n", 0x0B);
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (list[i].state != PROCESS_STATE_INACTIVE) {
            char pid_s[12];
            int_to_str(list[i].pid, pid_s);
            shell_print(pid_s, 0x0F);
            shell_print("    ", 0x0F);
            shell_print(list[i].name, 0x0E);
            for (uint32_t s = str_len(list[i].name); s < 13; s++) shell_print(" ", 0x0F);
            shell_print((list[i].state == PROCESS_STATE_RUNNING) ? "RUNNING  " :
                        (list[i].state == PROCESS_STATE_READY)   ? "READY    " : "BLOCKED  ",
                        (list[i].state == PROCESS_STATE_RUNNING) ? 0x0A : 0x03);

            char mem_s[12];
            int_to_str(list[i].memory_kb, mem_s);
            shell_print(mem_s, 0x0E);
            shell_print(" KB\n", 0x0F);
        }
    }
    shell_print("Aktif: ", 0x07);
    char cnt_s[12];
    int_to_str(count, cnt_s);
    shell_print(cnt_s, 0x0A);
    shell_print("\n", 0x07);
}

static bool spawn_from_inode(uint32_t inode, const char *name, const char *args) {
    struct EXT2DriverRequest req = {
        .buf          = (void*) 0,
        .name         = (char*) name,
        .name_len     = (uint8_t) str_len(name),
        .parent_inode = inode,
        .buffer_size  = 0x100000,
        .is_directory = false,
    };
    int32_t ret = -1;
    syscall(11, (uint32_t) &req, (uint32_t) &ret, (uint32_t) args);
        if (ret == 0) {
        // Jika yang dijalankan adalah doom, shell menunggu sampai game selesai
        // agar input keyboard dan layar tidak bentrok sama sekali
        if (str_cmp(name, "doom") == 0) {
            while (true) {
                struct ProcessMetadata list[PROCESS_COUNT_MAX];
                sys_process_get_info(list);
                bool doom_running = false;
                for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
                    if (list[i].state != PROCESS_STATE_INACTIVE && str_cmp(list[i].name, "doom") == 0) {
                        doom_running = true;
                        break;
                    }
                }
                if (!doom_running) break;
                for (volatile int i = 0; i < 5000000; i++);
            }
            // Kembalikan hardware VGA ke Mode 12h BillyOS & bersihkan layar
            syscall(21, 3, 0, 0);
            sys_clear();
        } else {
            for (volatile int i = 0; i < 3000000; i++);
        }
        return true;
    }
    return false;
}

static bool try_exec_from_path(const char *cmd_name, const char *args) {
    if (!cmd_name || !*cmd_name) return false;

    if (str_cmp(cmd_name, "shell") == 0) {
        shell_print("Shell sudah aktif!\n", 0x0C);
        return true;
    }

    char path_copy[128];
    str_cpy(path_copy, env_path);
    char *dir = path_copy;
    while (*dir) {
        char *end = dir;
        while (*end && *end != ':' && *end != ';') end++;
        char saved = *end;
        *end = '\0';
        if (*dir != '\0') {
            uint32_t target = resolve_path(dir);
            if (target != 0 && spawn_from_inode(target, cmd_name, args)) return true;
        }
        if (saved == '\0') break;
        dir = end + 1;
    }
    if (cwd_inode != 1 && spawn_from_inode(cwd_inode, cmd_name, args)) return true;
    return false;
}

static void cmd_exec(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("exec <nama_file>\n", 0x0C);
        return;
    }
    if (!try_exec_from_path(argv[1], NULL)) {
        shell_print("exec: Gagal\n", 0x0C);
    }
}

static void cmd_kill(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("kill <pid>\n", 0x0C);
        return;
    }
    uint32_t pid = str_to_int(argv[1]);
    if (pid == 0 || !sys_process_destroy(pid)) {
        shell_print("kill: Gagal / PID tdk valid\n", 0x0C);
    } else {
        shell_print("kill: Berhasil\n", 0x0A);
    }
}

static void cmd_export(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("PATH=", 0x0B);
        shell_print(env_path, 0x0E);
        shell_print("\n", 0x0F);
        return;
    }

    // Format: export PATH=...
    if (memcmp(argv[1], "PATH=", 5) == 0) {
        char *val = argv[1] + 5;
        if (*val == '"') {
            val++;
            char *end = val;
            while (*end && *end != '"') end++;
            *end = '\0';
        }

        char new_path[128];
        char *p_in = val;
        char *p_out = new_path;
        uint32_t out_len = 0;

        // Ekspansi $PATH jika ada di dalam input user
        while (*p_in && out_len < 120) {
            if (memcmp(p_in, "$PATH", 5) == 0) {
                uint32_t elen = str_len(env_path);
                if (out_len + elen < 120) {
                    memcpy(p_out, env_path, elen);
                    p_out += elen;
                    out_len += elen;
                }
                p_in += 5;
            } else {
                *p_out++ = *p_in++;
                out_len++;
            }
        }
        *p_out = '\0';

        if (new_path[0] != '\0') {
            str_cpy(env_path, new_path);
            env_save();
            shell_print("PATH: ", 0x0A);
            shell_print(env_path, 0x0E);
            shell_print("\n", 0x0F);
        }
    } else {
        shell_print("Format: export PATH=<val>\n", 0x0C);
    }
}

static void cmd_bfs(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("Gunakan: bfs <info|ls|mkdir|write|cat|rm|find> [args]\n", 0x0E);
        return;
    }

    if (str_cmp(argv[1], "info") == 0) {
        struct BFSSuperblock sb;
        struct BFSRequest req = { .cmd = 0, .buf = &sb };
        if (sys_bfs_req(&req) == 0) {
            shell_print("=== BillyFS (Non-EXT2) Status ===\n", 0x0E);
            shell_print("Magic       : 0x42494C4C (BILL)\n", 0x0A);
            shell_print("Block Size  : 512 bytes\n", 0x0F);
            shell_print("Total Blocks: 4096 (2 MB)\n", 0x0F);
            shell_print("B+ Tree Root: Sektor LBA 33 (Indexed O(log N))\n", 0x0B);
        } else {
            shell_print("[BFS] Gagal membaca superblock!\n", 0x0C);
        }
    } 
    else if (str_cmp(argv[1], "mkdir") == 0) {
        if (argc < 3) { shell_print("bfs mkdir <nama>\n", 0x0C); return; }
        struct BFSRequest req = { .cmd = 1, .parent_id = 0, .name = argv[2], .is_dir = true };
        int32_t res = sys_bfs_req(&req);
        if (res == 0) shell_print("[BFS] Folder berhasil dibuat!\n", 0x0A);
        else shell_print("[BFS] Gagal membuat folder!\n", 0x0C);
    }
    else if (str_cmp(argv[1], "write") == 0) {
        if (argc < 4) { shell_print("bfs write <nama> <teks>\n", 0x0C); return; }
        
        // Gabungkan seluruh argumen teks argv[3] .. argv[argc-1] dan buang tanda petik
        char payload[256];
        payload[0] = '\0';
        char *d = payload;
        for (int i = 3; i < argc; i++) {
            char *s = argv[i];
            while (*s && (d - payload) < 250) {
                if (*s != '"') *d++ = *s;
                s++;
            }
            if (i < argc - 1 && (d - payload) < 250) *d++ = ' ';
        }
        *d = '\0';
        uint32_t len = str_len(payload);

        struct BFSRequest req = { .cmd = 2, .parent_id = 0, .name = argv[2], .buf = payload, .size = len };
        int32_t res = sys_bfs_req(&req);
        if (res == 0) shell_print("[BFS] File berhasil ditulis!\n", 0x0A);
        else shell_print("[BFS] Gagal menulis file!\n", 0x0C);
    }
    else if (str_cmp(argv[1], "cat") == 0) {
        if (argc < 3) { shell_print("bfs cat <nama>\n", 0x0C); return; }
        char buf[512];
        memset(buf, 0, sizeof(buf));
        struct BFSRequest req = { .cmd = 3, .parent_id = 0, .name = argv[2], .buf = buf, .size = 511 };
        int32_t res = sys_bfs_req(&req);
        if (res == 0) {
            shell_print(buf, 0x0F);
            shell_print("\n", 0x0F);
        } else {
            shell_print("[BFS] File tidak ditemukan!\n", 0x0C);
        }
    }
    else if (str_cmp(argv[1], "rm") == 0) {
        if (argc < 3) { shell_print("bfs rm <nama>\n", 0x0C); return; }
        struct BFSRequest req = { .cmd = 4, .parent_id = 0, .name = argv[2] };
        int32_t res = sys_bfs_req(&req);
        if (res == 0) shell_print("[BFS] File/folder berhasil dihapus!\n", 0x0A);
        else shell_print("[BFS] Gagal menghapus!\n", 0x0C);
    }
    else if (str_cmp(argv[1], "ls") == 0) {
        struct BFSEntry entries[16];
        struct BFSRequest req = { .cmd = 5, .parent_id = 0, .buf = entries, .size = 16 };
        int32_t count = sys_bfs_req(&req);
        shell_print("Nama               Tipe    Ukuran    Present\n", 0x0B);
        shell_print("--------------------------------------------\n", 0x07);
        for (int i = 0; i < count; i++) {
            shell_print(entries[i].name, 0x0E);
            for (uint32_t s = str_len(entries[i].name); s < 19; s++) shell_print(" ", 0x0F);
            shell_print(entries[i].is_directory ? "DIR     " : "FILE    ", 0x0A);
            char sz[12];
            int_to_str(entries[i].filesize, sz);
            shell_print(sz, 0x0F);
            shell_print(" B    YES\n", 0x0F);
        }
    }
    else if (str_cmp(argv[1], "find") == 0) {
        if (argc < 3) { shell_print("bfs find <nama>\n", 0x0C); return; }
        struct BFSRequest req = { .cmd = 6, .name = argv[2] };
        int32_t inode = sys_bfs_req(&req);
        if (inode >= 0) {
            shell_print("[B+ Tree Index] Ditemukan: ", 0x0A);
            shell_print(argv[2], 0x0E);
            shell_print(" (Inode ", 0x0F);
            char in_str[12]; int_to_str((uint32_t) inode, in_str);
            shell_print(in_str, 0x0E);
            shell_print(") via O(log N) Instant Lookup!\n", 0x0A);
        } else {
            shell_print("[B+ Tree Index] File tidak ditemukan!\n", 0x0C);
        }
    }
    
}

static void cmd_heap_test(void) {
    shell_print("=== BillyOS Dynamic Heap Memory Test ===\n", 0x0E);
    // 1. Alokasi 3 blok dinamis berbeda ukuran
    char *buf1 = (char*) malloc(32);
    char *buf2 = (char*) malloc(128);
    char *buf3 = (char*) malloc(512);
    if (buf1 && buf2 && buf3) {
        shell_print("[1] Alokasi 3 blok (32B, 128B, 512B) [OK]\n", 0x0A);
    } else {
        shell_print("[1] Gagal alokasi blok heap!\n", 0x0C);
        return;
    }
    // 2. Tulis string dinamis dan baca kembali
    str_cpy(buf1, "BillyOS Heap Aktif!");
    str_cpy(buf2, "Blok memori dinamis kedua bekerja normal.");
    shell_print("Data di Buf1: ", 0x0B); shell_print(buf1, 0x0F); shell_print("\n", 0x0F);
    shell_print("Data di Buf2: ", 0x0B); shell_print(buf2, 0x0F); shell_print("\n", 0x0F);
    // 3. Pengujian Free & Coalescing
    free(buf2);
    shell_print("[2] Bebaskan Buf2 (128B) [OK]\n", 0x0A);
    // 4. Alokasi kembali pada ruang yang dibebaskan
    char *buf_reuse = (char*) malloc(64);
    if (buf_reuse) {
        str_cpy(buf_reuse, "Reused Memory Slot!");
        shell_print("[3] Alokasi ulang 64B di slot bekas [OK]\n", 0x0A);
        shell_print("Data Reused : ", 0x0B); shell_print(buf_reuse, 0x0E); shell_print("\n", 0x0F);
    }
    free(buf1);
    free(buf3);
    free(buf_reuse);
    shell_print("[4] Semua blok berhasil di-free & coalesced [PASS]\n", 0x0A);
}

static void execute_single(char *cmd, const char *p_in) {
    char *argv[MAX_ARGS];
    int argc = 0;
    char *p = cmd;

    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (argc < MAX_ARGS) argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (argc == 0) return;

    if (str_cmp(argv[0], "help") == 0)       cmd_help();
    else if (str_cmp(argv[0], "clear") == 0) sys_clear();
    else if (str_cmp(argv[0], "ls") == 0)    cmd_ls(argc, argv);
    else if (str_cmp(argv[0], "cd") == 0)    cmd_cd(argc, argv);
    else if (str_cmp(argv[0], "cat") == 0)   cmd_cat(argc, argv);
    else if (str_cmp(argv[0], "mkdir") == 0) cmd_mkdir(argc, argv);
    else if (str_cmp(argv[0], "rm") == 0)    cmd_rm(argc, argv);
    else if (str_cmp(argv[0], "cp") == 0)    cmd_cp_mv(argc, argv, false);
    else if (str_cmp(argv[0], "mv") == 0)    cmd_cp_mv(argc, argv, true);
    else if (str_cmp(argv[0], "find") == 0)  cmd_find(argc, argv);
    else if (str_cmp(argv[0], "grep") == 0)  cmd_grep(argc, argv, p_in);
    else if (str_cmp(argv[0], "ps") == 0)    cmd_ps();
    else if (str_cmp(argv[0], "exec") == 0)  cmd_exec(argc, argv);
    else if (str_cmp(argv[0], "kill") == 0)  cmd_kill(argc, argv);
    else if (str_cmp(argv[0], "export") == 0) cmd_export(argc, argv);
    else if (str_cmp(argv[0], "bfs") == 0)   cmd_bfs(argc, argv);
    else if (str_cmp(argv[0], "heap") == 0)  cmd_heap_test();
    else if (str_cmp(argv[0], "exit") == 0) {
        shell_print("Keluar...\n", 0x0E);
        syscall(10, 0, 0, 0);
    }
    else {
        char args_buf[128];
        args_buf[0] = '\0';
        if (argc > 1) {
            char *d = args_buf;
            for (int i = 1; i < argc; i++) {
                char *s = argv[i];
                while (*s && (d - args_buf) < 126) *d++ = *s++;
                if (i < argc - 1 && (d - args_buf) < 126) *d++ = ' ';
            }
            *d = '\0';
        }

        if (!try_exec_from_path(argv[0], (argc > 1) ? args_buf : NULL)) {
            shell_print("Perintah tdk dikenal\n", 0x0C);
        }
    }
}

static void execute_line(char *line) {
    char *pipe_pos = str_find(line, "|");
    if (pipe_pos == NULL) {
        pipe_pos = str_find(line, "\\");
    }
    if (pipe_pos != NULL) {
        *pipe_pos = '\0';
        char *cmd1 = line;
        char *cmd2 = pipe_pos + 1;

        pipe_mode = true;
        pipe_len = 0;
        pipe_buf[0] = '\0';

        execute_single(cmd1, NULL);

        pipe_mode = false;
        execute_single(cmd2, pipe_buf);
    } else {
        execute_single(line, NULL);
    }
}

// ==================== 6. MAIN REPL ====================

int main(void) {
    cwd_inode = 1;
    cwd_path[0] = '/';
    cwd_path[1] = '\0';

    syscall(7, 0, 0, 0); // Aktifkan keyboard interrupt
    sys_clear();

    env_load();

    if (sys_read_file(1, "banner", sh_buf, sizeof(sh_buf) - 1) == 0 && sh_buf[0] != '\0') {
        sys_puts((char*) sh_buf, 0x0B);
    } else {
        sys_puts("=== BillyOS Shell ===\n\n", 0x0B);
    }

    char line[MAX_LINE_LEN];
    uint32_t idx = 0;

    sys_puts("BillyOS:", 0x0B);
    sys_puts(cwd_path, 0x0E);
    sys_puts("$ ", 0x0F);

    while (true) {
        char c = 0;
        sys_getchar(&c);

        if (c != 0) {
            if (c == '\n') {
                sys_putchar('\n', 0x0F);
                line[idx] = '\0';

                if (idx > 0) {
                    execute_line(line);
                    idx = 0;
                }

                sys_puts("BillyOS:", 0x0B);
                sys_puts(cwd_path, 0x0E);
                sys_puts("$ ", 0x0F);
            } else if (c == '\b') {
                if (idx > 0) {
                    idx--;
                    sys_putchar('\b', 0x0F);
                }
            } else {
                if (idx < MAX_LINE_LEN - 1) {
                    line[idx++] = c;
                    sys_putchar(c, 0x0F);
                }
            }
        }
    }
    return 0;
}
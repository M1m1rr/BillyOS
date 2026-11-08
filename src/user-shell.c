#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

#define MAX_LINE_LEN 128
#define MAX_ARGS     16

// ==================== 1. SYSCALL WRAPPERS ====================

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static void sys_putchar(char c, uint8_t color) {
    syscall(5, (uint32_t) c, (uint32_t) color, 0);
}

static void sys_puts(const char *s, uint8_t color) {
    uint32_t len = 0;
    while (s[len]) len++;
    syscall(6, (uint32_t) s, len, (uint32_t) color);
}

static void sys_getchar(char *c) {
    syscall(4, (uint32_t) c, 0, 0);
}

static void sys_clear(void) {
    syscall(8, 0, 0, 0);
}

static int8_t sys_read_file(uint32_t parent, const char *name, void *buf, uint32_t size) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;
    struct EXT2DriverRequest req = {
        .buf = buf, .name = (char*) name, .name_len = nlen,
        .parent_inode = parent, .buffer_size = size, .is_directory = false
    };
    int8_t ret = -1;
    syscall(0, (uint32_t) &req, (uint32_t) &ret, 0);
    return ret;
}

static int8_t sys_read_dir(uint32_t parent, const char *name, void *buf) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;
    struct EXT2DriverRequest req = {
        .buf = buf, .name = (char*) name, .name_len = nlen,
        .parent_inode = parent, .buffer_size = BLOCK_SIZE, .is_directory = true
    };
    int8_t ret = -1;
    syscall(1, (uint32_t) &req, (uint32_t) &ret, 0);
    return ret;
}

static int8_t sys_write_entry(uint32_t parent, const char *name, void *buf, uint32_t size, bool is_dir) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;
    struct EXT2DriverRequest req = {
        .buf = buf, .name = (char*) name, .name_len = nlen,
        .parent_inode = parent, .buffer_size = size, .is_directory = is_dir
    };
    int8_t ret = -1;
    syscall(2, (uint32_t) &req, (uint32_t) &ret, 0);
    return ret;
}

static int8_t sys_delete_entry(uint32_t parent, const char *name) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;
    struct EXT2DriverRequest req = {
        .buf = NULL, .name = (char*) name, .name_len = nlen,
        .parent_inode = parent, .buffer_size = 0, .is_directory = false
    };
    int8_t ret = -1;
    syscall(3, (uint32_t) &req, (uint32_t) &ret, 0);
    return ret;
}

// ==================== 2. HELPER STRING ====================

static uint32_t str_len(const char *s) {
    uint32_t l = 0;
    while (s[l]) l++;
    return l;
}

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

// ==================== 3. STATE SHELL & SHARED BUFFER ====================

static uint32_t cwd_inode = 1;
static char     cwd_path[64] = "/";

static uint8_t  sh_buf[1024];       // Buffer bersama untuk file operations
static char     pipe_buf[1024];     // Buffer pipeline
static uint32_t pipe_len = 0;
static bool     pipe_mode = false;

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
    shell_print("Perintah: cd, ls, mkdir, cat, cp, rm, mv, find, grep, clear, help\n", 0x0E);
    shell_print("Contoh: cd folder1 | ls | cat file | ls | grep test\n", 0x07);
}

static void cmd_ls(int argc, char *argv[]) {
    const char *target = (argc > 1) ? argv[1] : ".";
    uint8_t d_buf[BLOCK_SIZE];
    memset(d_buf, 0, BLOCK_SIZE);

    if (sys_read_dir(cwd_inode, target, d_buf) != 0) {
        shell_print("ls: Direktori tidak ditemukan\n", 0x0C);
        return;
    }

    uint32_t off = 0;
    while (off < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(d_buf + off);
        if (e->rec_len == 0) break;
        if (e->inode != 0) {
            char name[64];
            uint8_t len = (e->name_len < 63) ? e->name_len : 63;
            memcpy(name, (char*)e + sizeof(struct EXT2DirectoryEntry), len);
            name[len] = '\0';

            if (pipe_mode) {
                shell_print(name, 0x0F);
                shell_print("\n", 0x0F); // Satu baris per file untuk input grep
            } else {
                if (e->file_type == EXT2_FT_DIR) {
                    shell_print(name, 0x0B);
                    shell_print("/  ", 0x0B);
                } else {
                    shell_print(name, 0x0F);
                    shell_print("  ", 0x0F);
                }
            }
        }
        off += e->rec_len;
    }
    if (!pipe_mode) shell_print("\n", 0x0F);
}

static void cmd_cd(int argc, char *argv[]) {
    if (argc < 2 || str_cmp(argv[1], "/") == 0) {
        cwd_inode = 1;
        str_cpy(cwd_path, "/");
        return;
    }
    if (str_cmp(argv[1], ".") == 0) return;

    uint8_t d_buf[BLOCK_SIZE];
    if (sys_read_dir(cwd_inode, ".", d_buf) != 0) return;

    uint32_t off = 0;
    bool found = false;
    while (off < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(d_buf + off);
        if (e->rec_len == 0) break;
        if (e->inode != 0) {
            char name[64];
            uint8_t len = (e->name_len < 63) ? e->name_len : 63;
            memcpy(name, (char*)e + sizeof(struct EXT2DirectoryEntry), len);
            name[len] = '\0';

            if (str_cmp(name, argv[1]) == 0) {
                if (e->file_type != EXT2_FT_DIR) {
                    shell_print("cd: Bukan direktori\n", 0x0C);
                    return;
                }
                cwd_inode = e->inode;
                found = true;
                if (str_cmp(argv[1], "..") == 0) {
                    if (cwd_inode == 1) {
                        str_cpy(cwd_path, "/");
                    } else {
                        int l = str_len(cwd_path);
                        while (l > 1 && cwd_path[l - 1] != '/') l--;
                        if (l > 1) cwd_path[l - 1] = '\0';
                        else cwd_path[1] = '\0';
                    }
                } else {
                    int l = str_len(cwd_path);
                    if (l > 1 && cwd_path[l - 1] != '/') {
                        cwd_path[l] = '/';
                        cwd_path[l + 1] = '\0';
                    }
                    if (cwd_path[0] == '/' && cwd_path[1] == '\0') {
                        str_cpy(cwd_path + 1, argv[1]);
                    } else {
                        str_cpy(cwd_path + str_len(cwd_path), argv[1]);
                    }
                }
                break;
            }
        }
        off += e->rec_len;
    }
    if (!found) shell_print("cd: Direktori tidak ditemukan\n", 0x0C);
}

static void cmd_cat(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("Gunakan: cat <file>\n", 0x0E);
        return;
    }
    memset(sh_buf, 0, sizeof(sh_buf));
    if (sys_read_file(cwd_inode, argv[1], sh_buf, sizeof(sh_buf) - 1) == 0) {
        shell_print((char*) sh_buf, 0x0F);
        shell_print("\n", 0x0F);
    } else {
        shell_print("cat: Gagal membaca file\n", 0x0C);
    }
}

static void cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("Gunakan: mkdir <folder>\n", 0x0E);
        return;
    }
    if (sys_write_entry(cwd_inode, argv[1], NULL, 0, true) == 0) {
        shell_print("Folder berhasil dibuat\n", 0x0A);
    } else {
        shell_print("mkdir: Gagal membuat folder\n", 0x0C);
    }
}

static void cmd_rm(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("Gunakan: rm <target>\n", 0x0E);
        return;
    }
    int8_t r = sys_delete_entry(cwd_inode, argv[1]);
    if (r == 0) shell_print("Berhasil dihapus\n", 0x0A);
    else if (r == -1) shell_print("rm: Ditolak (Proteksi Root/CWD)\n", 0x0C);
    else shell_print("rm: File/folder tidak ditemukan\n", 0x0C);
}

static void cmd_cp(int argc, char *argv[]) {
    if (argc < 3) {
        shell_print("Gunakan: cp <src> <dst>\n", 0x0E);
        return;
    }
    memset(sh_buf, 0, sizeof(sh_buf));
    if (sys_read_file(cwd_inode, argv[1], sh_buf, sizeof(sh_buf)) != 0) {
        shell_print("cp: File sumber tidak ditemukan\n", 0x0C);
        return;
    }
    if (sys_write_entry(cwd_inode, argv[2], sh_buf, str_len((char*)sh_buf), false) == 0) {
        shell_print("File berhasil disalin\n", 0x0A);
    } else {
        shell_print("cp: Gagal menulis file tujuan\n", 0x0C);
    }
}

static void cmd_mv(int argc, char *argv[]) {
    if (argc < 3) {
        shell_print("Gunakan: mv <src> <dst>\n", 0x0E);
        return;
    }
    memset(sh_buf, 0, sizeof(sh_buf));
    if (sys_read_file(cwd_inode, argv[1], sh_buf, sizeof(sh_buf)) != 0) {
        shell_print("mv: File sumber tidak ditemukan\n", 0x0C);
        return;
    }
    if (sys_write_entry(cwd_inode, argv[2], sh_buf, str_len((char*)sh_buf), false) != 0) {
        shell_print("mv: Gagal menulis file tujuan\n", 0x0C);
        return;
    }
    sys_delete_entry(cwd_inode, argv[1]);
    shell_print("File berhasil dipindahkan\n", 0x0A);
}

static void dfs_find(uint32_t ino, const char *path, const char *target) {
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

                if (str_cmp(name, target) == 0) {
                    shell_print("Found: ", 0x0A);
                    shell_print(next, 0x0E);
                    shell_print("\n", 0x0F);
                }
                if (e->file_type == EXT2_FT_DIR) dfs_find(e->inode, next, target);
            }
        }
        off += e->rec_len;
    }
}

static void cmd_find(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("Gunakan: find <nama>\n", 0x0E);
        return;
    }
    dfs_find(1, "/", argv[1]);
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
    else if (str_cmp(argv[0], "cp") == 0)    cmd_cp(argc, argv);
    else if (str_cmp(argv[0], "mv") == 0)    cmd_mv(argc, argv);
    else if (str_cmp(argv[0], "find") == 0)  cmd_find(argc, argv);
    else if (str_cmp(argv[0], "grep") == 0)  cmd_grep(argc, argv, p_in);
    else {
        shell_print("Perintah tidak dikenal: ", 0x0C);
        shell_print(argv[0], 0x0C);
        shell_print(" (ketik 'help')\n", 0x0C);
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
    syscall(7, 0, 0, 0); // Aktifkan keyboard interrupt
    sys_clear();

    sys_puts("============================================\n", 0x0B);
    sys_puts("          BillyOS Shell (User Mode)         \n", 0x0E);
    sys_puts("============================================\n", 0x0B);
    sys_puts("Ketik 'help' untuk daftar perintah.\n\n", 0x07);

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
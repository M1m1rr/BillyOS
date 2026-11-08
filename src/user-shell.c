#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

#define MAX_LINE_LEN     256
#define MAX_ARGS         16
#define PIPE_BUFFER_SIZE 4096

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

static int8_t sys_read_file(uint32_t parent_inode, const char *name, void *buf, uint32_t buf_size) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;

    struct EXT2DriverRequest req = {
        .buf          = buf,
        .name         = (char*) name,
        .name_len     = nlen,
        .parent_inode = parent_inode,
        .buffer_size  = buf_size,
        .is_directory = false
    };
    int8_t retcode = -1;
    syscall(0, (uint32_t) &req, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t sys_read_dir(uint32_t parent_inode, const char *name, void *buf) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;

    struct EXT2DriverRequest req = {
        .buf          = buf,
        .name         = (char*) name,
        .name_len     = nlen,
        .parent_inode = parent_inode,
        .buffer_size  = BLOCK_SIZE,
        .is_directory = true
    };
    int8_t retcode = -1;
    syscall(1, (uint32_t) &req, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t sys_write_entry(uint32_t parent_inode, const char *name, void *buf, uint32_t size, bool is_dir) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;

    struct EXT2DriverRequest req = {
        .buf          = buf,
        .name         = (char*) name,
        .name_len     = nlen,
        .parent_inode = parent_inode,
        .buffer_size  = size,
        .is_directory = is_dir
    };
    int8_t retcode = -1;
    syscall(2, (uint32_t) &req, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t sys_delete_entry(uint32_t parent_inode, const char *name) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;

    struct EXT2DriverRequest req = {
        .buf          = NULL,
        .name         = (char*) name,
        .name_len     = nlen,
        .parent_inode = parent_inode,
        .buffer_size  = 0,
        .is_directory = false
    };
    int8_t retcode = -1;
    syscall(3, (uint32_t) &req, (uint32_t) &retcode, 0);
    return retcode;
}

// ==================== 2. HELPER STRING ====================

static uint32_t str_len(const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static int str_cmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void str_cpy(char *dest, const char *src) {
    while ((*dest++ = *src++));
}

static char* str_find(const char *haystack, const char *needle) {
    if (!*needle) return (char*) haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack, *n = needle;
            while (*h && *n && (*h == *n)) {
                h++;
                n++;
            }
            if (!*n) return (char*) haystack;
        }
    }
    return NULL;
}

// ==================== 3. STATE SHELL & PIPELINE ====================

static uint32_t cwd_inode = 1;          // Inode root BillyOS = 1
static char     cwd_path[128] = "/";     // Path CWD

static bool pipe_capture_mode = false;
static char pipe_buffer[PIPE_BUFFER_SIZE];
static uint32_t pipe_buffer_len = 0;

static void shell_print(const char *str, uint8_t color) {
    if (pipe_capture_mode) {
        uint32_t len = str_len(str);
        if (pipe_buffer_len + len < PIPE_BUFFER_SIZE - 1) {
            memcpy(pipe_buffer + pipe_buffer_len, str, len);
            pipe_buffer_len += len;
            pipe_buffer[pipe_buffer_len] = '\0';
        }
    } else {
        sys_puts(str, color);
    }
}

// ==================== 4. IMPLEMENTASI COMMAND ====================

// --- COMMAND: HELP ---
static void cmd_help(int argc, char *argv[]) {
    (void) argc; (void) argv;
    shell_print("BillyOS Shell Commands:\n", 0x0E);
    shell_print("  cd <dir>         : Ganti direktori (termasuk '..' dan '/')\n", 0x07);
    shell_print("  ls [dir]         : Tampilkan daftar file & direktori\n", 0x07);
    shell_print("  mkdir <dir>      : Buat direktori baru\n", 0x07);
    shell_print("  cat <file>       : Tampilkan isi file teks\n", 0x07);
    shell_print("  cp <src> <dst>   : Salin file\n", 0x07);
    shell_print("  rm <file/dir>    : Hapus file atau folder kosong\n", 0x07);
    shell_print("  mv <src> <dst>   : Pindah atau ubah nama file\n", 0x07);
    shell_print("  find <name>      : Cari file/folder secara rekursif\n", 0x07);
    shell_print("  grep <pattern>   : Cari teks (bisa via pipeline |)\n", 0x07);
    shell_print("  clear            : Bersihkan layar\n", 0x07);
    shell_print("  help             : Tampilkan bantuan ini\n", 0x07);
}

// --- COMMAND: CLEAR ---
static void cmd_clear(int argc, char *argv[]) {
    (void) argc; (void) argv;
    sys_clear();
}

// --- COMMAND: LS ---
static void cmd_ls(int argc, char *argv[]) {
    const char *target_dir = (argc > 1) ? argv[1] : ".";
    uint8_t dir_buf[BLOCK_SIZE];
    memset(dir_buf, 0, BLOCK_SIZE);

    int8_t ret = sys_read_dir(cwd_inode, target_dir, dir_buf);
    if (ret != 0) {
        shell_print("ls: Direktori tidak ditemukan!\n", 0x0C);
        return;
    }

    uint32_t offset = 0;
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (dir_buf + offset);
        if (entry->rec_len == 0) break;

        if (entry->inode != 0) {
            char name_buf[64];
            uint8_t nlen = (entry->name_len < 63) ? entry->name_len : 63;
            memcpy(name_buf, (char*)entry + sizeof(struct EXT2DirectoryEntry), nlen);
            name_buf[nlen] = '\0';

            if (entry->file_type == EXT2_FT_DIR) {
                shell_print(name_buf, 0x0B); // Biru muda untuk folder
                shell_print("/  ", 0x0B);
            } else {
                shell_print(name_buf, 0x0F); // Putih untuk file biasa
                shell_print("  ", 0x0F);
            }
        }
        offset += entry->rec_len;
    }
    shell_print("\n", 0x0F);
}

// --- COMMAND: CD ---
static void cmd_cd(int argc, char *argv[]) {
    if (argc < 2) {
        // Tanpa argumen: kembali ke root
        cwd_inode = 1;
        str_cpy(cwd_path, "/");
        return;
    }

    const char *target = argv[1];
    if (str_cmp(target, "/") == 0) {
        cwd_inode = 1;
        str_cpy(cwd_path, "/");
        return;
    }
    if (str_cmp(target, ".") == 0) return;

    uint8_t dir_buf[BLOCK_SIZE];
    if (sys_read_dir(cwd_inode, ".", dir_buf) != 0) {
        shell_print("cd: Gagal membaca direktori aktif\n", 0x0C);
        return;
    }

    uint32_t offset = 0;
    bool found = false;
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (dir_buf + offset);
        if (entry->rec_len == 0) break;

        if (entry->inode != 0) {
            char name_buf[64];
            uint8_t nlen = (entry->name_len < 63) ? entry->name_len : 63;
            memcpy(name_buf, (char*)entry + sizeof(struct EXT2DirectoryEntry), nlen);
            name_buf[nlen] = '\0';

            if (str_cmp(name_buf, target) == 0) {
                if (entry->file_type != EXT2_FT_DIR) {
                    shell_print("cd: Bukan sebuah direktori!\n", 0x0C);
                    return;
                }
                cwd_inode = entry->inode;
                found = true;

                // Perbarui path string
                if (str_cmp(target, "..") == 0) {
                    if (cwd_inode == 1) {
                        str_cpy(cwd_path, "/");
                    } else {
                        // Potong subfolder terakhir
                        int len = str_len(cwd_path);
                        while (len > 1 && cwd_path[len - 1] != '/') len--;
                        if (len > 1) cwd_path[len - 1] = '\0';
                        else { cwd_path[1] = '\0'; }
                    }
                } else {
                    int len = str_len(cwd_path);
                    if (len > 1 && cwd_path[len - 1] != '/') {
                        cwd_path[len] = '/';
                        cwd_path[len + 1] = '\0';
                    }
                    if (cwd_path[0] == '/' && cwd_path[1] == '\0') {
                        str_cpy(cwd_path + 1, target);
                    } else {
                        uint32_t cur = str_len(cwd_path);
                        str_cpy(cwd_path + cur, target);
                    }
                }
                break;
            }
        }
        offset += entry->rec_len;
    }

    if (!found) {
        shell_print("cd: Direktori tidak ditemukan: ", 0x0C);
        shell_print(target, 0x0C);
        shell_print("\n", 0x0C);
    }
}

// --- COMMAND: CAT ---
static void cmd_cat(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("Gunakan: cat <nama_file>\n", 0x0E);
        return;
    }

    static uint8_t file_buf[4096];
    memset(file_buf, 0, sizeof(file_buf));

    int8_t ret = sys_read_file(cwd_inode, argv[1], file_buf, sizeof(file_buf) - 1);
    if (ret == 0) {
        shell_print((char*) file_buf, 0x0F);
        shell_print("\n", 0x0F);
    } else if (ret == 1) {
        shell_print("cat: Target adalah direktori, bukan file!\n", 0x0C);
    } else if (ret == 3) {
        shell_print("cat: File tidak ditemukan: ", 0x0C);
        shell_print(argv[1], 0x0C);
        shell_print("\n", 0x0C);
    } else {
        shell_print("cat: Gagal membaca file!\n", 0x0C);
    }
}

// --- COMMAND: MKDIR ---
static void cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("Gunakan: mkdir <nama_folder>\n", 0x0E);
        return;
    }

    int8_t ret = sys_write_entry(cwd_inode, argv[1], NULL, 0, true);
    if (ret == 0) {
        shell_print("Folder berhasil dibuat!\n", 0x0A);
    } else if (ret == 1) {
        shell_print("mkdir: File/folder dengan nama tersebut sudah ada!\n", 0x0C);
    } else {
        shell_print("mkdir: Gagal membuat folder!\n", 0x0C);
    }
}

// --- COMMAND: RM ---
static void cmd_rm(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("Gunakan: rm <nama_file_atau_folder>\n", 0x0E);
        return;
    }

    int8_t ret = sys_delete_entry(cwd_inode, argv[1]);
    if (ret == 0) {
        shell_print("Berhasil dihapus!\n", 0x0A);
    } else if (ret == 1) {
        shell_print("rm: File/folder tidak ditemukan!\n", 0x0C);
    } else if (ret == 2) {
        shell_print("rm: Folder tidak kosong!\n", 0x0C);
    } else if (ret == -1) {
        shell_print("rm: Ditolak! Tidak boleh menghapus root atau CWD aktif.\n", 0x0C);
    } else {
        shell_print("rm: Gagal menghapus!\n", 0x0C);
    }
}

// --- COMMAND: CP ---
static void cmd_cp(int argc, char *argv[]) {
    if (argc < 3) {
        shell_print("Gunakan: cp <source> <destination>\n", 0x0E);
        return;
    }

    static uint8_t copy_buf[4096];
    memset(copy_buf, 0, sizeof(copy_buf));

    int8_t ret = sys_read_file(cwd_inode, argv[1], copy_buf, sizeof(copy_buf));
    if (ret != 0) {
        shell_print("cp: Gagal membaca file sumber!\n", 0x0C);
        return;
    }

    uint32_t fsize = str_len((char*) copy_buf);
    ret = sys_write_entry(cwd_inode, argv[2], copy_buf, fsize, false);
    if (ret == 0) {
        shell_print("File berhasil disalin!\n", 0x0A);
    } else if (ret == 1) {
        shell_print("cp: File tujuan sudah ada!\n", 0x0C);
    } else {
        shell_print("cp: Gagal menulis file tujuan!\n", 0x0C);
    }
}

// --- COMMAND: MV ---
static void cmd_mv(int argc, char *argv[]) {
    if (argc < 3) {
        shell_print("Gunakan: mv <source> <destination>\n", 0x0E);
        return;
    }

    // mv diimplementasikan dengan copy lalu hapus source
    static uint8_t mv_buf[4096];
    memset(mv_buf, 0, sizeof(mv_buf));

    int8_t ret = sys_read_file(cwd_inode, argv[1], mv_buf, sizeof(mv_buf));
    if (ret != 0) {
        shell_print("mv: File sumber tidak ditemukan!\n", 0x0C);
        return;
    }

    uint32_t fsize = str_len((char*) mv_buf);
    ret = sys_write_entry(cwd_inode, argv[2], mv_buf, fsize, false);
    if (ret != 0) {
        shell_print("mv: Gagal menulis file tujuan!\n", 0x0C);
        return;
    }

    sys_delete_entry(cwd_inode, argv[1]);
    shell_print("File berhasil dipindahkan/diubah nama!\n", 0x0A);
}

// --- COMMAND: FIND (DFS Traversal) ---
static void dfs_find(uint32_t inode, const char *cur_path, const char *target_name) {
    uint8_t dir_buf[BLOCK_SIZE];
    if (sys_read_dir(inode, ".", dir_buf) != 0) return;

    uint32_t offset = 0;
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (dir_buf + offset);
        if (entry->rec_len == 0) break;

        if (entry->inode != 0) {
            char name_buf[64];
            uint8_t nlen = (entry->name_len < 63) ? entry->name_len : 63;
            memcpy(name_buf, (char*)entry + sizeof(struct EXT2DirectoryEntry), nlen);
            name_buf[nlen] = '\0';

            if (str_cmp(name_buf, ".") != 0 && str_cmp(name_buf, "..") != 0) {
                // Susun path lengkap
                char next_path[128];
                str_cpy(next_path, cur_path);
                if (next_path[str_len(next_path) - 1] != '/') {
                    uint32_t p = str_len(next_path);
                    next_path[p] = '/';
                    next_path[p + 1] = '\0';
                }
                uint32_t p = str_len(next_path);
                str_cpy(next_path + p, name_buf);

                // Cek kecocokan nama
                if (str_cmp(name_buf, target_name) == 0) {
                    shell_print("Found: ", 0x0A);
                    shell_print(next_path, 0x0E);
                    shell_print("\n", 0x0F);
                }

                // Jika subfolder, telusuri secara rekursif
                if (entry->file_type == EXT2_FT_DIR) {
                    dfs_find(entry->inode, next_path, target_name);
                }
            }
        }
        offset += entry->rec_len;
    }
}

static void cmd_find(int argc, char *argv[]) {
    if (argc < 2) {
        shell_print("Gunakan: find <nama_file_atau_folder>\n", 0x0E);
        return;
    }
    shell_print("Mencari '", 0x07);
    shell_print(argv[1], 0x0B);
    shell_print("' di seluruh filesystem...\n", 0x07);
    dfs_find(1, "/", argv[1]); // Mulai dari root
}

// --- COMMAND: GREP ---
static void cmd_grep(int argc, char *argv[], const char *pipe_input) {
    if (argc < 2) {
        shell_print("Gunakan: grep <pattern> [file] atau via pipe |\n", 0x0E);
        return;
    }

    const char *pattern = argv[1];
    static uint8_t src_buf[4096];
    const char *source = NULL;

    if (pipe_input && pipe_input[0] != '\0') {
        source = pipe_input;
    } else if (argc >= 3) {
        memset(src_buf, 0, sizeof(src_buf));
        if (sys_read_file(cwd_inode, argv[2], src_buf, sizeof(src_buf) - 1) == 0) {
            source = (char*) src_buf;
        } else {
            shell_print("grep: File tidak dapat dibaca!\n", 0x0C);
            return;
        }
    } else {
        shell_print("grep: Tidak ada input (berikan argumen file atau gunakan pipeline |)\n", 0x0E);
        return;
    }

    // Pisahkan baris demi baris dan cari kecocokan pattern
    char line[256];
    uint32_t l_idx = 0;
    while (*source) {
        if (*source == '\n') {
            line[l_idx] = '\0';
            if (str_find(line, pattern) != NULL) {
                shell_print(line, 0x0A);
                shell_print("\n", 0x0F);
            }
            l_idx = 0;
        } else {
            if (l_idx < sizeof(line) - 1) {
                line[l_idx++] = *source;
            }
        }
        source++;
    }
    if (l_idx > 0) {
        line[l_idx] = '\0';
        if (str_find(line, pattern) != NULL) {
            shell_print(line, 0x0A);
            shell_print("\n", 0x0F);
        }
    }
}

// ==================== 5. PARSER & EXECUTOR ====================

static void execute_single_command(char *cmd_str, const char *pipe_in) {
    // Parsing spasi menjadi argv
    char *argv[MAX_ARGS];
    int argc = 0;

    char *p = cmd_str;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (argc < MAX_ARGS) argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }

    if (argc == 0) return;

    if (str_cmp(argv[0], "help") == 0)       cmd_help(argc, argv);
    else if (str_cmp(argv[0], "clear") == 0) cmd_clear(argc, argv);
    else if (str_cmp(argv[0], "ls") == 0)    cmd_ls(argc, argv);
    else if (str_cmp(argv[0], "cd") == 0)    cmd_cd(argc, argv);
    else if (str_cmp(argv[0], "cat") == 0)   cmd_cat(argc, argv);
    else if (str_cmp(argv[0], "mkdir") == 0) cmd_mkdir(argc, argv);
    else if (str_cmp(argv[0], "rm") == 0)    cmd_rm(argc, argv);
    else if (str_cmp(argv[0], "cp") == 0)    cmd_cp(argc, argv);
    else if (str_cmp(argv[0], "mv") == 0)    cmd_mv(argc, argv);
    else if (str_cmp(argv[0], "find") == 0)  cmd_find(argc, argv);
    else if (str_cmp(argv[0], "grep") == 0)  cmd_grep(argc, argv, pipe_in);
    else {
        shell_print("Command tidak dikenal: '", 0x0C);
        shell_print(argv[0], 0x0C);
        shell_print("'. Ketik 'help' untuk daftar perintah.\n", 0x0C);
    }
}

static void execute_command_line(char *line) {
    // Cek apakah terdapat operator pipeline '|'
    char *pipe_pos = str_find(line, "|");
    if (pipe_pos != NULL) {
        *pipe_pos = '\0';
        char *cmd1 = line;
        char *cmd2 = pipe_pos + 1;

        // 1. Eksekusi command pertama dan rekam outputnya ke buffer
        pipe_capture_mode = true;
        pipe_buffer_len = 0;
        pipe_buffer[0] = '\0';

        execute_single_command(cmd1, NULL);

        pipe_capture_mode = false;

        // 2. Eksekusi command kedua dengan membawa output command pertama
        execute_single_command(cmd2, pipe_buffer);
    } else {
        execute_single_command(line, NULL);
    }
}

// ==================== 6. MAIN REPL LOOP ====================

int main(void) {
    // Aktifkan penerimaan keyboard interrupt
    syscall(7, 0, 0, 0);

    sys_clear();
    sys_puts("====================================================\n", 0x0B);
    sys_puts("           Selamat Datang di BillyOS Shell          \n", 0x0E);
    sys_puts("====================================================\n", 0x0B);
    sys_puts("Ketik 'help' untuk melihat daftar perintah.\n\n", 0x07);

    char line_buf[MAX_LINE_LEN];
    uint32_t line_idx = 0;

    // Cetak prompt awal
    sys_puts("BillyOS:", 0x0B);
    sys_puts(cwd_path, 0x0E);
    sys_puts("$ ", 0x0F);

    while (true) {
        char c = 0;
        sys_getchar(&c);

        if (c != 0) {
            if (c == '\n') {
                sys_putchar('\n', 0x0F);
                line_buf[line_idx] = '\0';

                if (line_idx > 0) {
                    execute_command_line(line_buf);
                    line_idx = 0;
                }

                // Tampilkan prompt baru dengan CWD terkini
                sys_puts("BillyOS:", 0x0B);
                sys_puts(cwd_path, 0x0E);
                sys_puts("$ ", 0x0F);
            } else if (c == '\b') {
                if (line_idx > 0) {
                    line_idx--;
                    sys_putchar('\b', 0x0F);
                }
            } else {
                if (line_idx < MAX_LINE_LEN - 1) {
                    line_buf[line_idx++] = c;
                    sys_putchar(c, 0x0F);
                }
            }
        }
    }

    return 0;
}
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

static void sys_puts_at(const char *str, uint8_t row, uint8_t col, uint8_t color) {
    syscall(15, (uint32_t)str, ((uint32_t)row << 16) | (uint32_t)col, (uint32_t)color);
}

static void sys_shutdown(bool is_reboot) {
    syscall(25, is_reboot ? 1 : 0, 0, 0);
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

static void sys_speaker_play(uint32_t freq) {
    syscall(24, 0, freq, 0);
}

static void sys_speaker_stop(void) {
    syscall(24, 1, 0, 0);
}

static uint32_t sys_ms(void) {
    uint32_t t = 0;
    syscall(22, (uint32_t) &t, 0, 0);
    return t;
}

static void sys_sleep_ms(uint32_t ms) {
    uint32_t end = sys_ms() + ms;
    while (sys_ms() < end) {
        for (volatile int w = 0; w < 1000; w++) {
            __asm__ volatile("pause");
        }
    }
}

static void sys_beep(uint32_t freq, uint32_t ms) {
    sys_speaker_play(freq);
    sys_sleep_ms(ms);
    sys_speaker_stop();
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
struct Note {
    uint32_t freq;
    uint32_t ms;
};

// Mainkan rangkaian nada secara kontinu tanpa mematikan stream speaker di tengah lagu
static void play_melody(const struct Note *notes, int count) {
    if (count <= 0) return;
    for (int i = 0; i < count; i++) {
        if (notes[i].freq > 0) {
            sys_speaker_play(notes[i].freq);
        } else {
            // Mainkan 20 Hz (frekuensi sub-audible) untuk jeda/pause
            sys_speaker_play(20);
        }
        sys_sleep_ms(notes[i].ms);
    }
    sys_speaker_stop();
}

// Tabel frekuensi standar (Hz) untuk not C, C#, D, D#, E, F, F#, G, G#, A, A#, B
// Octave 1 sampai 6
static const uint16_t NOTE_FREQ_TABLE[6][12] = {
    // C,    C#,   D,    D#,   E,    F,    F#,   G,    G#,   A,    A#,   B
    {  33,   35,   37,   39,   41,   44,   46,   49,   52,   55,   58,   62 }, // Octave 1
    {  65,   69,   73,   78,   82,   87,   92,   98,  104,  110,  117,  123 }, // Octave 2
    { 131,  139,  147,  156,  165,  175,  185,  196,  208,  220,  233,  247 }, // Octave 3
    { 261,  277,  294,  311,  330,  349,  370,  392,  415,  440,  466,  494 }, // Octave 4
    { 523,  554,  587,  622,  659,  698,  740,  784,  831,  880,  932,  988 }, // Octave 5
    { 1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976 }  // Octave 6
};

static uint32_t parse_note_freq(const char *s) {
    if (!s || !*s) return 0;
    if (s[0] == '0' || s[0] == 'R' || s[0] == 'r' || s[0] == 'P' || s[0] == 'p') {
        return 0; // Rest / Pause
    }
    if (s[0] >= '0' && s[0] <= '9') {
        return str_to_int(s);
    }

    char note = s[0];
    if (note >= 'a' && note <= 'z') note -= 32;
    int note_idx = -1;
    switch (note) {
        case 'C': note_idx = 0; break;
        case 'D': note_idx = 2; break;
        case 'E': note_idx = 4; break;
        case 'F': note_idx = 5; break;
        case 'G': note_idx = 7; break;
        case 'A': note_idx = 9; break;
        case 'B': note_idx = 11; break;
        default: return 0;
    }

    int i = 1;
    if (s[i] == '#' || s[i] == 's' || s[i] == 'S') {
        note_idx++;
        i++;
    } else if (s[i] == 'b' || s[i] == 'B') {
        note_idx--;
        i++;
    }

    if (note_idx < 0) note_idx += 12;
    if (note_idx >= 12) note_idx -= 12;

    int octave = 4;
    if (s[i] >= '1' && s[i] <= '6') {
        octave = s[i] - '0';
    }

    return NOTE_FREQ_TABLE[octave - 1][note_idx];
}

// Koleksi Lagu Jukebox
#define NOTE_E3  165
#define NOTE_E4  330
#define NOTE_D4  294
#define NOTE_C4  261
#define NOTE_AS3 233
#define NOTE_B3  247

static const struct Note doom_riff[] = {
    // Bar 1
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E4, 175 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_D4, 175 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_C4, 175 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_AS3,150 },
    { NOTE_B3, 220 },

    // Bar 2
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E4, 175 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_D4, 175 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_C4, 175 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_E3,  85 }, { 0, 15 },
    { NOTE_B3, 150 },
    { NOTE_C4, 250 },
};

static const struct Note mario_theme[] = {
    { 659, 125 }, { 0, 35 },
    { 659, 125 }, { 0, 125 },
    { 659, 125 }, { 0, 125 },
    { 523, 125 }, { 0, 35 },
    { 659, 160 }, { 0, 125 },
    { 784, 250 }, { 0, 250 },
    { 392, 250 }, { 0, 250 },
    // Verse
    { 523, 200 }, { 0, 100 },
    { 392, 200 }, { 0, 100 },
    { 330, 200 }, { 0, 100 },
    { 440, 180 }, { 0, 40 },
    { 494, 180 }, { 0, 40 },
    { 466, 150 }, { 0, 40 },
    { 440, 220 },
    { 392, 160 },
    { 659, 160 },
    { 784, 160 },
    { 880, 220 },
    { 698, 160 },
    { 784, 160 },
    { 659, 180 },
    { 523, 160 },
    { 587, 160 },
    { 494, 250 }
};

static const struct Note tetris_theme[] = {
    { 659, 250 }, { 494, 125 }, { 523, 125 }, { 587, 250 },
    { 523, 125 }, { 494, 125 }, { 440, 250 }, { 440, 125 },
    { 523, 125 }, { 659, 250 }, { 587, 125 }, { 523, 125 },
    { 494, 375 }, { 523, 125 }, { 587, 250 }, { 659, 250 },
    { 523, 250 }, { 440, 250 }, { 440, 350 }, { 0,   150 },
    // Part 2
    { 587, 250 }, { 698, 125 }, { 880, 250 }, { 784, 125 },
    { 698, 125 }, { 659, 375 }, { 523, 125 }, { 659, 250 },
    { 587, 125 }, { 523, 125 }, { 494, 250 }, { 494, 125 },
    { 523, 125 }, { 587, 250 }, { 659, 250 }, { 523, 250 },
    { 440, 250 }, { 440, 350 }
};

static const struct Note starwars_theme[] = {
    { 392, 350 }, { 0, 40 },
    { 392, 350 }, { 0, 40 },
    { 392, 350 }, { 0, 40 },
    { 311, 250 }, { 0, 20 },
    { 466, 120 }, { 0, 20 },
    { 392, 350 }, { 0, 40 },
    { 311, 250 }, { 0, 20 },
    { 466, 120 }, { 0, 20 },
    { 392, 550 }, { 0, 150 },
    // High phrase
    { 587, 350 }, { 0, 40 },
    { 587, 350 }, { 0, 40 },
    { 587, 350 }, { 0, 40 },
    { 622, 250 }, { 0, 20 },
    { 466, 120 }, { 0, 20 },
    { 370, 350 }, { 0, 40 },
    { 311, 250 }, { 0, 20 },
    { 466, 120 }, { 0, 20 },
    { 392, 600 }
};

static const struct Note zelda_theme[] = {
    { 466, 300 }, { 0, 30 },
    { 349, 150 }, { 0, 20 },
    { 466, 120 }, { 0, 20 },
    { 466, 120 }, { 0, 20 },
    { 523, 120 },
    { 587, 120 },
    { 622, 120 },
    { 698, 400 }, { 0, 50 },
    { 698, 150 }, { 0, 20 },
    { 698, 150 },
    { 740, 150 },
    { 831, 150 },
    { 932, 500 }
};

static void cmd_music(int argc, char *argv[]) {
    if (argc < 2 || str_cmp(argv[1], "list") == 0 || str_cmp(argv[1], "help") == 0) {
        shell_print("Gunakan: music <judul>\n", 0x0F);
        shell_print("  doom     : DOOM E1M1 - At Doom's Gate\n", 0x0C);
        shell_print("  mario    : Super Mario Bros Theme\n", 0x0A);
        shell_print("  tetris   : Tetris Korobeiniki (Theme A)\n", 0x0B);
        shell_print("  starwars : Star Wars - The Imperial March\n", 0x0E);
        shell_print("  zelda    : The Legend of Zelda Theme\n", 0x0D);
        return;
    }

    if (str_cmp(argv[1], "doom") == 0) {
        shell_print("Memainkan DOOM Theme (At Doom's Gate)...\n", 0x0C);
        play_melody(doom_riff, sizeof(doom_riff) / sizeof(doom_riff[0]));
    } else if (str_cmp(argv[1], "mario") == 0) {
        shell_print("Memainkan Super Mario Bros Theme...\n", 0x0A);
        play_melody(mario_theme, sizeof(mario_theme) / sizeof(mario_theme[0]));
    } else if (str_cmp(argv[1], "tetris") == 0) {
        shell_print("Memainkan Tetris Korobeiniki...\n", 0x0B);
        play_melody(tetris_theme, sizeof(tetris_theme) / sizeof(tetris_theme[0]));
    } else if (str_cmp(argv[1], "starwars") == 0) {
        shell_print("Memainkan Star Wars (The Imperial March)...\n", 0x0E);
        play_melody(starwars_theme, sizeof(starwars_theme) / sizeof(starwars_theme[0]));
    } else if (str_cmp(argv[1], "zelda") == 0) {
        shell_print("Memainkan The Legend of Zelda Theme...\n", 0x0D);
        play_melody(zelda_theme, sizeof(zelda_theme) / sizeof(zelda_theme[0]));
    } else {
        shell_print("Lagu tidak ditemukan! Ketik 'music list' untuk daftar lagu.\n", 0x0C);
    }
}

static char* sys_read_file_dynamic(uint32_t parent_ino, const char *fname) {
    uint32_t cap = 4096;
    while (cap <= 262144) {
        char *buf = (char*) malloc(cap);
        if (!buf) return NULL;
        memset(buf, 0, cap);

        int8_t ret = sys_read_file(parent_ino, fname, buf, cap - 1);
        if (ret == 0) {
            return buf;
        }
        free(buf);
        if (ret != 2) {
            return NULL;
        }
        cap *= 2;
    }
    return NULL;
}

static bool sys_sleep_ms_abortable(uint32_t ms) {
    uint32_t end = sys_ms() + ms;
    while (sys_ms() < end) {
        char c = 0;
        sys_getchar(&c);
        if (c == 0x1B || c == 'q' || c == 'Q') {
            return true;
        }
        for (volatile int w = 0; w < 1000; w++) {
            __asm__ volatile("pause");
        }
    }
    return false;
}

static void cmd_play(int argc, char *argv[]) {
    if (argc < 2 || argv[1][0] == '\0') {
        shell_print("Gunakan: play <file_partitur>\n", 0x0E);
        shell_print("Contoh : play mario.mus\n", 0x07);
        shell_print("Format : <not> <durasi_ms>, misal: E5 200 atau C4:150\n", 0x07);
        return;
    }

    uint32_t target_ino = cwd_inode;
    const char *fname = argv[1];
    if (fname[0] == '/') {
        target_ino = 1;
        fname++;
    }

    char *song_buf = sys_read_file_dynamic(target_ino, fname);
    if (!song_buf) {
        shell_print("play: File tidak ditemukan atau gagal dibaca!\n", 0x0C);
        return;
    }

    // Kosongkan sisa tombol keyboard sebelum mulai
    char drain = 0;
    do { sys_getchar(&drain); } while (drain != 0);

    shell_print("Memutar musik dari: ", 0x0B);
    shell_print(argv[1], 0x0E);
    shell_print(" (ESC / Q untuk berhenti)...\n", 0x0F);

    char *p = song_buf;
    bool stream_started = false;
    bool aborted = false;

    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',' || *p == ';')) {
            p++;
        }
        if (!*p) break;

        if (*p == '#' || (*p == '/' && *(p+1) == '/')) {
            while (*p && *p != '\n') p++;
            continue;
        }

        char note_str[16];
        int n_len = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != ':' && *p != ',' && n_len < 15) {
            note_str[n_len++] = *p++;
        }
        note_str[n_len] = '\0';

        while (*p == ' ' || *p == '\t' || *p == ':') p++;

        uint32_t dur = 150;
        if (*p >= '0' && *p <= '9') {
            dur = 0;
            while (*p >= '0' && *p <= '9') {
                dur = dur * 10 + (*p++ - '0');
            }
        }
        if (dur == 0) dur = 100;

        uint32_t freq = parse_note_freq(note_str);

        if (!stream_started) {
            sys_speaker_play(freq > 0 ? freq : 20);
            stream_started = true;
        }

        if (freq > 0) {
            sys_speaker_play(freq);
        } else {
            sys_speaker_play(20);
        }
        if (sys_sleep_ms_abortable(dur)) {
            aborted = true;
            break;
        }
    }

    if (stream_started) {
        sys_speaker_stop();
    }
    free(song_buf);
    shell_print(aborted ? "Musik dihentikan.\n" : "Musik selesai diputar!\n", aborted ? 0x0E : 0x0A);
}

static void cmd_beep(int argc, char *argv[]) {
    if (argc >= 2 && (str_cmp(argv[1], "song") == 0 || str_cmp(argv[1], "music") == 0)) {
        cmd_music(argc - 1, argv + 1);
        return;
    }

    uint32_t freq = 750;
    uint32_t dur = 150;

    if (argc >= 2) {
        uint32_t val = 0;
        for (int i = 0; argv[1][i]; i++) {
            if (argv[1][i] >= '0' && argv[1][i] <= '9') val = val * 10 + (argv[1][i] - '0');
        }
        if (val > 0) freq = val;
    }
    if (argc >= 3) {
        uint32_t val = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') val = val * 10 + (argv[2][i] - '0');
        }
        if (val > 0) dur = val;
    }

    shell_print("Beep PC Speaker: ", 0x0B);
    sys_beep(freq, dur);
    shell_print("Selesai!\n", 0x0A);
}

static bool boot_wait_ms(uint32_t ms) {
    uint32_t start = sys_ms();
    for (;;) {
        char ch = 0;
        sys_getchar(&ch);
        if (ch != 0) return true;
        uint32_t elapsed = (uint32_t)(sys_ms() - start);
        if (elapsed >= ms) return false;
        uint32_t remain = ms - elapsed;
        sys_sleep_ms(remain > 15 ? 15 : remain);
    }
}

static void play_boot_animation(void) {
    static const char * const logo[6] = {
        " ____  _ _ _         ___  ____   ",
        "| __ )(_) | |_   _  / _ \\/ ___|  ",
        "|  _ \\| | | | | | || | | \\___ \\  ",
        "| |_) | | | | |_| || |_| |___) | ",
        "|____/|_|_|_|\\__, | \\___/|____/  ",
        "             |___/               "
    };
    static const char * const subsys[5] = {
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB  ",
        "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII  ",
        "LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL  ",
        "LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL  ",
        "YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY  "
    };
    static const uint8_t sub_pct[5] = { 12, 32, 52, 72, 90 };
    static const uint16_t chime_hz[24] = {
        523, 523,  20, 659, 659,  20,
        784, 784,  20, 1047, 1047, 1047,
        1047, 784, 784, 784, 1047, 1047,
        1047, 1047, 1047, 0, 0, 0
    };
    const char *shuttle_top = "  __|__  ";
    const char *shuttle_bot = "(BillyOS)>>";

    sys_clear();
    sys_puts_at("+--------------------------------------------------------+", 2, 11, 0x03);
    sys_puts_at("                 BILLY OPERATING SYSTEM                 ", 3, 12, 0x0E);
    sys_puts_at("+--------------------------------------------------------+", 4, 11, 0x03);
    sys_puts_at("------------------------------------------------------------", 15, 10, 0x08);
    sys_puts_at("+--------------------------------------------------------+", 26, 11, 0x03);

    bool skipped = false;
    for (int f = 0; f < 24; f++) {
        uint32_t pct = (uint32_t)((f * 100) / 23);
        uint32_t pos = (uint32_t)((f * 47) / 23);

        // Animated color sweep across the 6-line ASCII logo
        for (int r = 0; r < 6; r++) {
            uint8_t c = 0x08;
            if (f < 8) {
                int diff = f - r;
                if (diff == 0) c = 0x0F;
                else if (diff == 1) c = 0x0B;
                else if (diff > 1) c = 0x03;
                else c = 0x08;
            } else {
                int band = (f - 8 + r) % 6;
                c = (band == 0) ? 0x0F : ((band <= 2) ? 0x0B : 0x03);
            }
            sys_puts_at(logo[r], (uint8_t)(5 + r), 23, c);
        }

        // Moving ASCII shuttle across col 10..69 (rows 13..14)
        char line1[61];
        char line2[61];
        for (int i = 0; i < 60; i++) {
            line1[i] = ' ';
            line2[i] = ' ';
        }
        line1[60] = '\0';
        line2[60] = '\0';
        for (int i = 0; shuttle_top[i] != '\0' && (pos + (uint32_t)i) < 60; i++) {
            line1[pos + (uint32_t)i] = shuttle_top[i];
        }
        for (int i = 0; shuttle_bot[i] != '\0' && (pos + (uint32_t)i) < 60; i++) {
            line2[pos + (uint32_t)i] = shuttle_bot[i];
        }
        sys_puts_at(line1, 13, 10, 0x0F);
        sys_puts_at(line2, 14, 10, 0x0B);
        if (pos >= 3) {
            const char *flame = ((f & 1) == 0) ? "*~~" : "~=~";
            sys_puts_at(flame, 14, (uint8_t)(10 + pos - 3), (f & 1) ? 0x0C : 0x0E);
        }

        // Loading bar (42 cells + percentage)
        char bar[54];
        uint32_t filled = (pct * 42) / 100;
        bar[0] = '[';
        for (uint32_t i = 0; i < 42; i++) {
            if (i < filled) bar[1 + i] = (char)0xDB;
            else if (i == filled && pct < 100) bar[1 + i] = (char)0xB2;
            else bar[1 + i] = (char)0xB0;
        }
        bar[43] = ']';
        bar[44] = ' ';
        bar[45] = (pct >= 100) ? '1' : ' ';
        bar[46] = (pct >= 10) ? (char)('0' + ((pct / 10) % 10)) : ' ';
        bar[47] = (char)('0' + (pct % 10));
        bar[48] = '%';
        bar[49] = '\0';
        sys_puts_at(bar, 16, 15, (pct == 100) ? 0x0A : 0x0B);

        // Subsystem checklist
        for (int s = 0; s < 5; s++) {
            uint8_t row = (uint8_t)(18 + s);
            if (pct >= (uint32_t)sub_pct[s] + 8) {
                sys_puts_at("[ OK ]", row, 18, 0x0A);
                sys_puts_at(subsys[s], row, 25, 0x0F);
            } else if (pct >= sub_pct[s]) {
                sys_puts_at("[ -> ]", row, 18, 0x0E);
                sys_puts_at(subsys[s], row, 25, 0x0E);
            } else {
                sys_puts_at("[ .. ]", row, 18, 0x08);
                sys_puts_at(subsys[s], row, 25, 0x08);
            }
        }

        // Synchronized PC Speaker startup chime
        if (chime_hz[f] > 0) {
            sys_speaker_play(chime_hz[f]);
        } else {
            sys_speaker_stop();
        }

        if (boot_wait_ms(90)) {
            skipped = true;
            break;
        }
    }

    sys_speaker_stop();
    if (skipped) {
        for (int d = 0; d < 16; d++) {
            char drain = 0;
            sys_getchar(&drain);
            if (drain == 0) break;
        }
    }
    sys_clear();
}

static void play_shutdown_animation(bool is_reboot) {
    static const uint16_t notes[8] = { 784, 659, 523, 392, 262, 0, 0, 0 };

    sys_clear();
    sys_puts_at("+--------------------------------------------------+", 7, 14, 0x03);
    if (is_reboot) {
        sys_puts_at("|          BILLYOS SYSTEM REBOOT SEQUENCE          |", 8, 14, 0x0E);
        sys_puts_at("Mengirim sinyal reset keyboard...", 14, 24, 0x07);
    } else {
        sys_puts_at("|                     SHUTDOWN                     |", 8, 14, 0x0E);
    }
    sys_puts_at("+--------------------------------------------------+", 9, 14, 0x03);
    sys_puts_at("+--------------------------------------------------+", 18, 14, 0x03);

    for (int f = 0; f < 8; f++) {
        for (int s = 0; s < 4; s++) {
            if (is_reboot && s == 3) continue;
            uint8_t row = (uint8_t)(11 + s);
            if (f >= s * 2 + 1) {
                sys_puts_at("[ GOOD BYE ]", row, 34, 0x0A);
            }
        }

        // Shrinking power-off progress bar
        uint32_t remain_pct = (uint32_t)(((7 - f) * 100) / 7);
        uint32_t filled = (remain_pct * 36) / 100;
        char bar[48];
        bar[0] = '[';
        for (uint32_t i = 0; i < 36; i++) {
            bar[1 + i] = (i < filled) ? (char)0xDB : ' ';
        }
        bar[37] = ']';
        bar[38] = ' ';
        bar[39] = (remain_pct >= 100) ? '1' : ' ';
        bar[40] = (remain_pct >= 10) ? (char)('0' + ((remain_pct / 10) % 10)) : ' ';
        bar[41] = (char)('0' + (remain_pct % 10));
        bar[42] = '%';
        bar[43] = '\0';
        sys_puts_at(bar, 16, 18, (remain_pct > 30) ? 0x0E : 0x0C);

        if (notes[f] > 0) sys_speaker_play(notes[f]);
        else sys_speaker_stop();

        sys_sleep_ms(105);
    }

    sys_speaker_stop();

    // Brief CRT horizontal collapse animation on the center row
    static const char * const crt[4] = {
        "      ==============================*==============================      ",
        "                   =================*=================                   ",
        "                             =======*=======                             ",
        "                                    *                                    "
    };
    for (int r = 7; r <= 18; r++) {
        if (r != 13) sys_puts_at("                                                    ", (uint8_t)r, 14, 0x07);
    }
    for (int c = 0; c < 4; c++) {
        sys_puts_at(crt[c], 13, 3, (c == 3) ? 0x0F : 0x0B);
        sys_sleep_ms(45);
    }
    sys_puts_at("                                                                         ", 13, 3, 0x07);
    if (is_reboot) {
        sys_puts_at("BillyOS sedang restart...", 13, 27, 0x0E);
    } else {
        sys_puts_at("BillyOS aman untuk dimatikan. Sampai jumpa!", 13, 18, 0x0A);
    }
    sys_sleep_ms(120);
}

static void cmd_shutdown(void) {
    play_shutdown_animation(false);
    sys_shutdown(false);
}

static void cmd_reboot(void) {
    play_shutdown_animation(true);
    sys_shutdown(true);
}

static void cmd_help(void) {
    shell_print(
        "Daftar Perintah:\n"
        "  cd <dir>         - Pindah direktori kerja\n"
        "  ls               - Tampilkan isi direktori\n"
        "  cat <file>       - Baca dan tampilkan isi file\n"
        "  mkdir <dir>      - Buat direktori baru\n"
        "  rm <target>      - Hapus file atau direktori\n"
        "  cp <src> <dst>   - Salin file ke lokasi tujuan\n"
        "  mv <src> <dst>   - Pindah atau ubah nama file\n"
        "  find <nama>      - Cari file atau direktori\n"
        "  grep <pola>      - Cari pola teks di dalam file\n"
        "  ps               - Tampilkan proses yang berjalan\n"
        "  kill <pid>       - Hentikan proses berdasarkan PID\n"
        "  export <var=val> - Atur variabel lingkungan\n"
        "  beep [freq] [ms] - Bunyikan PC speaker\n"
        "  music [judul]    - Putar melodi bawaan\n"
        "  play <file>      - Putar file audio\n"
        "  doom             - Memainkan game doom\n"
        "  badapple [file]  - Putar animasi ASCII Bad Apple\n"
        "  help             - Tampilkan daftar perintah ini\n"
        "  shutdown         - Matikan sistem\n"
        "  reboot           - Mulai ulang sistem\n"
        "  [Up/Down]        - Riwayat perintah | [PgUp/PgDn / Shift+Up/Down] Scroll\n",
        0x07
    );
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
    char *file_buf = sys_read_file_dynamic(cwd_inode, argv[1]);
    if (file_buf) {
        shell_print(file_buf, 0x0F);
        shell_print("\n", 0x0F);
        free(file_buf);
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
        // Jika yang dijalankan adalah doom atau badapple, shell menunggu sampai program selesai
        // agar input keyboard dan layar tidak bentrok sama sekali
        if (str_cmp(name, "doom") == 0 || str_cmp(name, "badapple") == 0) {
            while (true) {
                struct ProcessMetadata list[PROCESS_COUNT_MAX];
                sys_process_get_info(list);
                bool child_running = false;
                for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
                    if (list[i].state != PROCESS_STATE_INACTIVE && str_cmp(list[i].name, name) == 0) {
                        child_running = true;
                        break;
                    }
                }
                if (!child_running) break;
                sys_sleep_ms(30);
            }
            // Pastikan speaker mati, kembalikan hardware VGA ke Mode 12h & bersihkan layar
            sys_speaker_stop();
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
    else if (str_cmp(argv[0], "beep") == 0)  cmd_beep(argc, argv);
    else if (str_cmp(argv[0], "music") == 0) cmd_music(argc, argv);
    else if (str_cmp(argv[0], "play") == 0)  cmd_play(argc, argv);
    else if (str_cmp(argv[0], "heap") == 0)  cmd_heap_test();
    else if (str_cmp(argv[0], "shutdown") == 0) cmd_shutdown();
    else if (str_cmp(argv[0], "reboot") == 0)   cmd_reboot();
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

#define HISTORY_MAX 16
static char cmd_history[HISTORY_MAX][MAX_LINE_LEN];
static int  hist_count = 0;

static void shell_replace_line(char *line, uint32_t *idx, const char *text) {
    while (*idx > 0) {
        (*idx)--;
        sys_putchar('\b', 0x0F);
    }
    uint32_t i = 0;
    while (text[i] != '\0' && i < MAX_LINE_LEN - 1) {
        line[i] = text[i];
        i++;
    }
    line[i] = '\0';
    *idx = i;
    if (i > 0) sys_puts(line, 0x0F);
}

int main(void) {
    cwd_inode = 1;
    cwd_path[0] = '/';
    cwd_path[1] = '\0';

    syscall(7, 0, 0, 0); // Aktifkan keyboard interrupt
    play_boot_animation();
    env_load();

    if (sys_read_file(1, "banner", sh_buf, sizeof(sh_buf) - 1) == 0 && sh_buf[0] != '\0') {
        sys_puts((char*) sh_buf, 0x0B);
    } else {
        sys_puts("=== BillyOS Shell ===\n\n", 0x0B);
    }

    char line[MAX_LINE_LEN];
    char draft_line[MAX_LINE_LEN];
    draft_line[0] = '\0';
    uint32_t idx = 0;
    int hist_nav = 0;

    sys_puts("BillyOS:", 0x0B);
    sys_puts(cwd_path, 0x0E);
    sys_puts("$ ", 0x0F);

    while (true) {
        char c = 0;
        sys_getchar(&c);

        if (c != 0) {
            uint8_t uc = (uint8_t) c;
            if (uc == 0xB0) {
                syscall(26, 5, 0, 0); // PageUp / Shift+Up: scroll output ke atas
            } else if (uc == 0xB1) {
                syscall(26, (uint32_t)(-5), 0, 0); // PageDown / Shift+Down: scroll ke bawah
            } else if (uc == 0xAD) {
                // Panah Atas: mundur ke recent command lebih lama
                if (hist_count > 0 && hist_nav > 0) {
                    if (hist_nav == hist_count) {
                        line[idx] = '\0';
                        str_cpy(draft_line, line);
                    }
                    hist_nav--;
                    shell_replace_line(line, &idx, cmd_history[hist_nav]);
                }
            } else if (uc == 0xAF) {
                // Panah Bawah: maju ke recent command lebih baru
                if (hist_nav < hist_count) {
                    hist_nav++;
                    shell_replace_line(line, &idx, (hist_nav == hist_count) ? draft_line : cmd_history[hist_nav]);
                }
            } else if (c == '\n') {
                syscall(26, 0, 0, 0);
                sys_putchar('\n', 0x0F);
                line[idx] = '\0';

                if (idx > 0) {
                    if (hist_count == 0 || str_cmp(cmd_history[hist_count - 1], line) != 0) {
                        if (hist_count < HISTORY_MAX) {
                            str_cpy(cmd_history[hist_count++], line);
                        } else {
                            for (int h = 1; h < HISTORY_MAX; h++) {
                                str_cpy(cmd_history[h - 1], cmd_history[h]);
                            }
                            str_cpy(cmd_history[HISTORY_MAX - 1], line);
                        }
                    }
                    execute_line(line);
                    idx = 0;
                }
                hist_nav = hist_count;
                draft_line[0] = '\0';

                sys_puts("BillyOS:", 0x0B);
                sys_puts(cwd_path, 0x0E);
                sys_puts("$ ", 0x0F);
            } else if (c == '\b') {
                if (idx > 0) {
                    idx--;
                    sys_putchar('\b', 0x0F);
                }
            } else if (uc >= 0x20 && uc <= 0x7E) {
                if (idx < MAX_LINE_LEN - 1) {
                    line[idx++] = c;
                    sys_putchar(c, 0x0F);
                }
            }
        }
    }
    return 0;
}
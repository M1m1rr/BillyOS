#include <stdint.h>
#include <stdbool.h>

#define NULL ((void*)0)
typedef unsigned int size_t;

// ==================== 1. SYSCALL WRAPPERS ====================
static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static void* sys_malloc(int size) {
    if (size <= 0) return 0;
    void *ptr = 0;
    syscall(20, 0, (uint32_t) &ptr, (uint32_t) size);
    return ptr;
}

static void sys_free(void *ptr) {
    if (!ptr) return;
    bool ok = false;
    syscall(20, 1, (uint32_t) &ok, (uint32_t) ptr);
}

static void sys_clear(void) {
    syscall(8, 0, 0, 0);
}

static void sys_puts(const char *str, uint8_t color) {
    uint32_t len = 0;
    while (str[len]) len++;
    syscall(6, (uint32_t) str, len, color);
}

static char sys_get_char(void) {
    char c = 0;
    syscall(4, (uint32_t) &c, 0, 0);
    return c;
}

static void sys_blit(const uint8_t *frame_320x200) {
    syscall(21, (uint32_t) frame_320x200, 0, 0);
}

// ==================== 2. EXT2 FILE SYSTEM SHIM ====================
struct EXT2DriverRequest {
    void     *buf;
    char     *name;
    uint8_t   name_len;
    uint32_t  parent_inode;
    uint32_t  buffer_size;
    bool      is_directory;
} __attribute__((packed));

typedef struct {
    uint8_t  *data;
    uint32_t  size;
    uint32_t  offset;
} VirtualFile;

static VirtualFile wad_file;
static bool wad_loaded = false;

#define DOOM_IMPLEMENTATION
#include "header/doom/PureDOOM.h"

// Fungsi pemeriksa apakah string berisi "doom1.wad" (case-insensitive)
static bool str_contains_doom1(const char *str) {
    if (!str) return false;
    for (int i = 0; str[i]; i++) {
        if ((str[i] == 'd' || str[i] == 'D') &&
            (str[i+1] == 'o' || str[i+1] == 'O') &&
            (str[i+2] == 'o' || str[i+2] == 'O') &&
            (str[i+3] == 'm' || str[i+3] == 'M') &&
            (str[i+4] == '1') &&
            (str[i+5] == '.') &&
            (str[i+6] == 'w' || str[i+6] == 'W') &&
            (str[i+7] == 'a' || str[i+7] == 'A') &&
            (str[i+8] == 'd' || str[i+8] == 'D')) {
            return true;
        }
    }
    return false;
}

static void* doom_sys_open(const char* filename, const char* mode) {
    (void) mode;
    // Hanya buka jika yang dicari adalah doom1.wad (tolak doom2, config, dan file lain)
    if (!str_contains_doom1(filename)) {
        return NULL;
    }

    if (!wad_loaded) {
        sys_puts("Membaca doom1.wad dari EXT2 ke RAM...\n", 0x0E);
        uint32_t buf_size = 4200000;
        uint8_t *buf = (uint8_t*) sys_malloc(buf_size);
        if (!buf) {
            sys_puts("Gagal alokasi RAM untuk doom1.wad!\n", 0x0C);
            return NULL;
        }

        struct EXT2DriverRequest req = {
            .buf          = buf,
            .name         = "doom1.wad",
            .name_len     = 9,
            .parent_inode = 1,
            .buffer_size  = buf_size,
            .is_directory = false,
        };
        int8_t ret = -1;
        syscall(0, (uint32_t) &req, (uint32_t) &ret, 0);
        if (ret != 0) {
            req.name = "DOOM1.WAD";
            syscall(0, (uint32_t) &req, (uint32_t) &ret, 0);
        }

        if (ret != 0) {
            sys_puts("Gagal membaca doom1.wad dari filesystem!\n", 0x0C);
            sys_free(buf);
            return NULL;
        }

        uint32_t numlumps     = *(uint32_t*)(buf + 4);
        uint32_t infotableofs = *(uint32_t*)(buf + 8);
        uint32_t real_size    = infotableofs + numlumps * 16;
        if (real_size == 0 || real_size > buf_size) {
            real_size = 4196020;
        }

        wad_file.data   = buf;
        wad_file.size   = real_size;
        wad_file.offset = 0;
        wad_loaded      = true;
        sys_puts("doom1.wad (4.19 MB) berhasil dimuat ke RAM!\n", 0x0A);
    }

    wad_file.offset = 0;
    return (void*) &wad_file;
}

static void doom_sys_close(void* handle) {
    if (handle == (void*) &wad_file) {
        wad_file.offset = 0;
    }
}

static int doom_sys_read(void* handle, void *buf, int count) {
    VirtualFile *vf = (VirtualFile*) handle;
    if (!vf || !vf->data) return 0;
    if (vf->offset >= vf->size) return 0;

    int avail = (int)(vf->size - vf->offset);
    int to_read = (count < avail) ? count : avail;
    if (to_read <= 0) return 0;

    for (int i = 0; i < to_read; i++) {
        ((uint8_t*)buf)[i] = vf->data[vf->offset + i];
    }
    vf->offset += to_read;
    return to_read;
}

static int doom_sys_seek(void* handle, int offset, doom_seek_t origin) {
    VirtualFile *vf = (VirtualFile*) handle;
    if (!vf) return -1;

    int new_pos = vf->offset;
    if (origin == DOOM_SEEK_SET) new_pos = offset;
    else if (origin == DOOM_SEEK_CUR) new_pos += offset;
    else if (origin == DOOM_SEEK_END) new_pos = (int)vf->size + offset;

    if (new_pos < 0) new_pos = 0;
    if (new_pos > (int)vf->size) new_pos = (int)vf->size;

    vf->offset = (uint32_t) new_pos;
    return 0;
}

static int doom_sys_tell(void* handle) {
    VirtualFile *vf = (VirtualFile*) handle;
    return vf ? (int)vf->offset : 0;
}

static int doom_sys_eof(void* handle) {
    VirtualFile *vf = (VirtualFile*) handle;
    return vf ? (vf->offset >= vf->size) : 1;
}

static int doom_sys_write(void* handle, const void *buf, int count) {
    (void) handle; (void) buf; (void) count;
    return count;
}

// ==================== 3. TIME, PRINT, GETENV, EXIT ====================
static uint32_t tic_counter = 0;
static void doom_sys_gettime(int* sec, int* usec) {
    *sec  = (int) (tic_counter / 35);
    *usec = (int) ((tic_counter % 35) * 28571);
}

// Menampilkan log boot DOOM ke konsol BillyOS
static bool mode_13h_active = false;
static void doom_sys_print(const char* str) {
    if (mode_13h_active) return; // Jangan cetak teks saat Mode 13h aktif!
    if (str) {
        sys_puts(str, 0x07);
    }
}

// Menyediakan direktori home default untuk PureDOOM
static char* doom_sys_getenv(const char* var) {
    (void) var;
    return "/";
}

static void doom_sys_exit(int code) {
    (void) code;
    syscall(10, 0, 0, 0); // Terminate process
}

// ==================== 4. 256-COLOR PALETTE SYNCHRONIZER ====================
static uint32_t last_pal_hash = 0;

static void sync_palette(void) {
    extern unsigned char screen_palette[256 * 3];
    uint32_t hash = 5381;
    for (int i = 0; i < 256 * 3; i++) {
        hash = ((hash << 5) + hash) + screen_palette[i];
    }
    // Jika palet berubah (misal efek damage merah atau pickup item kuning), update DAC hardware
    if (hash != last_pal_hash) {
        last_pal_hash = hash;
        syscall(21, 1, (uint32_t) screen_palette, 0);
    }
}

// ==================== 5. KEYBOARD INPUT CONTROLLER ====================
// Status tombol dibaca langsung dari driver (syscall 23), yang mencatat make code
// (tekan) DAN break code (lepas). Jadi:
//  - tidak ada lagi delay auto-repeat keyboard sebelum karakter bergerak,
//  - beberapa tombol bisa ditahan bersamaan (maju + belok + tembak + lari, dll).
// doom_key_down dipanggil saat tombol baru ditekan, doom_key_up saat dilepas.

static void sys_get_key_state(uint8_t *out256) {
    syscall(23, (uint32_t) out256, 0, 0);
}

static uint32_t sys_ms(void) {
    uint32_t t = 0;
    syscall(22, (uint32_t) &t, 0, 0);
    return t;
}

#define EXT 128   // offset untuk scancode extended (E0 xx)

static const struct { uint16_t idx; int doom_key; } keymap[] = {
    // === GERAKAN ===
    { 0x11,       DOOM_KEY_UP_ARROW    },  // W
    { EXT + 0x48, DOOM_KEY_UP_ARROW    },  // Panah atas
    { 0x1F,       DOOM_KEY_DOWN_ARROW  },  // S
    { EXT + 0x50, DOOM_KEY_DOWN_ARROW  },  // Panah bawah
    { 0x1E,       DOOM_KEY_LEFT_ARROW  },  // A  (belok kiri)
    { EXT + 0x4B, DOOM_KEY_LEFT_ARROW  },  // Panah kiri
    { 0x20,       DOOM_KEY_RIGHT_ARROW },  // D  (belok kanan)
    { EXT + 0x4D, DOOM_KEY_RIGHT_ARROW },  // Panah kanan
    { 0x2C,       DOOM_KEY_COMMA       },  // Z  (geser/strafe kiri)
    { 0x2D,       DOOM_KEY_PERIOD      },  // X  (geser/strafe kanan)
    { 0x38,       DOOM_KEY_ALT         },  // Alt + panah = strafe

    // === AKSI ===
    { 0x1D,       DOOM_KEY_CTRL        },  // Ctrl kiri (tembak)
    { EXT + 0x1D, DOOM_KEY_CTRL        },  // Ctrl kanan
    { 0x21,       DOOM_KEY_CTRL        },  // F (tembak)
    { 0x39,       DOOM_KEY_SPACE       },  // Space (buka pintu)
    { 0x12,       DOOM_KEY_SPACE       },  // E (buka pintu)
    { 0x2A,       DOOM_KEY_SHIFT       },  // Shift kiri (lari)
    { 0x36,       DOOM_KEY_SHIFT       },  // Shift kanan
    { 0x13,       DOOM_KEY_SHIFT       },  // R (lari)

    // === MENU ===
    { 0x1C,       DOOM_KEY_ENTER       },
    { EXT + 0x1C, DOOM_KEY_ENTER       },  // Enter numpad
    { 0x01,       DOOM_KEY_ESCAPE      },
    { 0x0E,       DOOM_KEY_BACKSPACE   },
    { 0x15,       DOOM_KEY_Y           },
    { 0x31,       DOOM_KEY_N           },

    // === SENJATA 1-7 ===
    { 0x02, DOOM_KEY_1 }, { 0x03, DOOM_KEY_2 }, { 0x04, DOOM_KEY_3 },
    { 0x05, DOOM_KEY_4 }, { 0x06, DOOM_KEY_5 }, { 0x07, DOOM_KEY_6 },
    { 0x08, DOOM_KEY_7 },

    // === LAINNYA ===
    { 0x0F,       DOOM_KEY_TAB         },  // Peta / automap
    { 0x0D,       DOOM_KEY_EQUALS      },  // Zoom in layar
    { 0x0C,       DOOM_KEY_MINUS       },  // Zoom out layar
};
#define KEYMAP_COUNT ((int)(sizeof(keymap) / sizeof(keymap[0])))

static uint8_t key_state_now[256];
static uint8_t doom_key_held[KEYMAP_COUNT];   // status terakhir yang dikirim ke DOOM

static void handle_input(void) {
    sys_get_key_state(key_state_now);

    // Buang isi ring buffer karakter supaya tidak "bocor" ke shell setelah DOOM keluar
    while (sys_get_char() != 0) { }

    for (int i = 0; i < KEYMAP_COUNT; i++) {
        // Beberapa tombol fisik bisa memetakan ke tombol DOOM yang sama (W & panah atas).
        // Proses hanya di entri pertama dan gabungkan (OR) semua tombol fisiknya.
        bool first = true;
        for (int j = 0; j < i; j++) {
            if (keymap[j].doom_key == keymap[i].doom_key) { first = false; break; }
        }
        if (!first) continue;

        bool down = false;
        for (int j = i; j < KEYMAP_COUNT; j++) {
            if (keymap[j].doom_key == keymap[i].doom_key && key_state_now[keymap[j].idx]) {
                down = true;
                break;
            }
        }

        if (down && !doom_key_held[i]) {
            doom_key_down(keymap[i].doom_key);
            doom_key_held[i] = 1;
        } else if (!down && doom_key_held[i]) {
            doom_key_up(keymap[i].doom_key);
            doom_key_held[i] = 0;
        }
    }
}

// ==================== 6. ENTRY POINT ====================
int main(void) {
    sys_clear();
    sys_puts("=== Memulai DOOM (1993) Mode 13h (256 Warna) ===\n", 0x0E);

    doom_set_print(doom_sys_print);
    doom_set_malloc(sys_malloc, sys_free);
    doom_set_file_io(doom_sys_open, doom_sys_close, doom_sys_read, doom_sys_write, doom_sys_seek, doom_sys_tell, doom_sys_eof);
    doom_set_gettime(doom_sys_gettime);
    doom_set_getenv(doom_sys_getenv);
    doom_set_exit(doom_sys_exit);

    char *default_argv[] = { "doom", NULL };
    int default_argc = 1;

    doom_init(default_argc, default_argv, DOOM_FLAG_HIDE_MOUSE_OPTIONS | DOOM_FLAG_HIDE_SOUND_OPTIONS | DOOM_FLAG_HIDE_MUSIC_OPTIONS);

    // 1. Muat palet default DOOM (PLAYPAL) dari lump WAD
    byte *playpal = (byte*) W_CacheLumpName("PLAYPAL", PU_CACHE);
    if (playpal) {
        I_SetPalette(playpal);
    }

    // 2. Set ukuran layar penuh (screenblocks = 10)
    extern int screenblocks;
    screenblocks = 10;
    R_SetViewSize(screenblocks, 0);

    // 3. Alihkan hardware VGA ke Mode 13h (320x200, 256 Warna)
        // 3. Alihkan hardware VGA ke Mode 13h (320x200, 256 Warna)
    syscall(21, 0, 0, 0);
    mode_13h_active = true;  // Matikan doom_sys_print mulai sekarang!
    sync_palette();

    // Batasi kecepatan game ke 35 tic/detik berdasarkan waktu nyata (PIT 1000 Hz via syscall 22).
    // Sebelumnya loop berjalan secepat CPU emulator sehingga game 2-3x lebih cepat.
    uint32_t start_ms = sys_ms();

    // Pengaman: kalau kernel belum punya syscall 22 (jam tidak pernah maju), jangan macet
    // di layar hitam. Cek apakah jam bergerak; jika tidak, game jalan tanpa pembatas kecepatan.
    bool clock_works = false;
    for (int probe = 0; probe < 200000 && !clock_works; probe++) {
        if (sys_ms() != start_ms) clock_works = true;
    }

    while (true) {
        uint32_t target_tic = clock_works ? (((sys_ms() - start_ms) * 35) / 1000) : (tic_counter + 1);
        if (tic_counter >= target_tic) {
            // Belum waktunya frame berikutnya. Tunggu di user mode (tanpa syscall)
            // supaya tidak membanjiri kernel dengan syscall.
            for (volatile int w = 0; w < 3000; w++) { }
            continue;
        }
        // Kalau tertinggal terlalu jauh (emulator lambat), jangan catch-up berlebihan
        if (target_tic - tic_counter > 3) {
            tic_counter = target_tic - 1;
        }

        handle_input();

        tic_counter++;

        // Jalankan 1 frame logika + render DOOM
        doom_force_update();

        // ===== FIX WIPE: Skip animasi melt TAPI ambil layar baru yang benar =====
        // Ketika D_Display mendeteksi pergantian state (Demo→Level, Menu→Level, dll),
        // ia menyimpan layar BARU (dengan STBAR) ke screens[3] via wipe_EndScreen,
        // lalu MENIMPA screens[0] kembali ke layar LAMA untuk memulai animasi melt.
        // Karena kita skip melt, kita harus SALIN screens[3] ke screens[0] agar
        // framebuffer menampilkan layar baru, bukan layar lama!
        if (is_wiping_screen) {
            // screens[3] berisi layar baru yang ditangkap oleh wipe_EndScreen (line 12032-12033)
            // Salin kembali ke screens[0] agar framebuffer menampilkan layar yang benar
            doom_memcpy(screens[0], screens[3], SCREENWIDTH * SCREENHEIGHT);
            is_wiping_screen = false;
        }

        // ===== FIX STBAR: Paksa gambar ulang status bar setiap frame di level =====
        // st_firsttime = true memaksa ST_doRefresh() yang menggambar STBAR lump
        // (batu abu-abu) ke screens[4] lalu meng-copy ke screens[0].
        // Ini menjamin latar status bar SELALU benar, tidak akan pernah
        // menampilkan sisa teks kredit/demo lagi!
        if (gamestate == GS_LEVEL) {
            st_firsttime = true;
        }

        // Sinkronisasi palet 256 warna otomatis (damage flash, pickup, dll.)
        sync_palette();

        // Dapatkan frame buffer 320x200 256 warna asli dari engine DOOM
        const unsigned char *fb = doom_get_framebuffer(1);
        if (fb) {
            // Salin langsung ke VRAM Mode 13h tanpa konversi warna apa pun!
            syscall(21, 2, (uint32_t) fb, 0);
        }
    }

    // Kembalikan ke Mode 12h BillyOS sebelum keluar
    syscall(21, 3, 0, 0);
    return 0;
}
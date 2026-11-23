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
// Hold-Timer: Saat tombol ditekan, DOOM_KEY_DOWN dipanggil dan tombol
// "ditahan" selama KEY_HOLD_FRAMES. Saat timer habis, DOOM_KEY_UP dipanggil.
// Jika tombol ditekan lagi (auto-repeat), timer di-refresh.

#define KEY_HOLD_FRAMES 8    // Tahan 8 frame (~228ms pada 35 FPS)
#define MAX_HELD_KEYS   12   // Maks tombol bersamaan

static struct {
    int doom_key;
    int timer;
} held_keys[MAX_HELD_KEYS];
static int num_held = 0;

static void press_key(int doom_key) {
    // Jika sudah ditahan, refresh timer
    for (int i = 0; i < num_held; i++) {
        if (held_keys[i].doom_key == doom_key) {
            held_keys[i].timer = KEY_HOLD_FRAMES;
            return;
        }
    }
    // Tombol baru
    if (num_held < MAX_HELD_KEYS) {
        doom_key_down(doom_key);
        held_keys[num_held].doom_key = doom_key;
        held_keys[num_held].timer = KEY_HOLD_FRAMES;
        num_held++;
    }
}

static void handle_input(void) {
    // 1. Kurangi timer, lepas tombol yang sudah expired
    for (int i = 0; i < num_held; ) {
        held_keys[i].timer--;
        if (held_keys[i].timer <= 0) {
            doom_key_up(held_keys[i].doom_key);
            held_keys[i] = held_keys[num_held - 1];
            num_held--;
        } else {
            i++;
        }
    }

    // 2. Baca SEMUA karakter dari ring buffer keyboard
    char c = 0;
    while ((c = sys_get_char()) != 0) {
        switch ((uint8_t) c) {
            // === GERAKAN ===
            case 0xAD: case 'w': case 'W':  // Maju
                press_key(DOOM_KEY_UP_ARROW); break;
            case 0xAF: case 's': case 'S':  // Mundur
                press_key(DOOM_KEY_DOWN_ARROW); break;
            case 0xAC: case 'a': case 'A':  // Belok kiri
                press_key(DOOM_KEY_LEFT_ARROW); break;
            case 0xAE: case 'd': case 'D':  // Belok kanan
                press_key(DOOM_KEY_RIGHT_ARROW); break;

            // === AKSI ===
            case 0x9D:                      // Left Ctrl (tembak)
            case 'f': case 'F':             // F juga tembak
                press_key(DOOM_KEY_CTRL); break;
            case ' ':                       // Space (buka pintu / pakai)
            case 'e': case 'E':             // E juga buka pintu
                press_key(DOOM_KEY_SPACE); break;
            case 0x9E:                      // Shift (lari cepat!)
            case 'r': case 'R':             // R juga lari
                press_key(DOOM_KEY_SHIFT); break;

            // === MENU ===
            case '\n': case '\r':           // Enter (konfirmasi menu)
                press_key(DOOM_KEY_ENTER); break;
            case 0x1B:                      // Escape (menu / batal)
                press_key(DOOM_KEY_ESCAPE); break;
            case 'y': case 'Y':             // Y (konfirmasi quit)
                press_key(DOOM_KEY_Y); break;

            // === SENJATA 1-7 ===
            case '1': press_key(DOOM_KEY_1); break;  // Tinju / Gergaji
            case '2': press_key(DOOM_KEY_2); break;  // Pistol
            case '3': press_key(DOOM_KEY_3); break;  // Shotgun
            case '4': press_key(DOOM_KEY_4); break;  // Chaingun
            case '5': press_key(DOOM_KEY_5); break;  // Rocket Launcher
            case '6': press_key(DOOM_KEY_6); break;  // Plasma Gun
            case '7': press_key(DOOM_KEY_7); break;  // BFG 9000

            // === LAINNYA ===
            case '\t':                      // Tab (peta / automap)
                press_key(DOOM_KEY_TAB); break;
            case '+': case '=':             // Zoom in layar
                press_key(DOOM_KEY_EQUALS); break;
            case '-': case '_':             // Zoom out layar
                press_key(DOOM_KEY_MINUS); break;

            default: break;
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

    while (true) {
        handle_input();

        // Majukan tepat 1 tic per frame render (35 FPS mulus tanpa lag!)
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
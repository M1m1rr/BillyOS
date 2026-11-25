#include <stdint.h>
#include <stdbool.h>

#define NULL ((void*)0)
#define BADAPPLE_MAGIC 0x50504142u /* "BAPP" */
#define FRAME_W        320u
#define FRAME_H        200u
#define FRAME_PIXELS   (FRAME_W * FRAME_H)

// ==================== 1. SYSCALL WRAPPERS ====================

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static void* sys_malloc(uint32_t size) {
    if (size == 0) return NULL;
    void *ptr = NULL;
    syscall(20, 0, (uint32_t)&ptr, size);
    return ptr;
}

static void sys_free(void *ptr) {
    if (!ptr) return;
    bool ok = false;
    syscall(20, 1, (uint32_t)&ok, (uint32_t)ptr);
}

static uint32_t str_len(const char *s) {
    uint32_t l = 0;
    while (s[l]) l++;
    return l;
}

static void sys_clear(void) {
    syscall(8, 0, 0, 0);
}

static void sys_puts(const char *str, uint8_t color) {
    if (!str) return;
    syscall(6, (uint32_t)str, str_len(str), (uint32_t)color);
}

static char sys_get_char(void) {
    char c = 0;
    syscall(4, (uint32_t)&c, 0, 0);
    return c;
}

static uint32_t sys_ms(void) {
    uint32_t t = 0;
    syscall(22, (uint32_t)&t, 0, 0);
    return t;
}

static void sys_get_key_state(uint8_t *out256) {
    syscall(23, (uint32_t)out256, 0, 0);
}

static void sys_speaker_play(uint32_t freq_hz) {
    syscall(24, 0, freq_hz, 0);
}

static void sys_speaker_stop(void) {
    syscall(24, 1, 0, 0);
}

// ==================== 2. CONTAINER & EXT2 REQUEST ====================

struct EXT2DriverRequest {
    void     *buf;
    char     *name;
    uint8_t   name_len;
    uint32_t  parent_inode;
    uint32_t  buffer_size;
    bool      is_directory;
} __attribute__((packed));

struct BadAppleHeader {
    uint32_t magic;
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint16_t version;
    uint32_t frame_count;
    uint32_t audio_offset;
    uint32_t audio_count;
} __attribute__((packed));

struct BadAppleAudioEvent {
    uint32_t time_ms;
    uint16_t freq_hz;
    uint16_t flags;
} __attribute__((packed));

static uint8_t* load_video_file(const char *filename, uint32_t *out_cap) {
    uint32_t caps[2] = { 6u * 1024u * 1024u, 8462336u };
    uint8_t len = (uint8_t)str_len(filename);

    for (int attempt = 0; attempt < 2; attempt++) {
        uint32_t cap = caps[attempt];
        uint8_t *buf = (uint8_t*)sys_malloc(cap);
        if (!buf) return NULL;

        struct EXT2DriverRequest req = {
            .buf          = buf,
            .name         = (char*)filename,
            .name_len     = len,
            .parent_inode = 1,
            .buffer_size  = cap,
            .is_directory = false,
        };
        int8_t ret = -1;
        syscall(0, (uint32_t)&req, (uint32_t)&ret, 0);
        if (ret == 0) {
            *out_cap = cap;
            return buf;
        }
        sys_free(buf);
        if (ret != 2) {
            // 2 = buffer terlalu kecil (coba lagi dengan 8.07 MiB); error lain langsung gagal
            return NULL;
        }
    }
    return NULL;
}

// ==================== 3. RLE FRAME DECODER & MINI HUD ====================

static void fill_bytes(uint8_t *dst, uint8_t val, uint32_t count) {
    if (count >= 16u) {
        uint32_t val32 = (uint32_t)val * 0x01010101u;
        while (((uint32_t)dst & 3u) && count > 0u) {
            *dst++ = val;
            count--;
        }
        uint32_t *dst32 = (uint32_t*)dst;
        uint32_t words = count >> 2;
        while (words--) {
            *dst32++ = val32;
        }
        dst = (uint8_t*)dst32;
        count &= 3u;
    }
    while (count--) {
        *dst++ = val;
    }
}

static void decode_rle_frame(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t total_px) {
    uint32_t si = 0;
    uint32_t di = 0;

    while (di < total_px && si < src_len) {
        uint8_t b = src[si++];
        uint8_t color = (b & 0x80u) ? 0xFFu : 0x00u;
        uint32_t run_len;

        if ((b & 0x40u) == 0u) {
            run_len = (uint32_t)(b & 0x3Fu) + 1u;
        } else {
            if (si >= src_len) break;
            uint8_t b2 = src[si++];
            run_len = (((uint32_t)(b & 0x3Fu) << 8) | (uint32_t)b2) + 1u;
        }

        uint32_t remain = total_px - di;
        if (run_len > remain) run_len = remain;
        fill_bytes(dst + di, color, run_len);
        di += run_len;
    }

    if (di < total_px) {
        fill_bytes(dst + di, 0x00u, total_px - di);
    }
}

// Mini font 3x5 (digit 0-9, 'F', 'P', 'S', ' ') untuk opsional overlay FPS (tekan 'F')
static const uint8_t mini_font[14][5] = {
    { 7, 5, 5, 5, 7 }, // 0
    { 2, 6, 2, 2, 7 }, // 1
    { 7, 1, 7, 4, 7 }, // 2
    { 7, 1, 7, 1, 7 }, // 3
    { 5, 5, 7, 1, 1 }, // 4
    { 7, 4, 7, 1, 7 }, // 5
    { 7, 4, 7, 5, 7 }, // 6
    { 7, 1, 1, 2, 2 }, // 7
    { 7, 5, 7, 5, 7 }, // 8
    { 7, 5, 7, 1, 7 }, // 9
    { 7, 4, 7, 4, 4 }, // 10: F
    { 7, 5, 7, 4, 4 }, // 11: P
    { 7, 4, 7, 1, 7 }, // 12: S
    { 0, 0, 0, 0, 0 }  // 13: space
};

static void draw_fps_overlay(uint8_t *fb, uint32_t fps_val) {
    // Kotak latar gelap di pojok kiri atas (28x9 piksel) agar terbaca di layar hitam & putih
    for (uint32_t y = 2; y < 11; y++) {
        for (uint32_t x = 2; x < 30; x++) {
            fb[y * FRAME_W + x] = 32;
        }
    }

    uint32_t glyphs[6];
    if (fps_val > 99u) fps_val = 99u;
    glyphs[0] = (fps_val >= 10u) ? (fps_val / 10u) : 13u;
    glyphs[1] = fps_val % 10u;
    glyphs[2] = 13u;
    glyphs[3] = 10u; // F
    glyphs[4] = 11u; // P
    glyphs[5] = 12u; // S

    for (uint32_t g = 0; g < 6; g++) {
        const uint8_t *bm = mini_font[glyphs[g]];
        uint32_t bx = 4u + g * 4u;
        for (uint32_t row = 0; row < 5; row++) {
            uint8_t bits = bm[row];
            for (uint32_t col = 0; col < 3; col++) {
                if (bits & (4u >> col)) {
                    fb[(4u + row) * FRAME_W + (bx + col)] = 255u;
                }
            }
        }
    }
}

// ==================== 4. INPUT HELPERS ====================

static bool poll_controls(uint8_t *key_state, bool *prev_f_down, bool *show_fps) {
    sys_get_key_state(key_state);

    bool esc_pressed  = (key_state[0x01] != 0);
    bool q_pressed    = (key_state[0x10] != 0);
    bool ctrl_pressed = (key_state[0x1D] != 0) || (key_state[128 + 0x1D] != 0);
    bool ctrl_c       = ctrl_pressed && (key_state[0x2E] != 0);

    bool f_now   = (key_state[0x21] != 0);
    bool toggled = false;
    if (f_now && !*prev_f_down) {
        *show_fps = !*show_fps;
        toggled   = true;
    }
    *prev_f_down = f_now;

    bool ring_exit = false;
    char c;
    while ((c = sys_get_char()) != 0) {
        if (c == 0x1B || c == 'q' || c == 'Q') {
            ring_exit = true;
        } else if ((c == 'f' || c == 'F') && !toggled && !f_now) {
            *show_fps = !*show_fps;
            toggled   = true;
        }
    }

    return esc_pressed || q_pressed || ctrl_c || ring_exit;
}

static void drain_exit_keys(uint8_t *key_state) {
    uint32_t wait_until = sys_ms() + 300u;
    while (sys_ms() < wait_until) {
        sys_get_key_state(key_state);
        while (sys_get_char() != 0) {}
        if (!key_state[0x01] && !key_state[0x10] && !key_state[0x2E]) {
            break;
        }
        for (volatile int w = 0; w < 2000; w++) {
            __asm__ volatile("pause");
        }
    }
    while (sys_get_char() != 0) {}
}

// ==================== 5. ENTRY POINT ====================

int main(int argc, char *argv[]) {
    const char *filename = "badapple.dat";
    if (argc > 1 && argv && argv[1] && argv[1][0] != '\0') {
        filename = (argv[1][0] == '/' && argv[1][1] != '\0') ? (argv[1] + 1) : argv[1];
    }

    sys_clear();
    sys_puts("=== BillyOS Bad Apple!! Video Player (Mode 13h 320x200) ===\n", 0x0E);
    sys_puts("Membaca file video lokal: ", 0x0B);
    sys_puts(filename, 0x0F);
    sys_puts(" ...\n", 0x0B);
    sys_puts("Kontrol: [ESC] / [Q] / [Ctrl+C] Keluar  |  [F] Toggle FPS\n", 0x07);

    uint32_t file_cap = 0;
    uint8_t *file_buf = load_video_file(filename, &file_cap);
    if (!file_buf) {
        sys_puts("Gagal membuka atau memuat file ", 0x0C);
        sys_puts(filename, 0x0E);
        sys_puts("!\n", 0x0C);
        return 1;
    }

    const struct BadAppleHeader *hdr = (const struct BadAppleHeader*)file_buf;
    if (file_cap < sizeof(struct BadAppleHeader) ||
        hdr->magic != BADAPPLE_MAGIC ||
        hdr->version != 1u ||
        hdr->width != FRAME_W ||
        hdr->height != FRAME_H ||
        hdr->fps == 0u ||
        hdr->frame_count == 0u) {
        sys_puts("Format header file Bad Apple tidak valid!\n", 0x0C);
        sys_free(file_buf);
        return 1;
    }

    const uint32_t *frame_offsets = (const uint32_t*)(file_buf + sizeof(struct BadAppleHeader));
    uint32_t min_audio_off = (uint32_t)sizeof(struct BadAppleHeader) + (hdr->frame_count + 1u) * 4u;
    uint32_t file_size = frame_offsets[hdr->frame_count];
    if (hdr->audio_offset < min_audio_off ||
        file_size > file_cap ||
        frame_offsets[0] < hdr->audio_offset + hdr->audio_count * sizeof(struct BadAppleAudioEvent)) {
        sys_puts("Metadata offset file Bad Apple rusak!\n", 0x0C);
        sys_free(file_buf);
        return 1;
    }

    const struct BadAppleAudioEvent *audio_events =
        (const struct BadAppleAudioEvent*)(file_buf + hdr->audio_offset);

    uint8_t *frame_buf = (uint8_t*)sys_malloc(FRAME_PIXELS);
    if (!frame_buf) {
        sys_puts("Gagal alokasi framebuffer 320x200!\n", 0x0C);
        sys_free(file_buf);
        return 1;
    }

    // Aktifkan VGA Mode 13h (320x200 linier, 256 warna)
    syscall(21, 0, 0, 0);

    // Muat palet grayscale 256 level (0 = hitam murni, 255 = putih murni)
    uint8_t pal[256 * 3];
    for (uint32_t i = 0; i < 256u; i++) {
        pal[i * 3u + 0u] = (uint8_t)i;
        pal[i * 3u + 1u] = (uint8_t)i;
        pal[i * 3u + 2u] = (uint8_t)i;
    }
    syscall(21, 1, (uint32_t)pal, 0);

    uint8_t key_state[256];
    bool prev_f_down = false;
    bool show_fps    = false;

    uint32_t start_ms = sys_ms();
    bool clock_works = false;
    for (uint32_t probe = 0; probe < 200000u && !clock_works; probe++) {
        if (sys_ms() != start_ms) clock_works = true;
    }
    start_ms = sys_ms();

    uint32_t frame_idx       = 0;
    uint32_t audio_idx       = 0;
    bool     audio_active    = false;
    uint32_t fps_window_ms   = start_ms;
    uint32_t fps_rendered    = 0;
    uint32_t current_fps     = hdr->fps;

    while (frame_idx < hdr->frame_count) {
        if (poll_controls(key_state, &prev_f_down, &show_fps)) {
            break;
        }

        uint32_t now_ms  = sys_ms();
        uint32_t elapsed = clock_works ? (now_ms - start_ms) : (frame_idx * 1000u / hdr->fps);

        // Sinkronisasi event audio PC Speaker presisi 1 ms
        while (audio_idx < hdr->audio_count && elapsed >= audio_events[audio_idx].time_ms) {
            const struct BadAppleAudioEvent *ev = &audio_events[audio_idx++];
            if (ev->freq_hz > 0u) {
                sys_speaker_play((uint32_t)ev->freq_hz);
                audio_active = true;
            } else if (ev->flags & 1u) {
                if (audio_active) {
                    sys_speaker_stop();
                    audio_active = false;
                }
            } else {
                // Jeda antar-not pendek: pertahankan 20 Hz sub-audible agar PulseAudio tidak stutter
                if (audio_active) {
                    sys_speaker_play(20u);
                }
            }
        }

        uint32_t target_frame = clock_works ? ((elapsed * (uint32_t)hdr->fps) / 1000u) : frame_idx;
        if (frame_idx > target_frame) {
            // Tunggu di user space hingga milidetik berikutnya / frame berikutnya
            while (sys_ms() == now_ms) {
                for (volatile int w = 0; w < 500; w++) {
                    __asm__ volatile("pause");
                }
            }
            continue;
        }

        // Jika tertinggal > 1 frame, langsung ke target_frame (setiap frame RLE independen)
        if (target_frame > frame_idx + 1u) {
            frame_idx = target_frame;
            if (frame_idx >= hdr->frame_count) break;
        }

        uint32_t f_start = frame_offsets[frame_idx];
        uint32_t f_end   = frame_offsets[frame_idx + 1u];
        if (f_end < f_start || f_end > file_size) break;

        decode_rle_frame(file_buf + f_start, f_end - f_start, frame_buf, FRAME_PIXELS);

        fps_rendered++;
        uint32_t fps_dt = now_ms - fps_window_ms;
        if (fps_dt >= 1000u) {
            current_fps   = (fps_rendered * 1000u) / fps_dt;
            fps_rendered  = 0;
            fps_window_ms = now_ms;
        }

        if (show_fps) {
            draw_fps_overlay(frame_buf, current_fps);
        }

        syscall(21, 2, (uint32_t)frame_buf, 0);
        frame_idx++;
    }

    sys_speaker_stop();
    drain_exit_keys(key_state);

    // Kembalikan layar ke Mode 12h 640x480 standar BillyOS
    syscall(21, 3, 0, 0);
    sys_clear();

    sys_free(frame_buf);
    sys_free(file_buf);
    return 0;
}
